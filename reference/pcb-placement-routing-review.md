# Báo cáo Kiểm tra Phần cứng PCB — Commit `6bedf2e`

- **Repository**: `c:\Users\Admin\Documents\IoT`
- **Commit kiểm tra**: `6bedf2e` (`feat(pcb): complete 113-component layout placement and pass DRC checks`)
- **Commit đối chiếu**: `df6332c` (`feat(hardware): complete 3-channel sEMG AFE design with MCP6004 and ESP32-S3`)
- **Primary sources đã kiểm tra trực tiếp**:
  - `git diff df6332c..6bedf2e`
  - `hardware/EMG_Armband_3CH.kicad_pcb`
  - `hardware/EMG_Armband_3CH.kicad_pro`
  - `hardware/AFE_3CH_AD8232.kicad_sch`
  - Kết quả chạy trực tiếp `kicad-cli.exe pcb drc` (KiCad 10.0) trên `hardware/EMG_Armband_3CH.kicad_pcb`

---

## Tóm tắt Điều hành (Executive Summary)

1. **Board chưa hề được đi dây (Unrouted 100%)**: File `hardware/EMG_Armband_3CH.kicad_pcb` ở commit `6bedf2e` mới chỉ thực hiện **Placement (đặt 113 linh kiện)**, vẽ viền `Edge.Cuts` và thêm 7 dòng text phân khu trên `F.SilkS`. Số lượng `segment = 0`, `arc = 0`, `via = 0`, `zone = 0`.
2. **Trái với mô tả commit `"pass DRC checks"`**, khi chạy `kicad-cli.exe pcb drc` trên `hardware/EMG_Armband_3CH.kicad_pcb`, kết quả ghi nhận:
   - **248 `unconnected_items`** (chưa có bất kỳ đường mạch/zone nào).
   - **4 lỗi (`error`) `copper_edge_clearance`** (tại `U3` Pad 1, `U3` Pad 40 và `J1` hai pad shield `SH`).
   - **3 cảnh báo (`warning`) `silk_edge_clearance`** (silkscreen của `U3` và `J1` bị cắt bởi `Edge.Cuts`).
   - **113 cảnh báo `lib_footprint_issues`** (môi trường CLI không có sẵn thư viện footprint mặc định).
3. **Schematic không có bất kỳ thay đổi nào**: `git diff df6332c..6bedf2e` xác nhận các file `*.kicad_sch` giữ nguyên 100%. Lỗi op-amp `U8A` nối thẳng tụ `C61 = 10 uF` không có điện trở cách ly `R_iso` vẫn còn nguyên trong `hardware/AFE_3CH_AD8232.kicad_sch`.
4. **Các điểm nghẽn Placement nghiêm trọng cần sửa trước khi route**:
   - **Tụ decoupling `C10` (10 µF) và `C11` (0.1 µF) của ESP32-S3 (`U3`) đặt cách chân `3V3` (Pad 2) tới 31.13 mm và 34.60 mm** (nằm tít dưới khu vực `U2`/`U7`).
   - **Thiếu hoàn toàn Silkscreen ghi chú tên chân (pinout/polarity/function)** cho `J3` (8 chân điện cực), `J2` (`+/-` pin), `SW1` (`ON/OFF`), `SW2`/`SW3` (`RST/BOOT`), `D1..D4`.
   - **Vi phạm `copper_edge_clearance` (< 0.50 mm)** tại `U3` (`0.44 mm`) và `J1` (`0.15 mm`), và chồng lấn courtyard nhẹ giữa `R14` và `U3` (`2.68 x 0.05 mm`).

---

## 1. Kiểm tra Thay đổi giữa `df6332c` và `6bedf2e`

Kết quả `git diff --stat df6332c..6bedf2e`:

