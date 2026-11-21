# sEMG IoT & C Inference Server Resources

## Knowledge

- [Eclipse Mosquitto Client C API (`mosquitto.h` Official Docs)](https://mosquitto.org/api/files/mosquitto-h.html)
  Tài liệu gốc chính thức về vòng đời kết nối MQTT trong C (`mosquitto_new`, `mosquitto_loop`, callback `on_connect`, `on_message`, và cơ chế tự động kết nối lại `mosquitto_reconnect_delay_set`).
- [TensorFlow Lite C API Reference (`c_api.h` Header Specification)](https://github.com/tensorflow/tensorflow/blob/master/tensorflow/lite/c/c_api.h)
  Đặc tả chính thức của Google cho TFLite C API: cách nạp `.tflite` (`TfLiteModelCreateFromFile`), cấp phát vùng nhớ tĩnh một lần (`TfLiteInterpreterAllocateTensors`) và thực thi suy luận (`TfLiteInterpreterInvoke`).
- [Hudgins et al. (1993) - "A New Strategy for Multifunction Myoelectric Control" (IEEE TBME)](https://doi.org/10.1109/10.204774)
  Bài báo nền tảng định nghĩa bộ đặc trưng miền thời gian (MAV, WL, ZC, SSC) kết hợp mạng MLP để phân loại cử chỉ tay từ tín hiệu sEMG.

## Wisdom (Communities)

- [r/Embedded](https://www.reddit.com/r/embedded/)
  Cộng đồng kỹ sư hệ thống nhúng chuyên sâu để thảo luận về lọc nhiễu ADC trên PCB, thiết kế mạch AFE và tối ưu hóa C/FreeRTOS trên ESP32.
- [Eclipse Mosquitto Forum](https://forum.cedalo.com/c/eclipse-mosquitto/5)
  Diễn đàn trao đổi thực tế về cấu hình Broker, tối ưu QoS và xử lý kết nối MQTT chịu tải thời gian thực.
