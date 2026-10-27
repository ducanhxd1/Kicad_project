# Pump Timer 

## Hiển thị

- Thứ, ngày/tháng/năm và giờ chạy theo thời gian Việt Nam.
- Trạng thái P1/P2, chế độ AUTO/MAN/OFF và thời gian còn lại khi bơm đang chạy.
- Trạng thái Wi-Fi và địa chỉ IP dùng để mở website.
- Khi lịch hoặc chạy tay được thay đổi trên website, TFT hiện thông báo trong
  12 giây rồi tự quay về màn trạng thái.
- DS3231 giữ giờ khi mất Wi-Fi; khi có mạng, dịch vụ NTP đồng bộ lại RTC.

## Kết nối phần cứng

| Thiết bị | ESP32-C3 | Ghi chú |
|---|---:|---|
| TFT SCL / SCK | GPIO6 | SPI clock |
| TFT SDA / MOSI | GPIO7 | SPI data, không nối MISO |
| TFT CS | GPIO3 | Chip select |
| TFT DC | GPIO4 | Data/command |
| TFT RST | GPIO5 | Reset |
| TFT BLK | nguồn đèn nền | Board TFT có thể đã nối sẵn; kiểm tra đúng module |
| DS3231 SDA | GPIO0 | I²C data |
| DS3231 SCL | GPIO1 | I²C clock |
| Relay P1 | GPIO20 | ESP32-C3 U0RXD; xem lưu ý bên dưới |
| Relay P2 | GPIO10 | Mức kích mặc định cấu hình active-high |
| Nguồn/GND | theo module | Nối chung GND; kiểm tra mức logic và nguồn relay/TFT |


## Cài đặt và nạp code 

Yêu cầu ESP-IDF 5.5 và toolchain cho ESP32-C3. Mở terminal ESP-IDF:

```bash
cd PumpTimer12V
idf.py set-target esp32c3
idf.py menuconfig
idf.py build
idf.py -p PORT flash monitor
```


## Cấu trúc chính

- `main/`: khởi động, khởi tạo phần cứng và kết nối các dịch vụ.
- `components/st7735/`, `components/app_spi/`: driver TFT và bus SPI.
- `components/tft_text/`: font bitmap 5×7 và hàm vẽ chữ tối giản.
- `components/pump_display/`: màn hình trạng thái, không có đầu vào nút bấm.
- `components/pump_rtc/`: driver DS3231 qua I²C.
- `components/pump_schedule/`, `pump_storage/`, `pump_controller/`,
  `pump_output/`: lịch, lưu trữ và điều khiển relay.
- `components/web_dashboard/`: website và API điều khiển.
- `components/time_manager/`, `time_service/`, `ntp/`: giờ hệ thống, RTC, NTP
  và múi giờ.


## Demo video 

Demo basic ở link này, bấm vào để xem.

- `Link`: https://youtu.be/xzISH9F25kE