| File | Thay đổi | Chi tiết |
| :--- | :--- | :--- |
| `.gitignore` | `+4` dòng | Thêm `.history/`, `TH/`, `*.lck` |
| `hardware/EMG_Armband_3CH.kicad_pro` | `+1 / -1` dòng | Dòng 155: `"min_through_hole_diameter": 0.3` → `0.2` |
| `hardware/EMG_Armband_3CH.kicad_pcb` | `+4794 / -849` dòng | Đặt vị trí 113 linh kiện, vẽ `Edge.Cuts`, thêm 7 `gr_text` |
| `hardware/EMG_Armband_3CH-PCB.svg` | `+33875` dòng (file mới) | Xuất file SVG của PCB |
| `hardware/*.kicad_sch` | **0 thay đổi** | Không sửa bất kỳ file schematic nào |

### Kiểm tra tồn đọng Schematic (`U8A` & `C61`)
Trong `hardware/AFE_3CH_AD8232.kicad_sch` (dòng 1228–1262):
- `U8` unit 1 (`U8A`, `MCP6004`, tại `(121.92, 45.72)`) cấu hình unity-gain buffer từ `VREF_DIV` sang net `VREF`.
- Tụ `C61` (`10uF`, tại `(144.78, 45.72)`) vẫn nối **trực tiếp** giữa `VREF` và `GND`, **không có điện trở cách ly `R_iso`** giữa ngõ ra `U8A` và `C61`. Vấn đề mất ổn định pha (phase margin / oscillation) trên `MCP6004` khi kéo tải dung lớn `10 µF` vẫn chưa được xử lý.

---

## 2. Phân tích Placement (`hardware/EMG_Armband_3CH.kicad_pcb`)

### 2.1. Thông số chung & Phân khu chức năng
- **Kích thước board (`Edge.Cuts`)**: Hình chữ nhật tạo bởi 4 đoạn `gr_line`, tọa độ `X = [42.00, 150.00]` (**108.00 mm**), `Y = [40.00, 110.00]` (**70.00 mm**).
- **Số lớp (Copper Layers)**: 2 lớp (`F.Cu`, `B.Cu`).
- **Tổng số linh kiện**: **113 footprints**, toàn bộ 113/113 đặt trên lớp **`F.Cu`** (`B.Cu`: 0 linh kiện).
- **Phân bố các khối chức năng**:
  - **Khối Điện cực & AFE (nửa trái board, `X = 47.50 .. 110.00`)**:
    - `J3` (`PinHeader_1x08_P2.54mm_Vertical`): tại `(47.50, 65.00)`, `rot = 0°` (8 pad kéo dài `Y = 65.00 .. 82.78`).
    - `U8` (`MCP6004` - VREF/DRL): tại `(68.50, 49.00)`.
    - `U4` (`MCP6004` - CH1): tại `(68.50, 64.00)`.
    - `U5` (`MCP6004` - CH2): tại `(68.50, 80.00)`.
    - `U6` (`MCP6004` - CH3): tại `(68.50, 96.00)`.
    - `U7` (`MCP6004` - Post-filter/Gain): tại `(110.00, 80.00)`.
  - **Khối MCU & RF (góc trên-phải, `X ≈ 124 .. 142`, `Y ≈ 40 .. 80`)**:
    - `U3` (`ESP32-S3-WROOM-1`): tại `(133.00, 46.15)`, `rot = 0°`.
    - `SW2` (`RESET`): tại `(133.50, 74.00)`.
    - `SW3` (`BOOT`): tại `(133.50, 79.50)`.
    - `D4` (`LED_STATUS`): tại `(142.00, 76.50)`.
  - **Khối Nguồn & USB (góc dưới-phải, `X ≈ 118 .. 146`, `Y ≈ 85 .. 110`)**:
    - `U2` (`AP2112K-3.3`): tại `(120.00, 87.00)`.
    - `U1` (`TP4056`): tại `(134.50, 88.00)`.
    - `D1` (`RED_CHRG`) tại `(139.50, 93.00)`, `D2` (`GREEN_STDBY`) tại `(139.50, 96.20)`, `D3` (`BLUE_3V3`) tại `(126.00, 95.50)`.
    - `J2` (`BAT_IN`, JST-PH 2P): tại `(118.50, 104.50)`.
    - `J1` (`USB_C_16P`): tại `(130.50, 108.00)`.
    - `SW1` (`ON/OFF`, SPDT `SW_SPDT_PCM12`): tại `(142.50, 104.50)`.

