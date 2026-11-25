#!/usr/bin/env python3
"""
End-to-End sEMG Bridge (UART / File / Dataset -> 15 Hudgins Features -> MQTT / HTTP Server)

Supports:
  - Firmware v3 stream (0.1 uV integer RTI: 'ch1,ch2,ch3\\r\\n' @ 921600 baud)
  - Dataset CSV replay (ML_Model/datasets/train/*/*.csv)
  - Causal 50 Hz Notch (Q=30) + 4th-order Butterworth Bandpass (20-450 Hz)
  - Exact 15 Hudgins features matching Server/inc/semg_features.h (window=200, step=25)
  - Publishing via MQTT (semg/v1/<device_id>/features) or HTTP POST /api/simulate

Examples:
  # Live ESP32-S (real PCB or semg_adc_mock.c) over USB COM port -> HTTP Server
  python Firmware/tools/semg_bridge.py --source serial:COM3 --baud 921600 --transport http

  # Live ESP32-S -> Mosquitto MQTT Broker
  python Firmware/tools/semg_bridge.py --source serial:COM3 --baud 921600 --transport mqtt

  # Offline/Demo replay from a dataset CSV file -> HTTP Server (no ESP32 needed)
  python Firmware/tools/semg_bridge.py --source "dataset:ML_Model/datasets/train/2nd_Gesture_Fist/P1_G2_Fist_R01 .csv" --transport http --realtime
"""

import argparse
import csv
import json
import math
import sys
import time
import urllib.request
from collections import deque

import numpy as np
from scipy.signal import butter, iirnotch, lfilter, lfilter_zi

WINDOW_SIZE = 200
STEP_SIZE = 25
FS = 1000.0


def design_causal_filters(fs: float = FS):
    b_notch, a_notch = iirnotch(w0=50.0, Q=30.0, fs=fs)
    nyq = 0.5 * fs
    b_band, a_band = butter(4, [20.0 / nyq, 450.0 / nyq], btype="band")
    return (b_notch, a_notch), (b_band, a_band)


class CausalFilterBank:
    def __init__(self, fs: float = FS):
        (self.bn, self.an), (self.bb, self.ab) = design_causal_filters(fs)
        self.zi_n = [lfilter_zi(self.bn, self.an) * 0.0 for _ in range(3)]
        self.zi_b = [lfilter_zi(self.bb, self.ab) * 0.0 for _ in range(3)]

    def step(self, sample_3ch):
        out = [0.0, 0.0, 0.0]
        for ch in range(3):
            y1, self.zi_n[ch] = lfilter(self.bn, self.an, [sample_3ch[ch]], zi=self.zi_n[ch])
            y2, self.zi_b[ch] = lfilter(self.bb, self.ab, y1, zi=self.zi_b[ch])
            out[ch] = float(y2[0])
        return out


def extract_15_features(window_200x3: np.ndarray, thresh: float = 1e-5) -> list:
    """Exact match to Server/inc/semg_features.h::semg_extract_15_features."""
    features = []
    for ch in range(3):
        x = window_200x3[:, ch]
        mean = float(np.mean(x))
        mav = float(np.mean(np.abs(x)))
        rms = float(math.sqrt(float(np.mean(x * x))))
        diffs = np.diff(x)
        wl = float(np.sum(np.abs(diffs)))
        xc = x - mean
        zc = int(np.sum(((xc[:-1] * xc[1:]) < 0.0) & (np.abs(diffs) >= thresh)))
        diff1 = x[1:-1] - x[:-2]
        diff2 = x[1:-1] - x[2:]
        ssc = int(np.sum(((diff1 * diff2) > 0.0) & ((np.abs(diff1) >= thresh) | (np.abs(diff2) >= thresh))))
        features.extend([mav, rms, wl, float(zc), float(ssc)])
    return features


