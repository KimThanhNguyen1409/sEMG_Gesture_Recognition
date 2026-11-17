# Hướng dẫn Thiết kế Design Rules & Kiểm tra DRC cho `EMG_Armband_3CH` (KiCad 10.0)

Tài liệu này tổng hợp kết quả nghiên cứu từ **mã nguồn thực tế của dự án** (`hardware/EMG_Armband_3CH.kicad_pro`, `hardware/EMG_Armband_3CH.kicad_pcb`) kết hợp với **tài liệu chính thức KiCad (`docs.kicad.org`)** và **năng lực gia công PCB 2 lớp tiêu chuẩn (JLCPCB / PCBWay)**, nhằm thiết lập bộ Design Rules chống chập mạch, chống đứt mạch / acid trap, và đảm bảo tính toàn vẹn tín hiệu cho mạch y sinh sEMG 3 kênh + ESP32.

---

## 1. Hiện trạng Design Rules trong Repo (`EMG_Armband_3CH`)

Qua kiểm tra trực tiếp `hardware/EMG_Armband_3CH.kicad_pro` và `hardware/EMG_Armband_3CH.kicad_pcb` (generator KiCad `10.0`, 2 lớp `F.Cu` / `B.Cu`, độ dày `1.6 mm`):

1. **Chưa có file `.kicad_dru`**: Thư mục `hardware/` hiện chưa có `EMG_Armband_3CH.kicad_dru`.
2. **Net Classes đã khai báo nhưng chưa gán net**: Trong `EMG_Armband_3CH.kicad_pro` (`net_settings.classes`) đã tạo 4 Net Class (`Default`, `Power`, `Analog_Sens`, `High_Speed`), nhưng cả `netclass_assignments` (`{}`) và `netclass_patterns` (`[]`) đều đang **trống**. Toàn bộ net hiện đang chạy theo `Default` (`clearance = 0.2 mm`, `track_width = 0.25 mm`, `via = 0.6/0.3 mm`).
3. **Thông số `board.design_settings.rules` còn lỏng cho gia công**:
   - `min_clearance = 0.0` (chưa khóa sàn clearance toàn mạch; dựa hoàn toàn vào Net Class).
   - `min_connection = 0.0`, `min_silk_clearance = 0.0`.
   - `min_through_hole_diameter = 0.2 mm` (nhỏ hơn ngưỡng khoan tiêu chuẩn `0.3 mm` của JLCPCB — dễ phát sinh phụ phí hoặc gãy mũi khoan).
   - `min_via_annular_width = 0.1 mm` và `min_via_diameter = 0.5 mm` (với lỗ `0.3 mm`, pad `0.5 mm` chỉ cho annular ring `0.1 mm`, nằm sát giới hạn tuyệt đối và dễ bị breakout khi khoan lệch; khuyến nghị tối thiểu `0.15 mm`, tức via `0.6 / 0.3 mm`).
4. **Footprint cần lưu ý đặc biệt trong `.kicad_pcb`**:
   - `Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12`: Pitch chân nhỏ (~0.5 mm), khe hở giữa các pad hẹp — dễ dính lỗi `solder_mask_bridge` (mất cầu solder mask) hoặc `clearance` nếu không tối ưu aperture / kiểm tra cầu mask.
   - `RF_Module:ESP32-WROOM-32`: Cần vùng cấm đồng (keepout) dưới ăng-ten PCB và decoupling sát chân nguồn.
   - `Package_SO:SOIC-8-1EP_3.9x4.9mm_P1.27mm_EP2.41x3.3mm`, `SOIC-14`, `SOIC-16`: Các IC analog (CCIA / Op-amp) có chân mật độ trung bình.

---

## 2. Đối chiếu Năng lực Gia công PCB 2 Lớp (JLCPCB & PCBWay)

Để **không chập mạch (bridging/short), không đứt mạch (etching break), không vỡ via (breakout)** ở gói gia công tiêu chuẩn (1 oz copper, FR-4, 2 layers):

