#!/usr/bin/env python3
import argparse
import asyncio
import struct
import time
from collections import deque

from bleak import BleakClient, BleakScanner
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

DEFAULT_CHAR_UUID = "b5f90001-8456-4c84-a6b9-5f95a2f2f1f0"


def parse_args():
    parser = argparse.ArgumentParser(description="Lee y grafica PPG crudo por BLE desde InfiniTime")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--name", help="Nombre BLE del reloj (ej: InfiniTime)")
    group.add_argument("--address", help="Dirección BLE (MAC en Linux/Windows, UUID en macOS)")
    parser.add_argument("--char-uuid", default=DEFAULT_CHAR_UUID, help="UUID de característica de PPG crudo")
    parser.add_argument("--window-seconds", type=float, default=20.0, help="Ventana de tiempo visible")
    parser.add_argument("--no-als", action="store_true", help="No graficar ALS")
    return parser.parse_args()


async def find_device(args):
    if args.address:
        return args.address

    devices = await BleakScanner.discover(timeout=8.0)
    for dev in devices:
        if dev.name == args.name:
            return dev.address
    raise RuntimeError(f"No se encontró dispositivo con nombre '{args.name}'")


async def main_async(args):
    address = await find_device(args)
    print(f"Conectando a: {address}")

    t_values = deque(maxlen=5000)
    hrs_values = deque(maxlen=5000)
    als_values = deque(maxlen=5000)
    last_counter = None

    fig, ax = plt.subplots()
    (line_hrs,) = ax.plot([], [], label="HRS raw")
    line_als = None
    if not args.no_als:
        (line_als,) = ax.plot([], [], label="ALS raw")

    ax.set_title("InfiniTime Raw PPG (BLE)")
    ax.set_xlabel("Tiempo (s)")
    ax.set_ylabel("Valor crudo")
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.3)

    t0 = time.time()

    def notification_handler(_sender, data: bytearray):
        nonlocal last_counter
        if len(data) != 12:
            print(f"Paquete inesperado ({len(data)} bytes): {data.hex()}")
            return

        sample_counter, hrs_raw, als_raw = struct.unpack("<III", data)
        if last_counter is not None and sample_counter != (last_counter + 1):
            print(f"Salto de contador: esperado {last_counter + 1}, llegó {sample_counter}")
        last_counter = sample_counter

        t_values.append(time.time() - t0)
        hrs_values.append(hrs_raw)
        als_values.append(als_raw)

    def animate(_frame):
        if not t_values:
            return [line_hrs] + ([line_als] if line_als else [])

        t_min = max(0.0, t_values[-1] - args.window_seconds)
        idx = 0
        for i, tv in enumerate(t_values):
            if tv >= t_min:
                idx = i
                break

        tx = list(t_values)[idx:]
        hy = list(hrs_values)[idx:]
        line_hrs.set_data(tx, hy)

        y_values = hy.copy()
        artists = [line_hrs]

        if line_als is not None:
            ay = list(als_values)[idx:]
            line_als.set_data(tx, ay)
            y_values.extend(ay)
            artists.append(line_als)

        if tx:
            ax.set_xlim(tx[0], tx[-1] + 1e-6)
        if y_values:
            ymin = min(y_values)
            ymax = max(y_values)
            pad = max(10.0, (ymax - ymin) * 0.1)
            ax.set_ylim(ymin - pad, ymax + pad)

        return artists

    async with BleakClient(address) as client:
        print("Conectado. Activando notificaciones...")
        await client.start_notify(args.char_uuid, notification_handler)
        print("Recibiendo datos. Cierra la ventana del gráfico para salir.")

        anim = FuncAnimation(fig, animate, interval=100, blit=False, cache_frame_data=False)
        plt.show()
        del anim

        await client.stop_notify(args.char_uuid)
        print("Notificaciones detenidas. Fin.")


if __name__ == "__main__":
    arguments = parse_args()
    asyncio.run(main_async(arguments))
