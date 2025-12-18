/* 
 * Archivo convertido para GitHub desde: Xbee_ESP32_base.ino
 * 
 * Nota importante:
 * - Los sketches de Arduino se compilan como C++ (una variante de C).
 * - Este archivo mantiene la misma lógica que el original; solo cambia el formato/nombre (ahora .cpp).
 * - Para COMPILAR en Arduino IDE / PlatformIO, lo más seguro es usar extensión .ino o .cpp.
 */

#include <Arduino.h>

// =======================================================
//   CONFIGURACIÓN XBEE / UART2
// =======================================================
const uint32_t XBEE_BAUD = 9600;
const int XBEE_RX_PIN    = 16; // DOUT del XBee -> RX2 del ESP32
const int XBEE_TX_PIN    = 17; // DIN del XBee <- TX2 del ESP32

// =======================================================
//   CONSTANTES DE CÁLCULO
// =======================================================

// Ventana de integración: 1 hora (en ms y s)
const unsigned long PUBLISH_FREQUENCY = 3600000UL;  // 1 hora en millis
const float         T_WINDOW_SEC      = 3600.0f;    // 1 hora en segundos

// Factor de actividad de Livio: 0.43 CPS/Bq/L
const float S_act_cps_per_BqL = 0.43f;

// Intervalo de heartbeat (mensaje "vivo"): 15 minutos
const unsigned long HEARTBEAT_INTERVAL_MS = 900000UL; // 15 * 60 * 1000

// =======================================================
//   ACUMULADORES DE CUENTAS EN LA VENTANA DE 1 HORA
// =======================================================
unsigned long pulsesNode1Hour = 0;   // solo Nodo_1 en la hora
unsigned long pulsesNode2Hour = 0;   // solo Nodo_2 en la hora
unsigned long lastPublish     = 0;
unsigned long msgCount        = 0;

// buffer para armar líneas desde Serial2
String xbeeLine = "";

// =======================================================
//   ENVIAR ACTIVIDAD AL RASPBERRY PI POR SERIAL (JSON)
// =======================================================
void sendActivityToRpiSerial(float Cn1_Bq_m3,
                             float Cn2_Bq_m3) {
  String payload = "{";
  payload += "\"radon_activity_nodo1\":" + String(Cn1_Bq_m3, 3) + ",";
  payload += "\"radon_activity_nodo2\":" + String(Cn2_Bq_m3, 3);
  payload += "}";

  Serial.print("RADON_JSON ");
  Serial.println(payload);

  Serial.println("Enviado al Raspberry Pi:");
  Serial.println(payload);
}

// =======================================================
//   PROCESAR MENSAJES DE NODOS: "Nodo_1;C=123"
// =======================================================
void processNodeMessage(const String& msg) {
  Serial.print("\n[processNodeMessage] msg = \"");
  Serial.print(msg);
  Serial.println("\"");

  int sep = msg.indexOf(';');
  Serial.print("sep = ");
  Serial.println(sep);

  if (sep < 0) {
    Serial.println(" -> Formato inválido (sin ';'), se ignora.");
    return;
  }

  String nodeId = msg.substring(0, sep);  // "Nodo_1", "Nodo_2", etc.
  nodeId.trim();

  int idxC = msg.indexOf("C=", sep + 1);
  Serial.print("idxC = ");
  Serial.println(idxC);

  if (idxC < 0) {
    Serial.println(" -> No se encontró 'C=', se ignora.");
    return;
  }

  String cStr = msg.substring(idxC + 2);
  cStr.trim();
  unsigned long delta = (unsigned long)cStr.toInt();

  Serial.print(" -> Pulsos recibidos desde ");
  Serial.print(nodeId);
  Serial.print(" = ");
  Serial.println(delta);

  if (nodeId.indexOf('1') >= 0) {
    pulsesNode1Hour += delta;
  } else if (nodeId.indexOf('2') >= 0) {
    pulsesNode2Hour += delta;
  } else {
    Serial.println(" -> Nodo desconocido, se ignora para acumuladores.");
  }

  Serial.print("   Acumulado Nodo_1 (cuentas/hora) = ");
  Serial.println(pulsesNode1Hour);
  Serial.print("   Acumulado Nodo_2 (cuentas/hora) = ");
  Serial.println(pulsesNode2Hour);
}

