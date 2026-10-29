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

Mã tủ lấy ba byte cuối MAC Wi-Fi eFuse (`LKR-XXXXXX`); không dùng CUSTOM_DEVICE_ID để đổi danh tính board. Firmware tự chọn tài khoản và khóa tương ứng trong secrets.h theo MAC thật. README trước đây ghi tủ A `LKR-309228` bốn ngăn, tủ B `LKR-1A0844` một ngăn. Dùng các ngăn BE thực sự khai báo; firmware không thêm ngăn trong DB.

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
- Điện thoại có Internet/tủ mất MQTT: BE xác thực quyền rồi cấp `b2|HMAC|c1...`; điện thoại chuyển qua BLE. RESET dùng cùng envelope với payload z1.
- Điện thoại mất Internet: không tạo giao dịch mới. RETURN/PICKUP/PREPARE cũ bị từ chối; không còn resident permit.
- Khóa HMAC 32 byte (64 ký tự hex) riêng từng tủ trong `MqttDeviceAccount.commandSigningKeyHex` phải khớp `Bluetooth:CommandSigningKeys:<DeviceIdentifier>` của BE. Thiếu/sai khóa không mở hoặc reset. Nạp BE và firmware mới cùng đợt.
- Chỉ hoàn tất sau cửa mở rồi đóng; nhật ký giữ trong NVS đến ACK từ BE.
- NVS lỗi giữ nguyên dữ liệu và chặn mở mới. Ngăn chưa có inventory không được tự coi là trống.

Bộ nhớ dùng namespace mới `locker_core_v1`. Trước khi triển khai vào thiết bị đã có lịch sử hoặc hàng đang lưu, đối chiếu NVS/sequence với BE; không có chức năng tự xóa hoặc nhập dữ liệu firmware cũ.

## Kiểm tra

```powershell
.\tests\run-tests.ps1 -BackendRoot D:\code_capstone\smart-locking-be
```

Cần .NET 8, g++ C++17 và OpenSSL libcrypto cho native tests (script nhận đường dẫn qua `-Compiler`). Tests dùng core và adapter production với GPIO/NVS giả lập; không mở relay thật hoặc gửi lệnh lên broker. Source BE chỉ được link để kiểm tra codec, không sửa file BE.

Đọc [báo cáo phân tích và kết quả](docs/FIRMWARE_REBUILD.md) để xem hợp đồng, vai trò từng file, kết quả kiểm tra và các giới hạn chưa thể giải quyết chỉ bằng firmware, đặc biệt phí offline và tích hợp client. Không coi compile/native test là nghiệm thu board thật.

## Đặt lại trạng thái ngăn

Firmware hỗ trợ lệnh `z1` qua MQTT và `RESET:z1|...` qua BLE, phản hồi `z2` qua BLE/MQTT `events/state-reset`. Lệnh kiểm tra đúng giao dịch, không kích relay; chỉ xóa PIN/trạng thái của ngăn chỉ định sau khi lưu NVS thành công. Nhật ký chưa được BE tiếp nhận chặn reset. Không đổi cấu trúc NVS, không xóa ngăn khác hay bộ đếm thứ tự. Trang quản trị test tại `http://localhost:8765/maintenance.html`; cần nạp bản firmware này trước khi sử dụng.
