# ESP32-S 3-Channel High-Performance sEMG DMA Pipeline Firmware

Chương trình firmware thu thập và tiền xử lý tín hiệu cơ điện đồ bề mặt (sEMG) 3 kênh thời gian thực dành cho vi điều khiển **ESP32-S** (kiến trúc Xtensa Dual-Core LX6, chip ESP32 Classic D0WD), tích hợp phần cứng **Continuous ADC DMA Ping-Pong** và chuỗi tiền xử lý tín hiệu **Zero-Floating-Point Math**, biên dịch bằng **ESP-IDF v5.4.2**.

---

## ⚡ 1. Sơ Đồ Đấu Nối Phần Cứng (Pin Mapping)

Tín hiệu đầu ra từ mạch Analog Front-End (Op-Amp MCP6004, nguồn 3.3V, điểm tựa ảo $V_{\text{REF}} = 1.65\text{ V}$, độ lợi phần cứng $G = 113\times$):

| Kênh sEMG | Nhóm cơ tương ứng | Chân GPIO ESP32-S | Ngoại vi ADC1 | Dải điện áp ngõ vào |
| :---: | :--- | :---: | :---: | :---: |
| **Kênh 1** | *Flexor Carpi Radialis (FCR)* | **GPIO 34** | `ADC1_CHANNEL_6` | $0\text{ V} \to 3.3\text{ V}$ (Atten: 12dB) |
| **Kênh 2** | *Extensor Digitorum Communis (EDC)* | **GPIO 35** | `ADC1_CHANNEL_7` | $0\text{ V} \to 3.3\text{ V}$ (Atten: 12dB) |
| **Kênh 3** | *Brachioradialis* | **GPIO 32** | `ADC1_CHANNEL_4` | $0\text{ V} \to 3.3\text{ V}$ (Atten: 12dB) |
| **GND** | Mass tham chiếu mạch AFE | **GND** | Mass chung | $0\text{ V}$ |

---

## 🏗 2. Kiến Trúc Kỹ Thuật Đã Tối Ưu

### A. Cơ Chế Lấy Mẫu & Triệt Tiêu Xung Đột (DMA & Hardware Matching)
1. **Timer-Triggered Continuous ADC DMA**:
   - Tần số quét DMA: $60\text{ kHz}$ tổng $\to 20\text{ kHz}$ mỗi kênh.
   - Chu kỳ chuyển kênh: $T_{\text{conv}} = 16.67\text{ }\mu\text{s}$.
2. **Settling Time & Triệt Tiêu Kênh Nhiễu Chéo (Channel Crosstalk)**:
   - Mạch lọc RC anti-kickback trên PCB ($R = 100\text{ }\Omega, C = 1\text{ nF}$) có hằng số thời gian $\tau = 100\text{ ns}$.
   - Thời gian nạp đầy $7\tau \approx 0.7\text{ }\mu\text{s} \ll 16.67\text{ }\mu\text{s}$ (dự phòng an toàn $> 20\times$), triệt tiêu hoàn toàn hiện tượng điện tích sót lại trên tụ S/H gây nhiễu chéo giữa 3 kênh.
3. **Ping-Pong (Double) Buffering & Memory Alignment**:
   - Bộ đệm đôi DMA được khai báo `WORD_ALIGNED_ATTR` (căn chỉnh 32-bit / 4-byte trong RAM nội).
   - DMA ghi vào `Buffer[write_idx]`, Worker Task đọc từ `Buffer[read_idx]` luân phiên, triệt tiêu race-condition và duy trì Cache Coherency tuyệt đối.
4. **Cấm Tuyệt Đối Floating-Point Math trong ISR**:
   - ISR callback `s_conv_done_cb` chạy 100% thanh ghi integer, chỉ kích hoạt `vTaskNotifyGiveFromISR`, thời gian thực thi ISR $< 1\text{ }\mu\text{s}$.

### B. Chuỗi Tiền Xử Lý Số 6 Giai Đoạn (100% Integer Math - `semg_pipeline.c`)
1. **Validation: Range & Stuck-At Check**:
   - *Range Check*: Phát hiện bão hòa nguồn (Clipping) khi $\text{ADC} \le 15$ hoặc $\ge 4080$.
   - *Stuck-At Check*: Giám sát độ lệch $|\Delta x| \le 2$ liên tục trong 100 mẫu ($100\text{ ms}$) để cảnh báo tuột điện cực / đứt dây.
2. **Oversampling & Decimation**:
   - Tích lũy trung bình 20 mẫu trên mỗi kênh ($20\text{ kHz} \to 1\text{ kHz}$).
   - Nâng cao ENOB thực tế thêm $+2\text{ bit}$ và giảm nhiễu cao tần $\sim 13\text{ dB}$.
3. **Filtering: Moving Average (Boxcar FIR)**:
   - Bộ lọc Boxcar FIR 8-tap ($M = 8$) chạy bằng phép dịch bit `>> 3` thay vì phép chia.
4. **Calibration**:
   - Sử dụng bảng eFuse Line-Fitting nội bộ của ESP32 để chuyển đổi thành điện áp $\text{mV}$ chuẩn.
5. **Linearization: Curve Compensation**:
   - Bảng nội suy Piecewise Linear LUT 8 điểm bù phi tuyến đặc tính nén SAR ADC ở hai đầu $< 250\text{ mV}$ và $> 2700\text{ mV}$.
6. **Scaling: Physical Unit Conversion**:
   - Quy đổi về điện thế cơ học thực tế tại bề mặt da:
     $$V_{\text{RTI}} (\mu\text{V}) = \frac{(V_{\text{ADC}} - 1650\text{ mV}) \times 1000}{113}$$

---

## 🚀 3. Hướng Dẫn Sử Dụng

### Bước 1: Biên dịch Firmware
```cmd
build.bat
```

### Bước 2: Nạp Firmware
```cmd
flash.bat COM3
```

### Bước 3: Quan sát trực quan trên Serial Plotter
- **Dữ liệu xuất ra UART**: Định dạng CSV `ch1,ch2,ch3\r\n` (đơn vị: microvolts $\mu\text{V}$ Referred-To-Input, hoặc millivolts $\text{mV}$ tại chân chip theo macro `#define SEMG_UART_OUTPUT_RTI_UV`).
- **Ghi log CSV cho bài toán AI**:
  ```cmd
  python tools/plot_semg.py --port COM3 --baud 115200
  ```
