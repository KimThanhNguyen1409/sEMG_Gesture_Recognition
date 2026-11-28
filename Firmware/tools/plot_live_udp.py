#!/usr/bin/env python3
"""
Real-time 3-Channel Wi-Fi UDP Waveform Viewer for ESP32-S SIMULATE Mode.

Usage:
  python Firmware/tools/plot_live_udp.py --port 5005

Displays a smooth 3-second rolling window for CH1 (GPIO 34), CH2 (GPIO 35), CH3 (GPIO 32),
auto-detecting Sub-mode 0 (#M0: RAW mV) vs Sub-mode 1 (#M1: 0.1 uV RTI) and showing
live DC average + Peak-to-Peak (Vpp) per channel.
"""

import argparse
import socket
import threading
from collections import deque

import matplotlib.animation as animation
import matplotlib.pyplot as plt
import numpy as np


def main():
    parser = argparse.ArgumentParser(description="ESP32-S Wi-Fi UDP 3-CH sEMG Live Plotter")
    parser.add_argument("--port", type=int, default=5005, help="UDP listen port (default: 5005)")
    parser.add_argument("--window", type=int, default=3000, help="Samples to display @ 1 kHz (default: 3000 = 3s)")
    args = parser.parse_args()

    buf = [deque([0.0] * args.window, maxlen=args.window) for _ in range(3)]
    state = {"mode": 0, "packets": 0, "sender": "waiting...", "running": True}
    lock = threading.Lock()

    def udp_reader():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("0.0.0.0", args.port))
        sock.settimeout(0.5)
        print(f"[PLOT] Listening on UDP 0.0.0.0:{args.port} ...")
        try:
            while state["running"]:
                try:
                    data, addr = sock.recvfrom(4096)
                except socket.timeout:
                    continue
                text = data.decode("utf-8", errors="ignore")
                with lock:
                    state["packets"] += 1
                    state["sender"] = f"{addr[0]}:{addr[1]}"
                    for raw_line in text.splitlines():
                        line = raw_line.strip()
                        if line == "#M0":
                            state["mode"] = 0
                            continue
                        if line == "#M1":
                            state["mode"] = 1
                            continue
                        parts = line.split(",")
                        if len(parts) == 3:
                            try:
                                v0, v1, v2 = float(parts[0]), float(parts[1]), float(parts[2])
                                if state["mode"] == 1:
                                    # Convert 0.1 uV -> uV
                                    v0 *= 0.1
                                    v1 *= 0.1
                                    v2 *= 0.1
                                buf[0].append(v0)
                                buf[1].append(v1)
                                buf[2].append(v2)
                            except ValueError:
                                pass
        finally:
            sock.close()

    t = threading.Thread(target=udp_reader, daemon=True)
    t.start()

    fig, axes = plt.subplots(3, 1, sharex=True, figsize=(11, 7))
    fig.canvas.manager.set_window_title("ESP32-S Wi-Fi UDP Live sEMG Scope")
    colors = ["#2563eb", "#16a34a", "#dc2626"]
    names = ["CH1 (GPIO34 / ADC1_6)", "CH2 (GPIO35 / ADC1_7)", "CH3 (GPIO32 / ADC1_4)"]
    x = np.arange(args.window) / 1000.0
    lines = []
    for i, ax in enumerate(axes):
        (ln,) = ax.plot(x, np.zeros(args.window), color=colors[i], lw=1.0)
        lines.append(ln)
        ax.set_ylabel(names[i], fontsize=9)
        ax.grid(True, alpha=0.3)
    axes[-1].set_xlabel("Time (seconds, rolling window)")

    def update(_frame):
        with lock:
            mode = state["mode"]
            pkts = state["packets"]
            sender = state["sender"]
            arrs = [np.asarray(buf[i], dtype=np.float64) for i in range(3)]

        unit = "uV" if mode == 1 else "mV"
        mode_str = "Sub-mode 1: AI_LIVE_V3 (uV RTI)" if mode == 1 else "Sub-mode 0: CHECK_RAW_MV (0..3300 mV)"
        fig.suptitle(f"ESP32-S Wi-Fi UDP Scope | {mode_str} | Source: {sender} | Packets: {pkts}", fontsize=11)

        for i, ax in enumerate(axes):
            y = arrs[i]
            lines[i].set_ydata(y)
            dc = float(np.mean(y))
            vpp = float(np.max(y) - np.min(y))
            ax.set_title(f"{names[i]}   DC={dc:.1f} {unit}   Vpp={vpp:.1f} {unit}", loc="left", fontsize=9)
            pad = max(20.0, vpp * 0.2)
            ax.set_ylim(float(np.min(y)) - pad, float(np.max(y)) + pad)
        return lines

    _ani = animation.FuncAnimation(fig, update, interval=50, blit=False, cache_frame_data=False)
    try:
        plt.tight_layout()
        plt.show()
    finally:
        state["running"] = False


if __name__ == "__main__":
    main()
