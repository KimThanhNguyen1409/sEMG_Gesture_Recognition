#!/usr/bin/env python3
"""
sEMG ESP32-S Wi-Fi Provisioning & Control Center (USB -> NVS + 1-Click Launchers).

Usage:
  python Firmware/tools/semg_launcher.py

Features:
  - Auto-detects PC LAN IPv4 address & available COM ports
  - Pushes Wi-Fi SSID, Password, Target PC IP & UDP Port to ESP32-S over USB (saved in NVS)
  - Reads live Wi-Fi connection status & ESP32 IP back from ESP32-S
  - 1-click buttons: Open Windows Firewall UDP 5005, Start C Server (:8080),
    Start Wi-Fi UDP Bridge -> AI, and Open Live 3-CH Waveform Scope
"""

import ctypes
import os
import socket
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_SSID = "Nha Tre 2.4G"
DEFAULT_PASS = "01684306403"
DEFAULT_PORT = "5005"
BAUDRATE = 921600


def detect_lan_ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "192.168.1.13"


def list_com_ports() -> list:
    if serial is None:
        return ["COM10"]
    ports = [p.device for p in serial.tools.list_ports.comports()]
    return ports if ports else ["COM10"]


class SemgLauncherApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("ESP32-S sEMG Wi-Fi Provisioning & Control Center")
        self.geometry("620x540")
        self.resizable(False, False)

        self.com_var = tk.StringVar()
        self.ssid_var = tk.StringVar(value=DEFAULT_SSID)
        self.pass_var = tk.StringVar(value=DEFAULT_PASS)
        self.ip_var = tk.StringVar(value=detect_lan_ip())
        self.port_var = tk.StringVar(value=DEFAULT_PORT)
        self.show_pass_var = tk.BooleanVar(value=False)

        self._build_ui()
        self._refresh_ports()
        self._log(f"Ready. Detected PC LAN IP: {self.ip_var.get()}")

    def _build_ui(self):
        pad = {"padx": 8, "pady": 4}

        # --- Section 1: Wi-Fi Provisioning via USB ---
        frm_wifi = ttk.LabelFrame(self, text="1. Cau hinh Wi-Fi cho ESP32-S (qua USB -> luu NVS Flash)")
        frm_wifi.pack(fill="x", padx=12, pady=8)

        ttk.Label(frm_wifi, text="Cong COM:").grid(row=0, column=0, sticky="w", **pad)
        self.com_combo = ttk.Combobox(frm_wifi, textvariable=self.com_var, width=18, state="readonly")
        self.com_combo.grid(row=0, column=1, sticky="w", **pad)
        ttk.Button(frm_wifi, text="Lam moi COM", command=self._refresh_ports).grid(row=0, column=2, sticky="w", **pad)

        ttk.Label(frm_wifi, text="Wi-Fi SSID (2.4G):").grid(row=1, column=0, sticky="w", **pad)
        self.ssid_combo = ttk.Combobox(frm_wifi, textvariable=self.ssid_var, width=28)
        self.ssid_combo.grid(row=1, column=1, sticky="ew", **pad)
        ttk.Button(frm_wifi, text="Quet Wi-Fi 2.4G", command=self._scan_wifi).grid(row=1, column=2, sticky="w", **pad)

        ttk.Label(frm_wifi, text="Mat khau Wi-Fi:").grid(row=2, column=0, sticky="w", **pad)
        self.pass_entry = ttk.Entry(frm_wifi, textvariable=self.pass_var, width=30, show="*")
        self.pass_entry.grid(row=2, column=1, sticky="ew", **pad)
        ttk.Checkbutton(frm_wifi, text="Hien MK", variable=self.show_pass_var, command=self._toggle_pass).grid(
            row=2, column=2, sticky="w", **pad
        )

        ttk.Label(frm_wifi, text="IP may tinh (PC):").grid(row=3, column=0, sticky="w", **pad)
        ttk.Entry(frm_wifi, textvariable=self.ip_var, width=18).grid(row=3, column=1, sticky="w", **pad)
        ttk.Button(frm_wifi, text="Tu dong lay IP", command=self._refresh_ip).grid(row=3, column=2, sticky="w", **pad)

        ttk.Label(frm_wifi, text="UDP Port:").grid(row=4, column=0, sticky="w", **pad)
        ttk.Entry(frm_wifi, textvariable=self.port_var, width=10).grid(row=4, column=1, sticky="w", **pad)

        btn_row = ttk.Frame(frm_wifi)
        btn_row.grid(row=5, column=0, columnspan=3, pady=8)
        ttk.Button(btn_row, text="Nap Wi-Fi vao ESP32 & Reboot", command=self._send_wifi_config).pack(side="left", padx=6)
        ttk.Button(btn_row, text="Kiem tra trang thai ESP32", command=self._query_wifi_status).pack(side="left", padx=6)

        # --- Section 2: 1-Click Wi-Fi Runtime Launchers ---
        frm_run = ttk.LabelFrame(self, text="2. Khoi chay he thong qua Wi-Fi (sau khi rut cap USB)")
        frm_run.pack(fill="x", padx=12, pady=6)

        r1 = ttk.Frame(frm_run)
        r1.pack(fill="x", padx=6, pady=4)
        ttk.Button(r1, text="Mo Firewall UDP 5005 (Admin)", command=self._open_firewall).pack(side="left", padx=4, expand=True, fill="x")
        ttk.Button(r1, text="1. Bat C Server (:8080)", command=self._start_server).pack(side="left", padx=4, expand=True, fill="x")

        r2 = ttk.Frame(frm_run)
        r2.pack(fill="x", padx=6, pady=4)
        ttk.Button(r2, text="2. Bat Bridge UDP -> AI", command=self._start_bridge).pack(side="left", padx=4, expand=True, fill="x")
        ttk.Button(r2, text="Mo Do thi song Live (UDP)", command=self._start_scope).pack(side="left", padx=4, expand=True, fill="x")

        # --- Section 3: Status Log ---
        frm_log = ttk.LabelFrame(self, text="Log trang thai")
        frm_log.pack(fill="both", expand=True, padx=12, pady=6)
        self.log_text = tk.Text(frm_log, height=10, state="disabled", font=("Consolas", 9))
        self.log_text.pack(fill="both", expand=True, padx=4, pady=4)

    def _toggle_pass(self):
        self.pass_entry.config(show="" if self.show_pass_var.get() else "*")

    def _refresh_ports(self):
        ports = list_com_ports()
        self.com_combo["values"] = ports
        if ports:
            self.com_var.set(ports[0])

    def _refresh_ip(self):
        ip = detect_lan_ip()
        self.ip_var.set(ip)
        self._log(f"Updated PC LAN IP: {ip}")

    def _log(self, msg: str):
        ts = time.strftime("%H:%M:%S")
        def _append():
            self.log_text.config(state="normal")
            self.log_text.insert("end", f"[{ts}] {msg}\n")
            self.log_text.see("end")
            self.log_text.config(state="disabled")
        self.after(0, _append)

    def _send_wifi_config(self):
        if serial is None:
            messagebox.showerror("Thieu pyserial", "Hay cai pyserial: pip install pyserial")
            return
        port = self.com_var.get().strip()
        ssid = self.ssid_var.get().strip()
        pwd = self.pass_var.get()
        ip = self.ip_var.get().strip()
        udp_port = self.port_var.get().strip()
        if not port or not ssid or not ip or not udp_port.isdigit():
            messagebox.showwarning("Thieu thong tin", "Vui long nhap day du COM, SSID, IP PC va UDP Port.")
            return

        def worker():
            self._log(f"Opening {port} @ {BAUDRATE} to provision SSID='{ssid}' -> {ip}:{udp_port}...")
            try:
                with serial.Serial(port, BAUDRATE, timeout=0.5) as ser:
                    ser.reset_input_buffer()
                    cmd = f"WIFI_SET|{ssid}|{pwd}|{ip}|{udp_port}\n".encode("utf-8")
                    ser.write(cmd)
                    ser.flush()

                    ack_ok = False
                    deadline = time.time() + 10.0
                    while time.time() < deadline:
                        line = ser.readline().decode("utf-8", errors="ignore").strip()
                        if not line:
                            continue
                        if line.startswith("WIFI_ACK|OK"):
                            self._log(f"[OK] {line} -> ESP32 dang reboot ket noi Wi-Fi...")
                            ack_ok = True
                        elif line.startswith("WIFI_ACK|ERR"):
                            self._log(f"[ERR] {line}")
                            return
                        elif "Wi-Fi Connected!" in line or "SEMG_WIFI_UDP" in line:
                            self._log(f"[ESP32] {line}")
                            if "Wi-Fi Connected!" in line:
                                self._log("[DONE] ESP32 da vao Wi-Fi thanh cong! Co the rut cap USB.")
                                return
                    if not ack_ok:
                        self._log("[WARN] Khong nhan duoc WIFI_ACK. Hay kiem tra da nap firmware moi chua va dong monitor/Arduino IDE.")
            except Exception as exc:
                self._log(f"[ERROR] Loi mo cong {port}: {exc}")

        threading.Thread(target=worker, daemon=True).start()

    def _scan_wifi(self):
        if serial is None:
            messagebox.showerror("Thieu pyserial", "Hay cai pyserial: pip install pyserial")
            return
        port = self.com_var.get().strip()

        def worker():
            self._log(f"Scanning 2.4G Wi-Fi networks via ESP32 on {port} (~2-3s)...")
            found = []
            try:
                with serial.Serial(port, BAUDRATE, timeout=0.5) as ser:
                    ser.reset_input_buffer()
                    ser.write(b"WIFI_SCAN\n")
                    ser.flush()
                    deadline = time.time() + 8.0
                    while time.time() < deadline:
                        line = ser.readline().decode("utf-8", errors="ignore").strip()
                        if line.startswith("WIFI_AP|"):
                            parts = line.split("|")
                            if len(parts) >= 3 and parts[1]:
                                ssid_name = parts[1]
                                rssi = parts[2]
                                if ssid_name not in found:
                                    found.append(ssid_name)
                                self._log(f"  [AP] '{ssid_name}' (RSSI={rssi} dBm)")
                        elif line == "WIFI_SCAN_DONE":
                            break
                if found:
                    def _update_combo():
                        self.ssid_combo["values"] = found
                        self.ssid_var.set(found[0])
                    self.after(0, _update_combo)
                    self._log(f"[SCAN DONE] Tim thay {len(found)} mang 2.4G. Hay chon dung ten mang trong o SSID!")
                else:
                    self._log("[WARN] Khong tim thay mang 2.4G nao (hoac chua nap firmware moi).")
            except Exception as exc:
                self._log(f"[ERROR] Loi quet Wi-Fi tren {port}: {exc}")

        threading.Thread(target=worker, daemon=True).start()

    def _query_wifi_status(self):
        if serial is None:
            messagebox.showerror("Thieu pyserial", "Hay cai pyserial: pip install pyserial")
            return
        port = self.com_var.get().strip()

        def worker():
            self._log(f"Querying ESP32 Wi-Fi status on {port}...")
            try:
                with serial.Serial(port, BAUDRATE, timeout=0.5) as ser:
                    ser.reset_input_buffer()
                    ser.write(b"WIFI_GET\n")
                    ser.flush()
                    deadline = time.time() + 3.0
                    while time.time() < deadline:
                        line = ser.readline().decode("utf-8", errors="ignore").strip()
                        if line.startswith("WIFI_CFG|"):
                            parts = line.split("|")
                            if len(parts) >= 6:
                                status = "CONNECTED" if parts[4] == "1" else "DISCONNECTED"
                                self._log(f"[STATUS] SSID='{parts[1]}' | Target={parts[2]}:{parts[3]} | {status} | ESP32 IP={parts[5]}")
                            else:
                                self._log(f"[STATUS] {line}")
                            return
                    self._log("[WARN] Khong nhan duoc WIFI_CFG (dong monitor cu neu dang mo).")
            except Exception as exc:
                self._log(f"[ERROR] Loi mo cong {port}: {exc}")

        threading.Thread(target=worker, daemon=True).start()

    def _open_firewall(self):
        udp_port = self.port_var.get().strip() or "5005"
        args = (
            f'advfirewall firewall add rule name="ESP32 sEMG UDP {udp_port}" '
            f'dir=in action=allow protocol=UDP localport={udp_port}'
        )
        try:
            ctypes.windll.shell32.ShellExecuteW(None, "runas", "netsh", args, None, 1)
            self._log(f"Da gui yeu cau mo Windows Firewall cho UDP port {udp_port} (xac nhan UAC neu hien).")
        except Exception as exc:
            self._log(f"[ERROR] Khong the mo Firewall tu dong: {exc}")

    def _start_server(self):
        bat = os.path.join(ROOT_DIR, "Server", "run_server.bat")
        subprocess.Popen(
            ["cmd.exe", "/k", bat],
            cwd=os.path.join(ROOT_DIR, "Server"),
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )
        self._log("Da mo C Server (Web UI: http://localhost:8080).")

    def _start_bridge(self):
        udp_port = self.port_var.get().strip() or "5005"
        script = os.path.join(ROOT_DIR, "Firmware", "tools", "semg_bridge.py")
        subprocess.Popen(
            ["cmd.exe", "/k", sys.executable, script, "--source", f"udp:{udp_port}", "--transport", "http"],
            cwd=ROOT_DIR,
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )
        self._log(f"Da mo UDP Bridge lang nghe port {udp_port} -> HTTP Server :8080.")

    def _start_scope(self):
        udp_port = self.port_var.get().strip() or "5005"
        script = os.path.join(ROOT_DIR, "Firmware", "tools", "plot_live_udp.py")
        subprocess.Popen(
            ["cmd.exe", "/k", sys.executable, script, "--port", udp_port],
            cwd=ROOT_DIR,
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )
        self._log(f"Da mo Do thi song Live tren UDP port {udp_port} (luu y: dong Bridge khi mo Scope).")


if __name__ == "__main__":
    app = SemgLauncherApp()
    app.mainloop()
