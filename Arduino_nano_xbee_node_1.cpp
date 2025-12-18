/* 
 * Archivo convertido para GitHub desde: Arduino_nano_xbee_node_1.ino
 * 
 * Nota importante:
 * - Los sketches de Arduino se compilan como C++ (una variante de C).
 * - Este archivo mantiene la misma lógica que el original; solo cambia el formato/nombre (ahora .cpp).
 * - Para COMPILAR en Arduino IDE / PlatformIO, lo más seguro es usar extensión .ino o .cpp.
 */

#include <SoftwareSerial.h>

// =======================================================
// CONFIGURACIÓN XBEE Y SERIAL
// =======================================================
const uint8_t XBEE_RX_PIN = 4; // D4 <- DOUT del XBee
const uint8_t XBEE_TX_PIN = 5; // D5 -> DIN del XBee
SoftwareSerial xbeeSerial(XBEE_RX_PIN, XBEE_TX_PIN);

const uint32_t XBEE_BAUD = 9600;
const uint32_t USB_BAUD  = 9600;

// =======================================================
// ENTRADA ANALÓGICA (TP3)
// =======================================================
const uint8_t TP3_PIN = A4;    // TP3 conectado aquí
const float   VREF    = 5.0;   // referencia ADC del Nano
const float   ADC_LSB = VREF / 1023.0; // V por cuenta ADC

// Parámetros de baseline / ruido
float baselineV      = 3.0;    // se ajusta solo
bool  baselineInit   = false;
const float BASE_ALPHA = 0.001; // filtro exponencial muy suave

// =======================================================
// DETECCIÓN DE PULSOS EN TP3
// =======================================================

// Umbrales básicos (ajustables)
const float MIN_DROP_V = 0.27;   // caída mínima para ser candidato
const float MAX_DROP_V = 2.2;   // caída máxima razonable

// Duración del pulso (radón típico ≈ 10–40 ms)
const unsigned long MIN_PULSE_MS = 4;   // más corto = ruido
const unsigned long MAX_PULSE_MS = 70;  // más largo = descarga/ráfaga

// Tiempo muerto corto después de cada pulso procesado
const unsigned long REFRACT_MS        = 100;    // 0.1 s
// Política conservadora: separación mínima entre pulsos válidos
const unsigned long MIN_BETWEEN_VALID = 500;    // 0.5 s (comentario dice 10 s, pero valor actual es 500 ms)

// Detección de ráfagas muy rápidas (descargas, ruido fuerte)
const unsigned long BURST_WINDOW_MS   = 5;   // ventana para ver candidatos
const uint8_t       BURST_COUNT_LIMIT = 5;   // 5 candidatos en 5 ms ⇒ ráfaga
const unsigned long BURST_BLOCK_MS    = 100; // bloqueo de 100 ms después

// Estado de la detección
enum PulseState { PS_IDLE, PS_IN_PULSE, PS_REFRACTORY };
PulseState pulseState = PS_IDLE;

unsigned long pulseStartMs   = 0;
float         pulseStartBase = 3.0;
float         pulseMinV      = 3.0;

// Para ráfagas
unsigned long lastCandMs       = 0;
uint8_t       candInBurstWin   = 0;
bool          burstBlocked     = false;
unsigned long burstBlockEndMs  = 0;

// Separación entre pulsos válidos
unsigned long lastValidPulseMs = 0;

// =======================================================
// CONTADORES Y LED
// =======================================================

unsigned long pulseCountTotal  = 0;   // pulsos válidos totales
unsigned long pulseCountPeriod = 0;   // pulsos válidos del último minuto

// Envío cada 60 s
const unsigned long PERIODO_ENVIO_MS = 60000UL;
unsigned long ultimoEnvioMs = 0;

// Ventana de silencio alrededor de la comunicación con XBee
const unsigned long MUTE_COMMS_PRE_MS  = 50;  // 50 ms antes de enviar
const unsigned long MUTE_COMMS_POST_MS = 50;  // 50 ms después de enviar

// LED de indicación
const unsigned long LED_PULSE_MS = 50;
bool          ledOn          = false;
unsigned long ledStartMs     = 0;

// =======================================================
// FUNCIONES AUXILIARES
// =======================================================

void registrarPulsoValido(unsigned long ahora) {
  pulseCountTotal++;
  pulseCountPeriod++;

  // LED
  digitalWrite(LED_BUILTIN, HIGH);
  ledOn      = true;
  ledStartMs = ahora;

  lastValidPulseMs = ahora;

  Serial.print("Pulso RADON valido. Total = ");
  Serial.println(pulseCountTotal);
}

// Handshake de conexión al arrancar
void sendHandshake() {
  String msg = "Nodo_1;HELLO";
  xbeeSerial.println(msg);

  Serial.print(F("Handshake enviado desde Nodo 1 -> "));
  Serial.println(msg);
}

// =======================================================
// SETUP
// =======================================================