### 2.2. Kiểm tra Antenna Keepout của `ESP32-S3-WROOM-1` (`U3`) & Khoảng cách tới AFE
- **Vị trí ăng-ten**: `U3` đặt tại `(133.00, 46.15)` với `rot = 0°`.
  - Vùng `F.SilkS` của `U3` trải từ `X = [123.80, 142.20]`, `Y = [33.25, 59.10]`.
  - Mép trên của board (`Edge.Cuts`) là `Y = 40.00`. Do đó **phần ăng-ten PCB (`Y = 33.25 .. 40.00`, nhô ra 6.75 mm) đã được đặt nhô ra ngoài mép trên của board** — đúng nguyên tắc tránh suy hao RF do FR4/GND bên dưới.
  - Footprint `U3` có sẵn một `zone` keepout nội bộ trên `F.Cu` và `B.Cu` (`tracks/vias/pads/copperpour/footprints = not_allowed`) tại `X = [109.00, 157.00], Y = [18.40, 39.40]` (nằm hoàn toàn phía ngoài mép `Y = 40.00`).
- **Lỗi phát sinh do đặt quá sát mép `Y = 40.00`**:
  - Pad 1 (`GND`, `(124.25, 40.89)`) và Pad 40 (`GND`, `(141.75, 40.89)`) có kích thước `1.5 x 0.9 mm`, mép trên pad nằm ở `Y = 40.44 mm`, cách `Edge.Cuts` (`Y = 40.00`) chỉ **0.44 mm**, vi phạm rule `edge_clearance = 0.50 mm` (**2 lỗi DRC `copper_edge_clearance`**).
  - `F.SilkS` của `U3` bị cắt ngang qua `Y = 40.00` (**1 cảnh báo `silk_edge_clearance`**).
  - `F.CrtYd` của `U3` (`Y_max = 59.60`) chồng lấn với `R14` (`100 Ω` tại `(110.00, 60.50)`), diện tích chồng lấn `2.68 x 0.05 mm`.
- **Khoảng cách tới khối Analog AFE**:
  - Từ `U3` đến `U4..U6, U8` (`X = 68.50`) là **~55 mm**, đến `J3` (`X = 47.50`) là **~76 mm** → cách ly không gian rất tốt.
  - Từ `U3` đến `U7` (`(110.00, 80.00)`) là **~41 mm** (tâm-tâm) / **~20 mm** (mép-mép) → mức chấp nhận được nếu tránh đi dây analog vòng lên phía `U3`.

### 2.3. Kiểm tra Decoupling Capacitors & Linh kiện hỗ trợ
- **`U3` (`ESP32-S3-WROOM-1`) — NGHIÊM TRỌNG**:
  - Chân `3V3` (Pad 2) của `U3` nằm ở `(124.25, 42.16)`.
  - `C10` (`10uF`) đặt tại `(120.00, 73.00)` → cách chân `3V3` **31.13 mm**.
  - `C11` (`0.1uF`) đặt tại `(120.00, 76.50)` → cách chân `3V3` **34.60 mm**.
  - **Nhận xét**: Tụ lọc nguồn cho ESP32-S3 đặt quá xa (`> 30 mm` thay vì `< 3–5 mm`), không thể đáp ứng dòng xung đỉnh khi phát Wi-Fi/BLE, dễ gây sụt áp/brownout reset.
- **`U2` (`AP2112K-3.3`)**: `C3` (`4.7uF`) cách tâm IC `5.0 mm`, `C4` (`10uF`) cách `8.5 mm` — tạm ổn nhưng nên kéo `C4` lại sát chân `VOUT` (`< 4 mm`).
- **`U1` (`TP4056`)**: `C1` (`10uF`) cách `9.01 mm`, `C2` (`10uF`) cách `8.51 mm` — hơi xa, nên kéo sát về `3–5 mm`.
- **`U4..U8` (`MCP6004`)**: Các tụ bypass `100 nF` (`C63..C67`) đặt cách chân `VDD` (Pin 4) đều **7.42 mm**. Với op-amp băng thông thấp sEMG (`1 MHz` GBW), mức này dùng được nhưng tối ưu nên đặt trong vòng `2–4 mm`.