def iter_samples(source: str, baud: int, fw_format: str, scale: float, realtime: bool):
    if source.startswith("serial:"):
        import serial

        port = source.split(":", 1)[1]
        print(f"[BRIDGE] Opening {port} @ {baud} baud (format={fw_format})...")
        ser = serial.Serial(port, baud, timeout=1.0)
        try:
            while True:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                parts = line.split(",")
                if len(parts) != 3:
                    continue
                try:
                    raw = [float(p) for p in parts]
                except ValueError:
                    continue
                # v3 emits 0.1 uV RTI -> convert to Volts (* 1e-7), then to dataset unit (/ scale)
                if fw_format == "v3":
                    yield [(v * 1e-7) / scale for v in raw]
                else:
                    yield [(v * 1e-6) / scale for v in raw]
        finally:
            ser.close()

    elif source.startswith("dataset:"):
        path = source.split(":", 1)[1]
        print(f"[BRIDGE] Replaying dataset CSV: {path}")
        with open(path, "r", encoding="utf-8") as f:
            reader = csv.reader(f)
            header = next(reader, None)
            _ = header
            for row in reader:
                if len(row) >= 4:
                    yield [float(row[1]), float(row[2]), float(row[3])]
                    if realtime:
                        time.sleep(0.001)

    elif source.startswith("file:"):
        path = source.split(":", 1)[1]
        print(f"[BRIDGE] Reading captured CSV: {path}")
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                parts = line.strip().split(",")
                if len(parts) != 3:
                    continue
                try:
                    raw = [float(p) for p in parts]
                except ValueError:
                    continue
                factor = 1e-7 if fw_format == "v3" else 1e-6
                yield [(v * factor) / scale for v in raw]
                if realtime:
                    time.sleep(0.001)
    else:
        raise ValueError(f"Unsupported source '{source}'. Use serial:COMx, file:<path>, or dataset:<path>.")


def send_http(url: str, device_id: str, seq: int, features: list):
    payload = json.dumps({"device_id": device_id, "seq": seq, "f": features}).encode("utf-8")
    req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=2.0) as resp:
        return resp.read().decode("utf-8", errors="ignore")


def main():
    parser = argparse.ArgumentParser(description="ESP32-S sEMG v3 Bridge -> C Server")
    parser.add_argument("--source", required=True, help="serial:COM3 | file:<csv> | dataset:<csv>")
    parser.add_argument("--baud", type=int, default=921600, help="UART baudrate (default: 921600)")
    parser.add_argument("--fw-format", choices=["v3", "v1"], default="v3", help="v3=0.1uV, v1=1uV")
    parser.add_argument("--scale", type=float, default=1.0 / 113.6, help="Scale D1 in V/unit (default: 1/113.6)")
    parser.add_argument("--transport", choices=["mqtt", "http", "stdout"], default="http")
    parser.add_argument("--host", default="127.0.0.1", help="Broker/Server host")
    parser.add_argument("--port", type=int, default=0, help="Port (default: 1883 mqtt, 8080 http)")
    parser.add_argument("--device-id", default="esp32_sim", help="Device ID for server")
    parser.add_argument("--realtime", action="store_true", help="Pace file/dataset replay at ~1 kHz")
    args = parser.parse_args()

    mqtt_client = None
    if args.transport == "mqtt":
        import paho.mqtt.client as mqtt

        mqtt_port = args.port or 1883
        mqtt_client = mqtt.Client()
        mqtt_client.connect(args.host, mqtt_port, keepalive=60)
        mqtt_client.loop_start()
        print(f"[BRIDGE] Connected to MQTT {args.host}:{mqtt_port} (topic: semg/v1/{args.device_id}/features)")
    elif args.transport == "http":
        http_port = args.port or 8080
        http_url = f"http://{args.host}:{http_port}/api/simulate"
        print(f"[BRIDGE] Targeting HTTP API {http_url}")

    filt = CausalFilterBank(FS)
    win = deque(maxlen=WINDOW_SIZE)
    sample_idx = 0
    seq = 0

    try:
        for s in iter_samples(args.source, args.baud, args.fw_format, args.scale, args.realtime):
            s_filt = filt.step(s)
            win.append(s_filt)
            sample_idx += 1

            if len(win) == WINDOW_SIZE and ((sample_idx - WINDOW_SIZE) % STEP_SIZE == 0):
                seq += 1
                feats = extract_15_features(np.asarray(win, dtype=np.float64))
                if args.transport == "mqtt":
                    payload = json.dumps({"seq": seq, "f": [round(v, 7) for v in feats]})
                    mqtt_client.publish(f"semg/v1/{args.device_id}/features", payload, qos=0)
                elif args.transport == "http":
                    res = send_http(http_url, args.device_id, seq, feats)
                    if seq % 10 == 0 or seq <= 3:
                        print(f"[SEQ {seq:4d}] {res.strip()}")
                else:
                    print(json.dumps({"seq": seq, "f": [round(v, 7) for v in feats]}))
    except KeyboardInterrupt:
        print("\n[BRIDGE] Stopped by user.")
    finally:
        if mqtt_client is not None:
            mqtt_client.loop_stop()
            mqtt_client.disconnect()
        print(f"[BRIDGE] Finished. Total windows sent: {seq}")


if __name__ == "__main__":
    main()
