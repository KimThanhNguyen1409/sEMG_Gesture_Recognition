# AFE EMG 3 kênh (Buffer + CCIA + DRL) – nguồn đơn 3.3 V

- `AFE_EMG_3ch.asc`: sơ đồ đầy đủ 3 kênh + nguồn/VREF + DRL (LTspice).
- `gen_asc3.py`: sinh lại `AFE_EMG_3ch.asc` (sửa linh kiện ở đây rồi chạy lại).
- `AFE_verify_MC.cir`: kiểm chứng thực tế 1 kênh (trở kháng da, sai số linh kiện Monte Carlo 200 lần). Kết quả `.meas` xem ở View → SPICE Error Log.

## Chuỗi tín hiệu (mỗi kênh)
| Tầng | Mạch | Linh kiện | Thông số |
|---|---|---|---|
| Vào | Bảo vệ + phân cực | R_IN = 150 kΩ, R_B = 10 MΩ → VREF | hạn dòng sự cố qua người ≤ 25 µA (biên ≥ 30 % tới 50 µA); giữ DC khi tuột điện cực |
| Buffer | 2 op-amp follower (E+, E−) | AD8605 | trở kháng vào ~TΩ → trở kháng da không còn ảnh hưởng |
| 1 | CCIA ghép tụ | C_IN = 100 nF, C_F = 2.2 nF, R_F = 22 MΩ | G = 45, chặn DC offset điện cực, HPF 3.3 Hz |
| 2 | HPF + khuếch đại không đảo | C_HP = 100 nF, R_HP = 82 kΩ, R_F = 15 kΩ, R_G = 10 kΩ | fc = 19.4 Hz, G = 2.5 |
| 3 | Sallen-Key Butterworth LPF | R_L = 22 kΩ ×2, C_L1 = 22 nF, C_L2 = 10 nF | fc ≈ 490 Hz (chống aliasing) |
| DRL (chung) | Trung bình 6 buffer → tích phân đảo → điện cực REF | R_A = 100 kΩ ×6, C_DRL = 1 nF ∥ 10 MΩ, R_DRLO = 390 kΩ | ép nhiễu 50 Hz trên cơ thể ~45 dB |

## Kiểm chứng
Bộ test tự động: `python3 tests/run_tests.py` → `tests/REPORT.md`. Điều kiện thử xấu hơn thực tế ×1.4 và mỗi chỉ tiêu phải còn biên ≥ 30 %.

## Firmware bắt buộc: notch 50 Hz
Cáp shield vẫn để lọt ~10 mVpp 50 Hz ở OUT (điện cực ướt, điều kiện ×1.4) – lớn hơn EMG nhỏ nhất. MCU phải lọc:
2 tầng biquad notch 50 Hz, Q = 5, nối tiếp (fs ≥ 1 kHz). Suy hao ≥ 50 dB kể cả khi lưới lệch ±0.28 Hz; EMG ở 40/60 Hz chỉ mất ~1.6 dB.
Dùng điện cực gel Ag/AgCl (ướt); điện cực khô làm nhiễu 50 Hz lớn gấp ~25 lần.

## Kết quả mô phỏng ban đầu (LTspice 26.1.1, R_IN 10 kΩ – xem `tests/REPORT.md` cho bản mới nhất)
| Kiểm tra | Kết quả | Yêu cầu |
|---|---|---|
| Hệ số khuếch đại (có trở kháng da) | 41.1 dB (≈113) | ~40 dB |
| Băng thông −3 dB | 19.8 – 511 Hz | 10–500 Hz (EMG) |
| CMRR 50 Hz, Monte Carlo 200 lần (sai số 1 %, da lệch ±20 %) | xấu nhất 101.6 dB, trung vị 109.9 dB | > 100 dB |
| Nhiễu quy về đầu vào (20–500 Hz) | 0.66 µVrms | ≪ 50 µV |
| 0.2 V nhiễu 50 Hz → OUT (trường hợp xấu nhất) | 0.37 mVpp | ≪ EMG 50 µV → 11 mVpp |
| Tran: vào 2 mVpp @150 Hz kèm 0.2 V CM + 0.3 V DC offset | OUT1 = 0.226 Vpp quanh 1.650 V; BODY chỉ còn 2.2 mVpp 50 Hz | |