### 2.4. Kiểm tra Ergonomics / Cơ khí
- **`J1` (`USB_C_16P`)** tại `(130.50, 108.00)`: Hai pad PTH vỏ kim loại (`SH`) tại `(126.18, 109.05)` và `(134.82, 109.05)` chỉ cách mép dưới `Edge.Cuts` (`Y = 110.00`) **0.15 mm** → gây **2 lỗi DRC `copper_edge_clearance`** (`< 0.50 mm`) và **2 cảnh báo `silk_edge_clearance`**. Cần dịch `J1` lên khoảng `0.5–1.0 mm` (hoặc điều chỉnh theo datasheet đầu cắm USB-C).
- **`J2` (`BAT_IN`)** tại `(118.50, 104.50)` và **`SW1` (`ON/OFF`)** tại `(142.50, 104.50)`: Nằm hai bên `J1`, khoảng cách tâm tới `J1` là `12.0 mm` (mép pad `~7.7 mm`), đủ không gian thao tác nhưng nên kiểm tra hướng gạt của `SW1` (nếu là cần gạt ngang hoặc dọc có bị vướng vỏ/dây USB không).
- **`SW2` (`RESET`)** tại `(133.50, 74.00)` và **`SW3` (`BOOT`)** tại `(133.50, 79.50)`: Khoảng cách tâm-tâm là **5.5 mm** (khá sát nhau khi bấm bằng tay nếu dùng nút nhỏ).
- **`J3` (`EMG_ELECTRODES_8P`)** tại `(47.50, 65.00)`: Đặt sát mép trái (`5.5 mm` từ `X = 42.00`), thông thoáng, không bị cấn linh kiện.

### 2.5. Kiểm tra Silkscreen / Ghi chú chân Jump, Công tắc, LED
- Kiểm tra toàn bộ `gr_text` trên lớp `F.SilkS` (`B.SilkS` trống hoàn toàn) cho thấy **chỉ có 7 dòng chữ phân khu**:
  - `'CH1 AFE'`, `'CH2 AFE'`, `'CH3 AFE'`, `'ESP32-S3 MCU'`, `'LI-PO POWER'`, `'SAFETY: UNPLUG USB WHEN WEARING (IEC 60601-1)'`, `'3-CH sEMG AFE (MCP6004 CCIA + HPF + LPF + DRL)'`.
- **Xác nhận thiếu 100% nhãn chân/chức năng trên Silkscreen** cho các linh kiện giao tiếp người dùng:
  - **`J3` (8 chân)**: Không có nhãn cho 8 chân (`Pin 1: EN1`, `Pin 2: EP1`, `Pin 3: EN2`, `Pin 4: EP2`, `Pin 5: EN3`, `Pin 6: EP3`, `Pin 7: DRL`, `Pin 8: VREF`). Khi hàn dây điện cực rất dễ cắm nhầm kênh hoặc ngược cực.
  - **`J2` (`BAT_IN`)**: Không có ký hiệu `+` / `-` phân cực pin Li-Po.
  - **`SW1`**: Không có nhãn `ON` / `OFF` chỉ chiều gạt.
  - **`SW2`, `SW3`**: Chỉ hiện `SW2`, `SW3` (`Value` bị ẩn), không có chữ `RST` / `BOOT` trên `F.SilkS`.
  - **`D1..D4`**: Chỉ hiện `D1..D4`, không có nhãn `CHRG`, `STDBY`, `3V3`, `STAT`.
- **Kiểm tra đè chữ**: Không có `Reference` silkscreen nào đè lên pad (`0` trường hợp).

---

## 3. Phân tích Routing (`hardware/EMG_Armband_3CH.kicad_pcb`)

Kết quả parse trực tiếp file `hardware/EMG_Armband_3CH.kicad_pcb` và chạy `kicad-cli.exe pcb drc`:

