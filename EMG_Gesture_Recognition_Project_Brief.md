# TÀI LIỆU TỔNG HỢP & BÀN GIAO ĐỀ TÀI: NHẬN DẠNG CỬ ĐỘNG TAY BẰNG sEMG 3 KÊNH
*(Sử dụng làm ngữ cảnh tiếp tục cho phiên làm việc trên Fedora Linux)*

---

## 1. TỔNG QUAN ĐỀ TÀI & MỤC TIÊU HOÀN THIỆN

* **Tên đề tài:** Phân loại và nhận dạng cử động cơ bản bằng tín hiệu điện cơ bề mặt (sEMG Gesture Classification).
* **Cấu hình cảm biến:** 3 kênh sEMG đo các nhóm cơ cẳng tay (*Forearm*).
* **4 Trạng thái cử động mục tiêu:**
  1. `Rest` (Thả lỏng)
  2. `Fist` (Nắm bàn tay)
  3. `Open hand` (Xòe / Duỗi bàn tay)
  4. `Pinch` (Chụm ngón / Véo ngón tay)
* **Mức độ hoàn thiện:** **Mức 4 – Sản phẩm hoàn chỉnh**
  * Thiết kế bo mạch phần cứng riêng (Custom PCB gồm khối nguồn, AFE 3 kênh, ESP32-S3).
  * Xử lý tín hiệu và nhúng mô hình TinyML trực tiếp trên Vi điều khiển.
  * Truyền tín hiệu điều khiển không dây (BLE HID - Chuột máy tính hoặc điều khiển cơ cấu chấp hành).
  * Đóng gói thiết bị dạng vòng đeo tay (Armband) in 3D.

---

## 2. KIẾN TRÚC KỸ THUẬT PHẦN CỨNG & Y SINH (AFE & HARDWARE)

### 2.1. Vị trí giải phẫu dán điện cực (Electrode Placement)
* **Kênh 1:** Cơ gấp cổ tay quay (*Flexor Carpi Radialis - FCR*) $\to$ Nhận diện rõ nhất cử động `Fist` và `Pinch`.
* **Kênh 2:** Cơ duỗi các ngón (*Extensor Digitorum Communis - EDC*) $\to$ Nhận diện cử động `Open hand`.
* **Kênh 3:** Cơ cánh tay - quay (*Brachioradialis*) hoặc vùng cơ mô cái cẳng tay $\to$ Tăng cường phân biệt giữa `Fist` và `Pinch`.
* **Điện cực tham chiếu (Reference / DRL):** Đặt ở mỏm khuỷu tay (*Olecranon*) hoặc mỏm trâm xương trụ cổ tay (vùng xương không có cơ hoạt động).

### 2.2. Khối khuếch đại tương tự AFE (Analog Front-End)
* **Tầng 1 (Instrumentation Amp):** Dùng `INA333` (hoặc `AD8237`), hệ số khuếch đại $Gain \approx 10\times - 20\times$ (giữ Gain nhỏ để tránh bão hòa do điện thế phân cực tiếp xúc $\pm 300\text{ mV}$).
* **Mạch DRL (Driven Right Leg):**
  * > [!IMPORTANT]
    > **Chỉ dùng 1 mạch DRL Master duy nhất** nối ra điện cực tham chiếu trên da để triệt nhiễu 50Hz. Nếu dùng chip tích hợp như AD8232 thì các kênh phụ bắt buộc phải ngắt/bỏ trống chân RLD để tránh tự kích dao động.
* **Bộ lọc thông dải y sinh (Bandpass 20Hz – 450Hz):**
  * Lọc thông cao (HPF) $f_c \approx 20\text{ Hz}$ (loại bỏ triệt để trôi nền và nhiễu rung lắc dây).
  * Lọc thông thấp (LPF) $f_c \approx 450\text{ Hz}$ (chống hiện tượng chồng phổ *aliasing* khi lấy mẫu 1000Hz).
* **Đất ảo (Virtual Ground / DC Bias):** Tạo mức tham chiếu $V_{\text{ref}} = 1.65\text{V}$ bằng Op-Amp Buffer để tín hiệu dao động quanh $1.65\text{V}$, tương thích trọn vẹn với dải ADC $0 - 3.3\text{V}$ của ESP32.
* **Tổng hệ số khuếch đại (Total Gain):** $1000\times - 1500\times$.

### 2.3. Khối Vi điều khiển & Nguồn
* **Vi điều khiển:** `ESP32-S3-WROOM-1` (tích hợp sẵn BLE 5.0, ADC, tập lệnh tăng tốc DSP chạy TinyML).
* **Nguồn điện:** Pin Li-Po $3.7\text{V}$ + IC sạc `TP4056` + LDO `AP2112K-3.3` (Ultra-low noise).
  * > [!CAUTION]
    > Tuyệt đối không cắm cổng USB trực tiếp vào máy tính khi đang dán điện cực vào người (phải dùng nguồn Pin Li-Po cách ly hoàn toàn để đảm bảo an toàn điện giật và triệt tiêu nhiễu nguồn lưới).

---

## 3. DATASET & THUẬT TOÁN MACHINE LEARNING

