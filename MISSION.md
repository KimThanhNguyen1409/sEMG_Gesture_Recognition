# Mission: Hệ thống IoT nhận diện cử chỉ sEMG thời gian thực (ESP32 + C Server + TFLite)

## Why
Xây dựng và làm chủ toàn bộ luồng hệ thống nhận diện cử chỉ tay từ tín hiệu cơ điện đồ bề mặt (sEMG) 3 kênh: từ mạch AFE + ESP32 trích xuất 15 đặc trưng Hudgins, truyền qua MQTT lên Server viết bằng C chạy model MLP (TensorFlow Lite), đến việc phát lệnh điều khiển xuống thiết bị đích với độ trễ thấp và độ ổn định cao.

## Success looks like
- Hiểu rõ và tự chỉnh sửa được từng mắt xích trong Backend Server C (`Server/src/main.c`): MQTT FSM, Zero-malloc JSON Parser, Z-score Normalization, Dual-Engine TFLite Inference, và Majority Voting Debounce.
- Kết nối thông suốt ESP32 bắn gói tin 15 đặc trưng qua Mosquitto Broker vào `semg_server.exe` và điều khiển thiết bị đầu cuối theo 4 cử chỉ (`RESTING`, `FIST`, `OPEN_HAND`, `POINTING`).
- Chống nhiễu thành công trên mạch PCB AFE + ESP32 bằng bộ lọc số IIR Biquad đồng bộ với model đã huấn luyện.

## Constraints
- Môi trường Server chạy trực tiếp trên Windows (biên dịch bằng MinGW-w64 GCC + Mosquitto DLL).
- Code C tuân thủ chuẩn Embedded/IoT: Không cấp phát bộ nhớ động (`malloc`/`free`) trong vòng lặp xử lý bản tin runtime.

## Out of scope
- Huấn luyện các mô hình chuỗi nặng (LSTM/Transformer) hoặc truyền luồng sóng thô (Raw Window Streaming) tốn băng thông MQTT.