| Thông số gia công | JLCPCB (Tiêu chuẩn 1 oz) | PCBWay (Tiêu chuẩn 1 oz) | Giá trị Khuyến nghị cho `EMG_Armband_3CH` |
| :--- | :--- | :--- | :--- |
| **Min Trace Width** | 5 mil (`0.127 mm`) | 4–6 mil (`0.10–0.15 mm`) | **≥ `0.20 mm`** (tín hiệu thường: `0.25 mm`, nguồn: `0.50–0.80 mm`) |
| **Min Trace Spacing (Clearance)** | 5 mil (`0.127 mm`) | 4–6 mil (`0.10–0.15 mm`) | **≥ `0.20 mm`** (Default), **`0.25 mm`** (Power / High_Speed), **`0.30 mm`** (Analog High-Z) |
| **Min Via Drill (PTH)** | `0.30 mm` (12 mil) | `0.20 mm` (khuyến nghị `≥ 0.30 mm`) | **≥ `0.30 mm`** (Power via: `0.40 mm`) |
| **Min Via Pad / Annular Ring** | Pad ≥ Drill + `0.20 mm`; Annular ring tuyệt đối `0.15–0.18 mm` (khuyến nghị `0.25 mm`) | Annular ring tuyệt đối `0.15 mm` (6 mil) | **Annular ring ≥ `0.15 mm`** → Via tối thiểu **`0.60 / 0.30 mm`** (Power: **`0.80 / 0.40 mm`**) |
| **Copper-to-Edge Clearance** | Tuyệt đối `0.20 mm` (V-cut `0.30–0.50 mm`), khuyến nghị `0.50 mm` | `0.30–0.50 mm` | **≥ `0.50 mm`** (giữ `min_copper_edge_clearance = 0.5 mm`) |
| **Hole-to-Hole Clearance** | `0.25–0.30 mm` (cạnh lỗ tới cạnh lỗ) | `0.25–0.30 mm` | **≥ `0.30 mm`** |
| **Solder Mask Bridge (Dam)** | Xanh lá: **`0.10 mm`** (4 mil); Đen/Trắng: **`0.13 mm`** | Xanh lá/Xanh dương/Đỏ/Tím: pad spacing `≥ 0.19 mm` | Chọn **Solder Mask Xanh lá**; bật DRC `solder_mask_bridge = error` |

---

## 3. Bảng Cấu hình Board Setup -> Constraints & Severities (KiCad 10.0)

### A. Constraints (`Board Setup -> Design Rules -> Constraints`)

| Thông số trong KiCad 10.0 | Giá trị hiện tại (`.kicad_pro`) | Giá trị Khuyến nghị | Lý do kỹ thuật |
| :--- | :--- | :--- | :--- |
| **Minimum clearance** | `0.0 mm` | **`0.20 mm`** | Chặn tuyệt đối mọi khe hở < 0.20 mm (an toàn cao hơn ngưỡng 0.127 mm của JLCPCB, chống dính đồng khi khắc). |
| **Minimum track width** | `0.20 mm` | **`0.20 mm`** | Đủ an toàn cho 1 oz đồng; không bị đứt mạch khi ăn mòn. |
| **Minimum connection width** | `0.0 mm` | **`0.20 mm`** | Tránh cổ chai đồng quá mỏng nối vào pad/zone. |
| **Minimum annular width** | `0.10 mm` | **`0.15 mm`** | Đạt chuẩn tối thiểu của JLCPCB/PCBWay, chống breakout khi khoan lệch dung sai. |
| **Minimum via diameter** | `0.50 mm` | **`0.60 mm`** | Tương ứng lỗ `0.30 mm` + `2 × 0.15 mm` annular ring. |
| **Minimum through hole** | `0.20 mm` | **`0.30 mm`** | Chuẩn khoan cơ khí không phụ phí của JLCPCB/PCBWay. |
| **Hole to hole clearance** | `0.25 mm` | **`0.30 mm`** | Tránh vỡ vách FR-4 giữa hai lỗ khoan gần nhau. |
| **Hole clearance (copper to hole)** | `0.25 mm` | **`0.25 mm`** | Giữ an toàn từ mép lỗ khoan tới đồng net khác. |
| **Copper to edge clearance** | `0.50 mm` | **`0.50 mm`** | An toàn khi phay viền / cắt V-cut (±0.2 mm dung sai cơ khí). |
| **Minimum silkscreen clearance** | `0.0 mm` | **`0.15 mm`** | Tránh mực in lụa lem lên pad hàn gây hở/chập chân SMD. |
| **Minimum text height / thickness** | `0.80 mm / 0.08 mm` | **`0.80 mm / 0.15 mm`** | Chữ dày ≥ 0.15 mm (6 mil) để nhà máy in rõ nét, không mất nét. |

