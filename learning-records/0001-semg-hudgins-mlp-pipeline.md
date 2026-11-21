# Trích xuất 15 đặc trưng Hudgins tại biên (Edge) và bỏ lọc đường bao khi dùng MLP

Nhóm đã thiết kế mạch AFE 3 kênh kết nối ESP32 và lựa chọn gửi 15 đặc trưng miền thời gian (`MAV, RMS, WL, ZC, SSC` x 3 kênh) qua MQTT thay vì gửi cửa sổ 200 mẫu thô. Thực nghiệm trên tập dữ liệu 300 trials cho thấy việc bỏ bước lọc thông thấp đường bao (`envelope 10Hz`) trước khi tính bộ đặc trưng Hudgins giúp giữ lại thông tin tần số (`ZC`, `SSC`, `WL`), nâng độ chính xác Validation lên 89.17% và Test lên 86.87%.
