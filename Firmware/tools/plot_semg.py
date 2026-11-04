"""
ESP32-S 3-Channel sEMG Real-Time Visualizer & Data Logger
Receives CSV data 'ch1,ch2,ch3' from UART (COM Port) at 115200 baud.
"""

import sys
import time
import argparse
from datetime import datetime

try:
    import serial
except ImportError:
    print("Warning: 'pyserial' not installed. Run 'pip install pyserial' to read from COM port.")

def run_logger(port: str, baudrate: int = 115200, output_csv: str = None):
    if output_csv is None:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_csv = f"semg_capture_{timestamp}.csv"

    print(f"[*] Opening serial port {port} at {baudrate} baud...")
    try:
        ser = serial.Serial(port, baudrate, timeout=1.0)
    except Exception as e:
        print(f"[!] Failed to open port {port}: {e}")
        return

    print(f"[*] Streaming sEMG data. Saving to '{output_csv}'...")
    print("[*] Press Ctrl+C to stop recording.\n")

    sample_count = 0
    start_time = time.time()

    with open(output_csv, "w", encoding="utf-8") as f:
        f.write("timestamp_ms,ch1_mv,ch2_mv,ch3_mv\n")
        try:
            while True:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if not line:
                    continue

                parts = line.split(",")
                if len(parts) == 3:
                    try:
                        ch1, ch2, ch3 = float(parts[0]), float(parts[1]), float(parts[2])
                        now_ms = int((time.time() - start_time) * 1000)
                        f.write(f"{now_ms},{ch1:.2f},{ch2:.2f},{ch3:.2f}\n")
                        sample_count += 1

                        if sample_count % 500 == 0:
                            elapsed = time.time() - start_time
                            rate = sample_count / elapsed if elapsed > 0 else 0
                            print(f"[+] Samples: {sample_count:6d} | Rate: {rate:5.1f} Hz | Last: CH1={ch1:6.1f} mV, CH2={ch2:6.1f} mV, CH3={ch3:6.1f} mV")
                    except ValueError:
                        pass
        except KeyboardInterrupt:
            print("\n[*] Capture finished by user.")
        finally:
            ser.close()

    print(f"[*] Total captured samples: {sample_count}. Saved in '{output_csv}'.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ESP32-S sEMG UART Logger")
    parser.add_argument("--port", "-p", type=str, default="COM3", help="Serial port (e.g. COM3, COM4)")
    parser.add_argument("--baud", "-b", type=int, default=115200, help="Baudrate (default 115200)")
    parser.add_argument("--output", "-o", type=str, default=None, help="Output CSV filename")
    args = parser.parse_args()

    run_logger(args.port, args.baud, args.output)
