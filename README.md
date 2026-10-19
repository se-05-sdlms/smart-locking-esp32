# Firmware ESP32 cho tủ Boxora

Firmware mới cho ESP32 Dev Module, hỗ trợ tối đa bốn kênh relay/công tắc theo phần cứng đã xác nhận. Giữ hợp đồng MQTT và BLE của Backend hiện tại; không gửi SMS.

## Phần cứng

| Kênh | GPIO relay | GPIO công tắc |
|---|---|---|
| 1 | 23 | 32 |
| 2 | 22 | 33 |
| 3 | 21 | 25 |
| 4 | 19 | 26 |

Relay HIGH mở 2 giây; timer tự đưa về LOW. Cửa đóng nối GND → LOW; mở → HIGH, INPUT_PULLUP, debounce 50 ms. Không coi kích relay là cửa đã mở hoặc giao dịch đã hoàn tất.

Mã tủ mặc định lấy ba byte cuối MAC Wi-Fi eFuse (`LKR-XXXXXX`), có thể đặt CUSTOM_DEVICE_ID trong secrets. README trước đây ghi tủ A `LKR-309228` bốn ngăn, tủ B `LKR-1A0844` một ngăn. Dùng các ngăn BE thực sự khai báo; firmware không thêm ngăn trong DB.

## Build

Đã dùng ESP32 core **3.3.12**, **NimBLE-Arduino 2.5.1**, **PubSubClient 2.8**. Giữ nguyên file `firmware/secrets.h` hiện có; máy mới sao chép `secrets.example.h` thành `secrets.h` và cấu hình riêng. Không commit secrets.

```powershell
arduino-cli lib install "NimBLE-Arduino@2.5.1" "PubSubClient@2.8"
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .\firmware
```

Arduino IDE: ESP32 Dev Module, flash 4 MB, PSRAM Disabled, Huge APP 3 MB No OTA, Serial 115200. Build không tự nạp board hoặc xóa flash. Các file .cpp được IDE biên dịch cùng sketch.

## Luồng chính

- MQTT online: `c1` D/P/R/G và lệnh mở vận hành từ BE.
- BLE: service fff0, ghi fff1, phản hồi fff2, ID fff3, cửa fff4. Frame `requestId|body`.
- Điện thoại có mạng/tủ mất mạng: EXEC từ BE qua BLE.
- Điện thoại mất mạng/tủ có mạng: PREPARE qua MQTT của tủ.
- Cả hai mất mạng: resident dùng permit đã lưu để RETURN hoặc PIN inbound đã nạp để PICKUP.
- Chỉ hoàn tất sau cửa mở rồi đóng; nhật ký giữ trong NVS đến ACK từ BE.
- NVS lỗi giữ nguyên dữ liệu và chặn mở mới. Ngăn chưa có inventory không được tự coi là trống.

Bộ nhớ dùng namespace mới `locker_core_v1`. Trước khi triển khai vào thiết bị đã có lịch sử hoặc hàng đang lưu, đối chiếu NVS/sequence với BE; không có chức năng tự xóa hoặc nhập dữ liệu firmware cũ.

## Kiểm tra

```powershell
.\tests\run-tests.ps1 -BackendRoot D:\code_capstone\smart-locking-be
```

Cần .NET 8 và g++ C++17 (script nhận đường dẫn qua `-Compiler`). Tests dùng core và adapter production với GPIO/NVS giả lập; không mở relay thật hoặc gửi lệnh lên broker. Source BE chỉ được link để kiểm tra codec, không sửa file BE.

Đọc [báo cáo phân tích và kết quả](docs/FIRMWARE_REBUILD.md) để xem hợp đồng, vai trò từng file, kết quả kiểm tra và các giới hạn chưa thể giải quyết chỉ bằng firmware, đặc biệt phí offline và tích hợp client. Không coi compile/native test là nghiệm thu board thật.