| Hạng mục | Kết quả đo được | Ghi chú |
| :--- | :--- | :--- |
| Tổng số `(segment ...)` | **0** | Chưa đi bất kỳ đường mạch nào |
| Tổng số `(arc ...)` | **0** | Chưa có |
| Tổng số `(via ...)` | **0** | Chưa có |
| Tổng số `(zone ...)` cấp board | **0** | Chưa có GND pour trên `F.Cu` / `B.Cu` |
| Phân bố `F.Cu` vs `B.Cu` | `F.Cu: 0`, `B.Cu: 0` | Chưa route |
| Điểm bẻ góc **90°** / **< 90°** | **0 / 0** | Chưa route |
| Differential pair (`USB_DP`, `USB_DN`) | **Chưa route (0 segment)** | Cần route 90 Ω diff pair từ `J1` lên `U3` |
| High-Z nodes ($10\text{ M}\Omega, 22\text{ M}\Omega$), `VREF`, `DRL`, Guard Ring | **Chưa route, chưa có Guard Ring** | Cần thiết kế Guard Ring quanh ngõ vào từ `J3` tới `U4..U6` |
| Ground Plane & Stitching Vias | **Chưa có** | Cần đổ `GND` trên `B.Cu` (liên tục dưới AFE) và `F.Cu` + stitching vias |
| Kết nối chưa hoàn chỉnh (`kicad-cli pcb drc`) | **248 `unconnected_items`** | Toàn bộ net trên board đều đang hở |
| Lỗi DRC (`kicad-cli pcb drc`) | **4 `error`** (`copper_edge_clearance`), **3 `warning`** (`silk_edge_clearance`) | Chưa pass DRC như tiêu đề commit |

---

## 4. Khuyến nghị Hành động Tiếp theo (Action Items)

1. **Sửa Schematic trước khi tiếp tục PCB**:
   - Thêm điện trở cách ly `R_iso` (ví dụ `10 Ω – 47 Ω`) giữa ngõ ra `U8A` và tụ `C61 (10 µF)` trong `hardware/AFE_3CH_AD8232.kicad_sch`, rồi *Update PCB from Schematic*.
2. **Sửa các lỗi Placement & DRC hiện hữu**:
   - Dời `C10 (10 µF)` và `C11 (0.1 µF)` lại sát chân `3V3` (Pad 2) của `U3` (`< 3–5 mm`).
   - Điều chỉnh vị trí `U3` xuống `~0.1–0.2 mm` (hoặc sửa rule/lùi pad `0.06 mm`) để hết 2 lỗi `copper_edge_clearance` ở Pad 1, Pad 40, và dịch `R14` xuống `0.5–1.0 mm` để hết chồng lấn courtyard với `U3`.
   - Dịch `J1` (`USB_C_16P`) vào trong tối thiểu `0.35–0.50 mm` (hoặc kiểm tra footprint USB-C) để hết 2 lỗi `copper_edge_clearance` tại hai chân shield `SH`.
   - Bổ sung đầy đủ text `F.SilkS` cho: `J3` (`EN1, EP1, EN2, EP2, EN3, EP3, DRL, VREF`), `J2` (`+`, `-`), `SW1` (`ON`, `OFF`), `SW2` (`RST`), `SW3` (`BOOT`), `D1..D4` (`CHRG`, `STDBY`, `3V3`, `STAT`).
3. **Thực hiện Routing & Grounding**:
   - Route `USB_DP`/`USB_DN` thành differential pair cân đối, hạn chế via.
   - Giữ các đường High-Z từ `J3` vào `U4..U6` ngắn nhất có thể, bọc **Guard Ring (`VREF`)** trên `F.Cu`, không đi chéo dây số/USB/RF và giữ mặt `GND` trên `B.Cu` liền mạch dưới toàn bộ khối AFE.
   - Đổ `GND` zone trên `F.Cu` và `B.Cu` (tôn trọng vùng keepout dưới ăng-ten `U3`) và thêm stitching vias.
   - Chạy lại `kicad-cli pcb drc` xác nhận **0 errors** và **0 unconnected items** trước khi xuất Gerber.