### B. Violation Severities (`Board Setup -> Design Rules -> Violation Severity`)

Đề xuất nâng cấp các mục đang để `warning` hoặc `ignore` có nguy cơ gây lỗi gia công / chập mạch lên **`error`**:

| Rule Key (`.kicad_pro`) | Hiện tại | Khuyến nghị | Mục đích |
| :--- | :--- | :--- | :--- |
| `clearance`, `shorting_items`, `tracks_crossing`, `zones_intersect` | `error` | **`error`** | Bắt buộc — chống chập mạch trực tiếp. |
| `solder_mask_bridge` | `error` | **`error`** | Phát hiện hai pad khác net bị gộp chung cửa sổ solder mask (nguy cơ cầu thiếc). |
| `starved_thermal`, `unconnected_items` | `error` | **`error`** | Đảm bảo pad GND/nguồn nối đủ spoke vào zone, không hở mạch. |
| `annular_width`, `drill_out_of_range`, `track_width`, `copper_edge_clearance` | `error` | **`error`** | Đảm bảo đúng năng lực gia công nhà máy. |
| `track_angle` | `error` | **`error`** | Kích hoạt bắt lỗi góc nhọn khi kết hợp với rule `(constraint track_angle ...)` trong `.kicad_dru`. |
| `hole_to_hole` | `warning` | **`error`** | Ngăn khoan quá sát gây nứt FR-4 hoặc chạm barrel. |
| `copper_sliver` | `warning` | **`error`** | Loại bỏ mảnh đồng mảnh dễ bong tróc gây chập mạch sau khi khắc. |
| `isolated_copper` | `warning` | **`error`** | Tránh đảo đồng nổi (floating copper) gây nhiễu ký sinh cho mạch sEMG High-Z. |
| `track_dangling`, `via_dangling` | `warning` | **`error`** | Loại bỏ stub đồng dư (tạo ăng-ten thu nhiễu 50/60Hz và RF từ ESP32). |
| `connection_width` | `warning` | **`error`** | Tránh cổ chai đồng quá mỏng. |
| `silk_over_copper` | `warning` | **`warning`** (hoặc `error` cho pad) | Kiểm tra trước khi xuất Gerber; tránh silk đè lên pad hàn. |
| `courtyards_overlap` | `error` | **`error`** | Tránh cấn linh kiện khi hàn SMD. |

---

## 4. Bảng Thiết kế Net Classes cho `EMG_Armband_3CH`

Dựa trên danh sách net thực tế trích xuất từ `hardware/EMG_Armband_3CH.kicad_pcb`:

| Net Class | Priority | Clearance | Track Width | Via (Dia / Drill) | Diff Pair (W / Gap) | Danh sách Net thực tế cần gán (hoặc Pattern) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`Power`** | `0` | `0.25 mm` | `0.60 mm` (min `0.40 mm`) | `0.80 / 0.40 mm` | — | `VBUS`, `VBAT`, `+3V3`, `/VSW`, `/CH340_V3` *(lưu ý: `GND` đi qua Zone/Polygon, via stitching `0.60/0.30` hoặc `0.80/0.40`)* |
| **`Analog_Sens`** | `1` | `0.30 mm` | `0.25 mm` | `0.60 / 0.30 mm` | — | **Electrode High-Z & AFE:** `/EN1`, `/EN2`, `/EN3`, `/EP1`, `/EP2`, `/EP3`, `/CCIA_N1..3`, `/CCIA_P1..3`, `/IN_BN1..3`, `/IN_BP1..3`, `/BUFN1..3`, `/BUFP1..3`, `/S1_1..3`, `/S2_1..3`, `/STG2_FB1..3`, `/HPF_IN1..3`, `/LPF_IN1..3`, `/LPF_MID1..3`, `/DRL`, `/DRL_INT`, `/DRL_SUM`, `/VREF`, `/VREF_DIV`, `EMG_CH1..3`, `/ADC_CH1..3` |
| **`High_Speed`** | `2` | `0.25 mm` | `0.25 mm` | `0.60 / 0.30 mm` | `0.25 mm / 0.20 mm` | `USB_DP`, `USB_DN` |
| **`Default`** | (cuối) | `0.20 mm` | `0.25 mm` | `0.60 / 0.30 mm` | — | `/UART_RX`, `/UART_TX`, `/USB_DTR`, `/USB_RTS`, `/ESP_EN`, `/ESP_IO0`, `/TP_PROG`, `/CC1`, `/CC2`, `/Q1_B`, `/Q2_B`, `/ESP_STATUS_LED`, `/LED_*`, `/NET_*`, `Net-(U*)` |