void setup() {
  Serial.begin(USB_BAUD);
  xbeeSerial.begin(XBEE_BAUD);

  pinMode(TP3_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println(F("Nodo radon NODO 1 iniciado (TP3 en A4, discriminacion en software)."));

  delay(500);       // pequeña espera para que el XBee esté listo
  sendHandshake();  // aviso de conexión al sistema
}

// =======================================================
// LOOP PRINCIPAL
// =======================================================

void loop() {
  unsigned long ahora = millis();

  // -------------------------------
  // Cálculo de ventana de silencio XBee (+/- 50 ms)
  // -------------------------------
  unsigned long tiempoDesdeEnvio = ahora - ultimoEnvioMs;
  bool enVentanaMute = false;

  // 50 ms después del último envío
  if (tiempoDesdeEnvio <= MUTE_COMMS_POST_MS) {
    enVentanaMute = true;
  }
  // 50 ms antes del próximo envío (si aún no llegamos al periodo completo)
  else if (tiempoDesdeEnvio < PERIODO_ENVIO_MS &&
           (PERIODO_ENVIO_MS - tiempoDesdeEnvio) <= MUTE_COMMS_PRE_MS) {
    enVentanaMute = true;
  }

  // -------------------------------
  // Lectura analógica de TP3
  // -------------------------------
  int   raw = analogRead(TP3_PIN);
  float v   = raw * ADC_LSB;

  // Inicializar baseline la primera vez
  if (!baselineInit) {
    baselineV    = v;
    baselineInit = true;
  }

  // ¿estamos en bloqueo por ráfaga?
  if (burstBlocked && ahora >= burstBlockEndMs) {
    burstBlocked = false;
    Serial.println(F("Fin de bloqueo por rafaga."));
  }

  // ---------------------------------------------------
  // MAQUINA DE ESTADOS PARA PULSOS
  // ---------------------------------------------------

  switch (pulseState) {

    case PS_IDLE: {
      // Actualizar baseline SOLO cuando no hay pulso
      baselineV += BASE_ALPHA * (v - baselineV);

      if (burstBlocked) {
        // Ignoramos candidatos mientras dure el bloqueo
        break;
      }

      float drop = baselineV - v;  // caída hacia abajo

      if (drop >= MIN_DROP_V) {
        // ---- CANDIDATO DE PULSO DETECTADO ----

        // Gestión de ráfagas
        if (ahora - lastCandMs <= BURST_WINDOW_MS) {
          candInBurstWin++;
        } else {
          candInBurstWin = 1;
        }
        lastCandMs = ahora;

        if (candInBurstWin >= BURST_COUNT_LIMIT) {
          // Ráfaga detectada → bloquear
          burstBlocked    = true;
          burstBlockEndMs = ahora + BURST_BLOCK_MS;
          candInBurstWin  = 0;
          Serial.println(F("Rafaga detectada: bloqueo 100 ms."));
          break; // NO iniciamos pulso
        }

        // Iniciar seguimiento del pulso
        pulseState     = PS_IN_PULSE;
        pulseStartMs   = ahora;
        pulseStartBase = baselineV;  // baseline al inicio
        pulseMinV      = v;
      }
      break;
    }

    case PS_IN_PULSE: {
      // Mantener mínimo
      if (v < pulseMinV) {
        pulseMinV = v;
      }

      float         dropNow = pulseStartBase - v;
      unsigned long durMs   = ahora - pulseStartMs;

      bool pulseTerminaPorNivel  = (dropNow < (MIN_DROP_V * 0.3)); // casi de vuelta
      bool pulseTerminaPorTiempo = (durMs > MAX_PULSE_MS);

      if (pulseTerminaPorNivel || pulseTerminaPorTiempo) {
        // ---- FIN DEL PULSO: CLASIFICACIÓN ----
        float ampV = pulseStartBase - pulseMinV;

        Serial.print(F("Pulso detectado: amp="));
        Serial.print(ampV, 3);
        Serial.print(F(" V, dur="));
        Serial.print(durMs);
        Serial.println(F(" ms"));

        bool valido = true;

        // 1) Amplitud dentro de rango
        if (ampV < MIN_DROP_V) {
          valido = false;
          Serial.println(F(" -> Rechazado: amplitud demasiado baja."));
        } else if (ampV > MAX_DROP_V) {
          valido = false;
          Serial.println(F(" -> Rechazado: amplitud demasiado alta (posible descarga)."));
        }

        // 2) Duración dentro de rango
        if (durMs < MIN_PULSE_MS || durMs > MAX_PULSE_MS) {
          valido = false;
          Serial.println(F(" -> Rechazado: duracion fuera de rango."));
        }

        // 3) Espaciado mínimo entre pulsos válidos
        if (valido && lastValidPulseMs != 0 &&
            (ahora - lastValidPulseMs) < MIN_BETWEEN_VALID) {
          valido = false;
          Serial.println(F(" -> Rechazado: muy cercano a pulso valido anterior."));
        }

        // 4) Ventana de silencio alrededor de la comunicación XBee
        if (valido && enVentanaMute) {
          valido = false;
          Serial.println(F(" -> Rechazado: ventana de silencio XBee (+/-50 ms)."));
        }

        if (valido && !burstBlocked) {
          registrarPulsoValido(ahora);
        }

        pulseState   = PS_REFRACTORY;
        pulseStartMs = ahora;   // reutilizamos para contar el refractario
      }
      break;
    }

    case PS_REFRACTORY: {
      if (ahora - pulseStartMs >= REFRACT_MS) {
        pulseState = PS_IDLE;
      }
      break;
    }
  }

  // ---------------------------------------------------
  // GESTIÓN DEL LED DE PULSO
  // ---------------------------------------------------
  if (ledOn && (ahora - ledStartMs >= LED_PULSE_MS)) {
    digitalWrite(LED_BUILTIN, LOW);
    ledOn = false;
  }

  // ---------------------------------------------------
  // ENVÍO PERIÓDICO POR XBEE (CADA 60 s)
  // ---------------------------------------------------
  if (ahora - ultimoEnvioMs >= PERIODO_ENVIO_MS) {
    ultimoEnvioMs = ahora;

    unsigned long delta = pulseCountPeriod;
    pulseCountPeriod = 0;

    // *** MENSAJE DE MEDICIÓN DEL NODO 1 ***
    String msg = "Nodo_1;C=" + String(delta);
    xbeeSerial.println(msg);

    Serial.print(F("Enviado al XBee (Nodo 1) -> "));
    Serial.println(msg);
  }
}
