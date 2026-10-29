#pragma once
// Sao chép thành secrets.h và cấu hình cục bộ; secrets.h được Git bỏ qua.
const char *WIFI_SSID = "";
const char *WIFI_PASSWORD = "";
const char *MQTT_SERVER = "";
const int MQTT_PORT = 8883;
// Cùng sketch nạp cho cả hai board: firmware đọc MAC thật để chọn đúng dòng.
// Điền mật khẩu thật chỉ trong secrets.h; không cấu hình tài khoản BE tại đây.
struct MqttDeviceAccount {
  const char *deviceIdentifier;
  const char *username;
  const char *password;
  const char *commandSigningKeyHex; // 32 byte ngẫu nhiên dạng 64 ký tự hex, khớp cấu hình BE của tủ.
};
const MqttDeviceAccount MQTT_DEVICE_ACCOUNTS[] = {
    {"LKR-309228", "esp_LKR-309228", "", ""},
    {"LKR-1A0844", "esp_LKR-1A0844", "", ""},
};