> **Ghi chú gán nhanh bằng Pattern trong `Board Setup -> Net Classes -> Netclass Patterns`:**
> - `^/E[NP][1-3]$`, `^/CCIA_.*`, `^/IN_B.*`, `^/BUF.*`, `^/S[12]_.*`, `^/STG2_.*`, `^/[HL]PF_.*`, `^/DRL.*`, `^/VREF.*`, `^EMG_CH[1-3]$`, `^/ADC_CH[1-3]$` → `Analog_Sens`
> - `^USB_D[PN]$` → `High_Speed`
> - `^(VBUS|VBAT|\+3V3|/VSW|/CH340_V3)$` → `Power`

---

## 5. File `EMG_Armband_3CH.kicad_dru` Hoàn chỉnh (KiCad 10.0)

Cú pháp bên dưới tuân thủ chuẩn S-expression `(version 1)` của KiCad 8/9/10 (`docs.kicad.org`): sử dụng `A.Type == 'Track'`, `A.NetClass`, `A.NetName`, `A.inDiffPair(...)`, `edge_clearance`, `track_angle`, `hole_to_hole`, `clearance`, `track_width`, `via_diameter`, `via_drill`, `silk_clearance`.

Bạn có thể lưu nguyên nội dung sau thành `hardware/EMG_Armband_3CH.kicad_dru`, sau đó mở **Board Setup -> Design Rules -> Custom Rules** và bấm **Check Rule Syntax** để xác nhận:

