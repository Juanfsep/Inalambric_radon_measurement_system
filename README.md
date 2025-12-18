# Sistema de adquisición de radón (XBee + Arduino/ESP32 + Raspberry Pi)

Este repositorio contiene:

- `Arduino_nano_xbee_node_1.cpp` y `Arduino_nano_xbee_node_2.cpp`: sketches para los nodos (Arduino Nano + XBee).
- `Xbee_ESP32_base.cpp`: sketch para la estación base (ESP32 + XBee) que recibe conteos de ambos nodos y publica por Serial un JSON.
- `radon_dashboard.py`: script de Python para Raspberry Pi que escucha el JSON por puerto serie, registra un CSV y grafica en vivo.

## Nota sobre los archivos `.cpp`
Aunque la extensión sea `.cpp` para GitHub, los sketches de Arduino se compilan como **C++**.
Si quieres compilarlos en Arduino IDE / PlatformIO, puedes mantenerlos como `.cpp` o renombrarlos a `.ino`.

## Python (Raspberry Pi)
Instala dependencias:
```bash
pip3 install -r requirements.txt
```

Ejecuta:
```bash
python3 radon_dashboard.py
```