### 3.1. Tập dữ liệu Mendeley (`mc8t88b7p9`)
* **Tên:** *A Compact Surface EMG Dataset for Hand Gesture Recognition Utilizing a Minimal Channel Configuration on Forearm Muscles* (Mendeley Data, DOI: `10.17632/mc8t88b7p9.1`).
* **Đặc điểm:**
  * Thu thập từ đúng **3 kênh sEMG** trên cẳng tay.
  * Tần số lấy mẫu: **1 kHz (1000 Hz)**, định dạng file `.csv` thô.
  * Gồm 14 cử động tay (chứa sẵn `Rest`, `Fist`, `Open hand`, `Pinch`).
* **Ý nghĩa:** Có thể dùng ngay file CSV này để code và tối ưu hóa 100% thuật toán Machine Learning trên Python trước khi làm xong phần cứng.

### 3.2. Quy trình xử lý tín hiệu & Phân loại
* **Lấy mẫu:** $F_s = 1000\text{ Hz}$ (chu kỳ 1ms/mẫu).
* **Cửa sổ trượt (Sliding Window):** Kích thước $200\text{ ms}$ (200 mẫu), bước nhảy $50\text{ ms}$ (đáp ứng trễ chỉ 50ms, người dùng cảm giác phản hồi tức thì).
* **Bộ đặc trưng Hudgins:** Trích xuất 4 đặc trưng miền thời gian:
  1. $RMS$ / $MAV$ (Root Mean Square / Mean Absolute Value)
  2. $WL$ (Waveform Length)
  3. $ZC$ (Zero Crossings)
  4. $SSC$ (Slope Sign Changes)
* **Mô hình:** **SVM (Support Vector Machine)** hoặc **MLP (Mạng Nơ-ron 2 lớp ẩn: 16 - 8)** $\to$ Xuất sang code C qua thư viện `micromlgen` hoặc `emlearn` nạp thẳng vào ESP32.

---

## 4. MÔI TRƯỜNG PHÁT TRIỂN TRÊN FEDORA LINUX & CÔNG CỤ MCP

### 4.1. Đánh giá hệ điều hành Fedora
* **Rất tối ưu:** Fedora có các gói phần mềm mới nhất (KiCad 9.0+, Python 3.12+, Node.js), hỗ trợ IPC socket Unix gốc, không bị lỗi cô lập đường dẫn Python (`pcbnew`) như trên Windows.

### 4.2. Hai lưu ý "sống còn" trên Fedora
1. **Cài KiCad qua DNF (Tuyệt đối KHÔNG cài bản Flatpak):**
   ```bash
   sudo dnf install kicad kicad-packages3d
   ```
   *(Bản Flatpak bị nhốt trong Sandbox, MCP Server bên ngoài sẽ không kết nối được).*
2. **Tạo Virtual Environment với cờ `--system-site-packages`:**
   ```bash
   python3 -m venv --system-site-packages venv_mcp
   source venv_mcp/bin/activate
   pip install pydantic cairosvg pillow
   ```

### 4.3. Chiến lược dùng KiCAD-MCP-Server (`mixelpixx/KiCAD-MCP-Server`)
* **Phân chia Schematic thành 3 Hierarchical Sheets:**
  * Sheet 1: `Power_Supply.kicad_sch` (Type-C, TP4056, LDO 3.3V, VREF 1.65V).
  * Sheet 2: `EMG_AFE_3CH.kicad_sch` (3 kênh INA333 + Lọc thông dải + 1 Master DRL).
  * Sheet 3: `MCU_ESP32.kicad_sch` (ESP32-S3, nút bấm, chân ADC đón tín hiệu).
* **Liên kết giữa các sheet:** Sử dụng **Net Labels** (`3V3`, `AGND`, `DGND`, `VREF_1V65`, `EMG_CH1_ADC`, `EMG_CH2_ADC`, `EMG_CH3_ADC`).
* **Lưu ý Layout PCB:** Tự tay phân tách 2 vùng mass riêng biệt: `AGND` (Mass tín hiệu tương tự) và `DGND` (Mass số của ESP32/WiFi/BLE), nối chung tại 1 điểm duy nhất (Star Ground).

---

## 5. CHECKLIST CÁC BƯỚC THỰC HIỆN TIẾP THEO BÊN LINUX

- [ ] **Bước 1 (Môi trường Fedora):** Chạy lệnh cài KiCad 9 (`sudo dnf install kicad`), Node.js và Git.
- [ ] **Bước 2 (Clone & Test MCP):** Clone `mixelpixx/KiCAD-MCP-Server`, thiết lập theo hướng dẫn và chạy thử lệnh tạo một schematic LED đơn giản để kiểm tra kết nối.
- [ ] **Bước 3 (Thuật toán Python):** Tải dataset Mendeley `mc8t88b7p9`, viết script lọc tín hiệu và trích xuất đặc trưng Hudgins cho 4 cử động.
- [ ] **Bước 4 (Vẽ Schematic):** Điều khiển MCP sinh lần lượt Sheet Nguồn $\to$ Sheet ESP32 $\to$ Sheet AFE 3 Kênh, sau đó chạy ERC kiểm tra lỗi.