```lisp
(version 1)

# ==============================================================================
# EMG_Armband_3CH - Custom Design Rules (KiCad 10.0)
# Target Fab: JLCPCB / PCBWay Standard 2-Layer (1 oz Cu, 1.6 mm FR-4)
# ==============================================================================

# 1. CHỐNG ĐỨT MẠCH / ACID TRAP (Góc nối giữa các đoạn track)
# Cấm tuyệt đối góc nhọn < 90 độ (lỗi DRC Error)
(rule "No acute track angles (< 90 deg)"
  (condition "A.Type == 'Track'")
  (constraint track_angle (min 90deg))
)

# Cảnh báo các góc vuông 90 độ (khuyến khích đi góc 45 độ = góc trong >= 135 độ)
(rule "Prefer 45 deg routing (warn < 135 deg)"
  (severity warning)
  (condition "A.Type == 'Track'")
  (constraint track_angle (min 135deg))
)

# 2. AN TOÀN GIA CÔNG CƠ BẢN (DFM Floor)
# Đảm bảo mọi lỗ khoan cách nhau tối thiểu 0.30 mm (mép-tới-mép)
(rule "DFM Hole to Hole Clearance"
  (condition "A.Net != B.Net")
  (constraint hole_to_hole (min 0.30mm))
)

# Đảm bảo đồng cách mép bo tối thiểu 0.50 mm
(rule "DFM Copper to Board Edge Clearance"
  (condition "A.Type != 'Graphic'")
  (constraint edge_clearance (min 0.50mm))
)

# 3. CÁCH LY TÍN HIỆU Y SINH sEMG SIÊU NHẠY (High-Z & AFE)
# Tách biệt Analog_Sens khỏi mọi net Digital / Power / High_Speed tối thiểu 0.35 mm
(rule "Isolate Analog_Sens from Non-Analog Nets"
  (condition "A.NetClass == 'Analog_Sens' && B.NetClass != 'Analog_Sens' && B.NetName != 'GND'")
  (constraint clearance (min 0.35mm))
)

# Các đường điện cực thô (/EN*, /EP*, /DRL*) cách nhau và cách các net khác >= 0.30 mm
(rule "Raw Electrode High-Z Clearance"
  (condition "A.NetName == '/EN1' || A.NetName == '/EN2' || A.NetName == '/EN3' || A.NetName == '/EP1' || A.NetName == '/EP2' || A.NetName == '/EP3' || A.NetName == '/DRL'")
  (constraint clearance (min 0.30mm))
)

# 4. NGUỒN (POWER) - CHỊU DÒNG & GIẢM SỤT ÁP
# Ép độ rộng đường nguồn tối thiểu 0.40 mm (khuyến nghị 0.60 mm)
(rule "Power Track Width"
  (condition "A.Type == 'Track' && A.NetClass == 'Power'")
  (constraint track_width (min 0.40mm) (opt 0.60mm))
)

# Via nguồn tối thiểu 0.80 / 0.40 mm
(rule "Power Via Size"
  (condition "A.Type == 'Via' && A.NetClass == 'Power'")
  (constraint via_diameter (min 0.80mm))
  (constraint via_drill (min 0.40mm))
)

# 5. USB FULL-SPEED DIFFERENTIAL PAIR (USB_DP / USB_DN)
# Kiểm soát độ lệch chiều dài (skew) và clearance xung quanh cặp USB
(rule "USB Differential Pair Skew"
  (condition "A.inDiffPair('USB*')")
  (constraint skew (max 1.0mm))
)

(rule "USB Differential Pair Clearance to Other Nets"
  (condition "A.inDiffPair('USB*') && !B.inDiffPair('USB*')")
  (constraint clearance (min 0.25mm))
)

# 6. SILKSCREEN AN TOÀN HÀN SMD
# Giữ silkscreen cách pad SMD tối thiểu 0.15 mm
(rule "Silkscreen to Pad Clearance"
  (condition "A.Type == 'Text' && B.Type == 'Pad'")
  (constraint silk_clearance (min 0.15mm))
)
```

> **Lưu ý khi chạy DRC lần đầu với file trên:**
> - Nếu `USB_C_Receptacle_HRO_TYPE-C-31-M-12` có pad-to-pad trong cùng footprint nhỏ hơn `0.20 mm` (hoặc gây `solder_mask_bridge`), kiểm tra thiết lập `pad_to_mask_clearance` trong footprint hoặc cho phép solder mask bridge cục bộ chỉ khi thật sự cần thiết; tuyệt đối không hạ `min_clearance` toàn mạch xuống dưới `0.15 mm`.
> - Nếu rule `"Prefer 45 deg routing"` báo warning tại các điểm chia nhánh chữ T (T-junction) hợp lệ, có thể giữ làm warning để review thủ công từng điểm hoặc bỏ rule warning đó và chỉ giữ rule error `(min 90deg)`.

---

## 6. Kiểm tra những thứ KiCad DRC KHÔNG tự bắt (Verification Checklist)

KiCad DRC kiểm tra tốt hình học và khoảng cách, nhưng **không thể tự đánh giá đầy đủ các yếu tố EMC/y sinh và layout vật lý** sau đây — cần kiểm tra thủ công hoặc qua script:

1. **Góc nhọn tại điểm tiếp xúc Track–Pad hoặc Track–Zone (Pad-entry Acid Trap)**:
   - `track_angle` chỉ kiểm tra góc giữa **hai đoạn track nối với nhau**. Nếu một track đi chéo góc 45° đâm vào cạnh vuông của pad SMD hoặc góc nhọn của copper pour, KiCad DRC có thể bỏ qua.
   - **Cách kiểm tra**: Bật chế độ hiển thị High Contrast, rà soát các pad `SOIC-8/14/16`, `R_0805`, `C_0805` và bật **Teardrops** (`Edit -> Edit Teardrops`) cho pad/via quan trọng để triệt tiêu khe nhọn cổ pad.