// =======================================================
//   DETECTAR MENSAJE DE HANDSHAKE "Nodo_X;HELLO"
// =======================================================
bool processHandshakeMessage(const String& msg) {
  if (msg.indexOf("HELLO") < 0) {
    return false; // no es handshake
  }

  int sep = msg.indexOf(';');
  if (sep < 0) return false;

  String nodeId = msg.substring(0, sep);
  nodeId.trim();

  Serial.println();
  Serial.println("========================================");
  Serial.print(" [HANDSHAKE] Mensaje de conexión desde ");
  Serial.println(nodeId);
  Serial.println("========================================");

  Serial.print("[HANDSHAKE] Conectado: ");
  Serial.println(nodeId);

  return true;
}

// =======================================================
//   SETUP
// =======================================================
void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("Base ESP32 (actividad radón, 2 nodos) -> Raspberry Pi por USB");

  Serial2.begin(XBEE_BAUD, SERIAL_8N1, XBEE_RX_PIN, XBEE_TX_PIN);
  Serial.print("UART2 configurado en RX=");
  Serial.print(XBEE_RX_PIN);
  Serial.print(" TX=");
  Serial.println(XBEE_TX_PIN);

  lastPublish = millis();
}

// =======================================================
//   LOOP
// =======================================================
void loop() {
  // Heartbeat cada 15 minutos
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeat = millis();
    Serial.println("[loop] ESP32 vivo, esperando datos del XBee...");
  }

  // 1) Leer mensajes desde el XBee
  while (Serial2.available()) {
    char c = Serial2.read();

    Serial.write(c);  // eco hacia el Raspi

    if (c == '\n' || c == '\r') {
      xbeeLine.trim();
      if (xbeeLine.length() > 0) {
        msgCount++;
        Serial.println();
        Serial.println("========================================");
        Serial.print(" MENSAJE #");
        Serial.println(msgCount);
        Serial.println("========================================");

        // Primero: ¿es handshake (HELLO)?
        if (!processHandshakeMessage(xbeeLine)) {
          // Si no es HELLO, lo procesamos como medición
          processNodeMessage(xbeeLine);
        }
      }
      xbeeLine = "";
    } else {
      if (c >= 32 && c <= 126) {
        xbeeLine += c;
      }
    }
  }

  // 2) Cada hora: calcular actividad y enviarla al Raspi
  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_FREQUENCY) {
    lastPublish = now;

    Serial.println();
    Serial.println("==== Ventana de 1 hora completada ====");
    Serial.print("Cuentas totales Nodo_1: ");
    Serial.println(pulsesNode1Hour);
    Serial.print("Cuentas totales Nodo_2: ");
    Serial.println(pulsesNode2Hour);

    float N1 = (float)pulsesNode1Hour;
    float N2 = (float)pulsesNode2Hour;

    float Cn1_Bq_m3 = N1 * 1000.0f / (T_WINDOW_SEC * S_act_cps_per_BqL);
    float Cn2_Bq_m3 = N2 * 1000.0f / (T_WINDOW_SEC * S_act_cps_per_BqL);

    Serial.println("---- Actividad estimada (Radon Activity) ----");
    Serial.print("Nodo_1: ");
    Serial.print(Cn1_Bq_m3, 3);
    Serial.println(" Bq/m^3");
    Serial.print("Nodo_2: ");
    Serial.print(Cn2_Bq_m3, 3);
    Serial.println(" Bq/m^3");

    sendActivityToRpiSerial(Cn1_Bq_m3, Cn2_Bq_m3);

    pulsesNode1Hour = 0;
    pulsesNode2Hour = 0;
  }
}
