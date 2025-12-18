#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import glob
import re
import serial
import json
import time
from collections import deque

import matplotlib.pyplot as plt
import tkinter as tk
from tkinter import simpledialog

# ==========================================================
# CONFIGURACIÓN DE CARPETA Y NOMBRES DE ARCHIVOS
# ==========================================================

HOME_DIR = os.path.expanduser("~")
BASE_DIR = os.path.join(HOME_DIR, "Desktop", "Datos_radon")
os.makedirs(BASE_DIR, exist_ok=True)

existing_csv = glob.glob(os.path.join(BASE_DIR, "Datos_*.csv"))
next_index = 1
for path in existing_csv:
    name = os.path.basename(path)
    m = re.match(r"Datos_(\d+)\.csv$", name)
    if m:
        idx = int(m.group(1))
        if idx >= next_index:
            next_index = idx + 1

RUN_INDEX = next_index

CSV_PATH = os.path.join(BASE_DIR, f"Datos_{RUN_INDEX}.csv")
FIG_PATH = os.path.join(BASE_DIR, f"Figura_datos_toma_{RUN_INDEX}.eps")

print(f"Carpeta de datos: {BASE_DIR}")
print(f"Archivo de datos: {CSV_PATH}")
print(f"Archivo de figura: {FIG_PATH}")

# ==========================================================
# CONFIGURACIÓN DEL PUERTO SERIE
# ==========================================================
SERIAL_PORT = "/dev/ttyUSB0"
BAUDRATE = 115200

ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)
print(f"Escuchando en {SERIAL_PORT} a {BAUDRATE} baudios...")

# ==========================================================
# ESTRUCTURAS PARA EL DASHBOARD
# ==========================================================
MAX_POINTS = 48

times = deque(maxlen=MAX_POINTS)
n1_vals = deque(maxlen=MAX_POINTS)
n2_vals = deque(maxlen=MAX_POINTS)

# ==========================================================
# VENTANA DE CONTROL (TKINTER) CON BOTÓN "PARAR MEDICIONES"
# ==========================================================
stop_requested = False

def on_stop_clicked():
    global stop_requested
    print("Botón 'Parar mediciones' pulsado. Deteniendo adquisición...")
    stop_requested = True

root = tk.Tk()
root.title("Control radón")
root.geometry("260x120")

label = tk.Label(root, text="Control de medición de radón")
label.pack(pady=5)

stop_button = tk.Button(root,
                        text="Parar mediciones",
                        command=on_stop_clicked,
                        height=2,
                        width=20)
stop_button.pack(pady=10)

# ==========================================================
# FIGURA: DOS GRÁFICAS (UNA POR NODO)
# ==========================================================
plt.ion()
fig, (ax1, ax2) = plt.subplots(2, 1, sharex=True, figsize=(7, 6))

line1, = ax1.plot([], [], marker="o", label="Nodo 1 (Bq/m³)")
line2, = ax2.plot([], [], marker="o", label="Nodo 2 (Bq/m³)")

ax2.set_xlabel("Día y fecha del muestreo")
ax1.set_ylabel("Actividad de Radón [Bq/m³]")
ax2.set_ylabel("Actividad de Radón [Bq/m³]")

base_title1 = "Actividad de radón en el laboratorio nodo 1"
base_title2 = "Actividad de radón en el laboratorio nodo 2"
ax1.set_title(base_title1)
ax2.set_title(base_title2)

ax1.legend()
ax2.legend()
ax1.grid(True)
ax2.grid(True)

def update_plot():
    x = range(len(times))

    line1.set_data(x, list(n1_vals))
    line2.set_data(x, list(n2_vals))

    ax2.set_xticks(list(x))
    ax2.set_xticklabels(list(times), rotation=45, ha="right")

    ax1.relim()
    ax1.autoscale_view(scalex=True, scaley=False)
    ax2.relim()
    ax2.autoscale_view(scalex=True, scaley=False)

    if n1_vals:
        max1 = max(n1_vals)
    else:
        max1 = 1.0

    if n2_vals:
        max2 = max(n2_vals)
    else:
        max2 = 1.0

    if max1 <= 0:
        max1 = 1.0
    if max2 <= 0:
        max2 = 1.0

    ax1.set_ylim(0, max1 * 1.1)
    ax2.set_ylim(0, max2 * 1.1)

    fig.tight_layout()
    plt.draw()
    plt.pause(0.1)

# ==========================================================
# ARCHIVO CSV PARA EXCEL
# ==========================================================
log_file = open(CSV_PATH, "w", buffering=1, newline="")
log_file.write("hora,nodo1_Bq_m3,nodo2_Bq_m3\n")

# ==========================================================
# LOOP PRINCIPAL
# ==========================================================
try:
    while not stop_requested:
        try:
            root.update_idletasks()
            root.update()
        except tk.TclError:
            print("Ventana de control cerrada. Deteniendo adquisición...")
            stop_requested = True
            break

        raw = ser.readline().decode("utf-8", errors="ignore").strip()
        if not raw:
            continue

        print(raw)

        # Mensajes de handshake resaltados
        if "HANDSHAKE" in raw and "Nodo_1" in raw:
            print(">>> Nodo 1 reportado como CONECTADO")
        if "HANDSHAKE" in raw and "Nodo_2" in raw:
            print(">>> Nodo 2 reportado como CONECTADO")

        if "RADON_JSON" not in raw:
            continue  # no es paquete de datos, solo log/handshake

        json_start = raw.find("{")
        if json_start < 0:
            continue

        try:
            data = json.loads(raw[json_start:])
        except json.JSONDecodeError:
            print("JSON inválido:", raw)
            continue

        Cn1 = float(data.get("radon_activity_nodo1", 0.0))
        Cn2 = float(data.get("radon_activity_nodo2", 0.0))

        clock = time.strftime("%Y-%m-%d %H:%M:%S")

        times.append(clock)
        n1_vals.append(Cn1)
        n2_vals.append(Cn2)

        log_file.write(f"{clock},{Cn1},{Cn2}\n")

        update_plot()

except KeyboardInterrupt:
    print("Saliendo por Ctrl+C...")

finally:
    try:
        lab1 = simpledialog.askstring(
            "Laboratorio nodo 1",
            "Escriba el nombre del laboratorio donde se hizo la medición (nodo 1):",
            parent=root
        )
        if lab1 and lab1.strip():
            ax1.set_title(f"Actividad de radón en el laboratorio {lab1.strip()}")
        else:
            ax1.set_title(base_title1)

        lab2 = simpledialog.askstring(
            "Laboratorio nodo 2",
            "Escriba el nombre del laboratorio donde se hizo la medición (nodo 2):",
            parent=root
        )
        if lab2 and lab2.strip():
            ax2.set_title(f"Actividad de radón en el laboratorio {lab2.strip()}")
        else:
            ax2.set_title(base_title2)

    except tk.TclError:
        ax1.set_title(base_title1)
        ax2.set_title(base_title2)

    print("Guardando figura en formato EPS...")
    try:
        plt.ioff()
        fig.canvas.draw()
        fig.savefig(FIG_PATH, format="eps")
        print(f"Figura guardada en: {FIG_PATH}")
    except Exception as e:
        print(f"Error al guardar la figura: {e}")

    log_file.close()
    ser.close()
    try:
        root.destroy()
    except Exception:
        pass
    print("Archivo CSV cerrado, puerto serie cerrado y ventana de control destruida.")