## BOM cho 3 kênh (để in PCB)
| Linh kiện | Giá trị | SL | Footprint | Ghi chú |
|---|---|---|---|---|
| Op-amp | AD8608ARZ (4× AD8605) | 5 | SOIC-14 | 17 op-amp: 6 buffer, 9 tín hiệu, VREF, DRL; dư 3 |
| C_IN | 100 nF C0G/NP0 **1 %** | 6 | 1206 | khớp nhau → CMRR |
| C_F | 2.2 nF C0G/NP0 **1 %** | 6 | 0805 | |
| R_F (CCIA) | 22 MΩ 1 % | 6 | 0805 | |
| R_IN / R_B | 150 kΩ / 10 MΩ 1 % | 6 + 6 | 0805 | R_IN là điện trở an toàn, không giảm |
| C_HP / R_HP | 100 nF X7R / 82 kΩ | 3 + 3 | 0805 | |
| R_F / R_G tầng 2 | 15 kΩ / 10 kΩ 1 % | 3 + 3 | 0805 | |
| R_L | 22 kΩ 1 % | 6 | 0805 | |
| C_L1 / C_L2 | 22 nF / 10 nF C0G | 3 + 3 | 0805 | |
| DRL | 100 kΩ ×6, 10 MΩ, 1 nF C0G, 390 kΩ | 1 bộ | 0805 | |
| VREF | 100 kΩ ×2, C_REF 10 µF, C_VREF 10 µF | 1 bộ | 0805 | C_REF 10 µF để chặn gợn nguồn |
| Decoupling | 100 nF sát mỗi IC + 10 µF bulk | 5 + 1 | 0805 | |
| Nguồn | Pin Li-ion + LDO 3.3 V (vd. MCP1700-3302) | 1 | SOT-23 | |
| Đầu nối điện cực | Header 2.54 mm có chân shield (vd. 3 chân/kênh: E+, E−, SHIELD) | 3 + 1 | – | (E+, E−, shield)×3 + REF(DRL) |
| **Cáp điện cực** | **Cáp có lớp bọc chống nhiễu (shielded), shield nối VREF** | 7 lõi | – | **bắt buộc**: dây trần làm 50 Hz lọt ra ~0.5 Vpp (test T06U) |

## Lưu ý khi làm PCB
- **An toàn:** chỉ chạy bằng pin khi gắn điện cực lên người; không cắm USB/sạc từ máy tính nối lưới trong lúc đo. R_DRLO 390 kΩ và R_IN 100 kΩ là điện trở hạn dòng an toàn, không bỏ.
- LTspice không xuất Gerber: vẽ lại trong KiCad theo BOM → layout → Gerber.
- Layout: đổ GND liền mặt dưới; buffer đặt sát đầu nối điện cực; đường E+/E− cùng kênh song song, đối xứng; C_IN/C_F sát op-amp; tách khu analog khỏi MCU.
- Shield cáp nối VREF ở phía mạch, đầu phía điện cực để hở (không nối 2 đầu).
- PCB lần 1: chừa test point ở BUFN/BUFP, S1, S2, OUT, DRL và jumper để tắt DRL (nối REF thẳng VREF qua 10 kΩ) khi debug.
- Trước khi in: lắp thử 1 kênh trên breadboard, so với bảng kết quả ở trên.
- Mở trên Linux: `flatpak run --filesystem=home --env=WINEPREFIX=$HOME/EDA/ltspice/prefix org.winehq.Wine "C:\\Program Files\\ADI\\LTspice\\LTspice.exe" "Z:$PWD/AFE_EMG_3ch.asc"`