2. **Khoảng cách Tụ Decoupling (`C_0805`) tới chân nguồn IC**:
   - DRC không kiểm tra độ dài vòng lặp nguồn (power loop inductance) từ tụ `100 nF` tới chân `VCC/VDD` của `ESP32-WROOM-32` và các IC Op-Amp/CCIA.
   - **Cách kiểm tra**: Đo trực tiếp bằng thước (`Ctrl+Shift+M`): tụ `100 nF` phải đặt **≤ 2–3 mm** tính từ chân nguồn IC, đường về `GND` qua via stitching ngắn nhất.
3. **Vùng cấm đồng (Keepout) dưới ăng-ten `ESP32-WROOM-32` & Nhiễu RF sang AFE**:
   - DRC chỉ bắt nếu bạn đã vẽ sẵn `Rule Area (Keepout)`. Nếu chưa vẽ, vùng phủ đồng GND hoặc đường tín hiệu đi dưới ăng-ten PCB sẽ làm suy hao sóng BLE/Wi-Fi.
   - **Cách kiểm tra**: Xác nhận phần nhô ăng-ten của `ESP32-WROOM-32` thò ra khỏi mép bo hoặc nằm trên vùng Keepout cả `F.Cu` và `B.Cu`; đặt cụm AFE (`/EN*`, `/EP*`, `/CCIA_*`) ở phía đối diện xa ăng-ten nhất.
4. **Guard Ring & Ground Partitioning cho mạch sEMG**:
   - DRC không biết mặt phẳng GND của phần Digital/ESP32 có đang cắt ngang dưới đường High-Z `/EN1..3`, `/EP1..3` hay không (gây ground bounce và nhiễu 50/60 Hz).
   - **Cách kiểm tra**: Kiểm tra trực quan trên `F.Cu` và `B.Cu` xem đường High-Z có được bọc bởi Guard Ring nối `/VREF` (hoặc `AGND` sạch tùy kiến trúc AFE) và tránh đi song song gần `VBUS`, `/VSW`, `USB_DP/DN`.
5. **Kiểm tra Gerber thực tế trước khi đặt mạch (DFM Viewer)**:
   - Sau khi pass 0 Errors trên KiCad DRC, xuất Gerber + Drill (`File -> Fabrication Outputs -> Gerbers`) và mở bằng **GerbView** hoặc upload lên **JLCPCB Gerber Viewer** để kiểm tra trực quan solder mask bridge quanh `USB_C_Receptacle_HRO_TYPE-C-31-M-12` và các pad `SOIC`.

---

## Nguồn tham khảo chính thống

1. **Repo nguồn dự án**:
   - [EMG_Armband_3CH.kicad_pro](file:///c:/Users/Admin/Documents/IoT/hardware/EMG_Armband_3CH.kicad_pro) (`board.design_settings`, `net_settings`)
   - [EMG_Armband_3CH.kicad_pcb](file:///c:/Users/Admin/Documents/IoT/hardware/EMG_Armband_3CH.kicad_pcb) (danh sách net thực tế, footprints, setup)
2. **KiCad Official Documentation (`docs.kicad.org`)**:
   - Pcbnew — *Design Rules & Custom Rules Syntax* (`.kicad_dru`, S-expression constraints `clearance`, `track_width`, `track_angle`, `edge_clearance`, `hole_to_hole`, `silk_clearance`, `skew`, `inDiffPair`, `NetClass`, `NetName`).
   - Pcbnew — *Design Rule Checker (DRC)* (`solder_mask_bridge`, `copper_sliver`, `starved_thermal`, `track_dangling`, `track_angle`).
3. **Năng lực gia công PCB**:
   - JLCPCB PCB Manufacturing Capabilities (Standard 2-Layer 1 oz: min trace/space 5 mil / 0.127 mm, min drill 0.30 mm, min annular ring 0.15–0.18 mm, green solder mask bridge 0.10 mm, copper-to-edge 0.20 mm min / 0.50 mm rec).
   - PCBWay Standard PCB Capabilities (2-Layer: min trace/space 4–6 mil, min drill 0.20–0.30 mm, min annular ring 0.15 mm, green solder mask pad spacing ≥ 0.19 mm).
