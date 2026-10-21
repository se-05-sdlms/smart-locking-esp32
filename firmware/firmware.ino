// Firmware xây dựng từ hợp đồng BE/FE và cấu hình phần cứng đã xác nhận.
#include "Hardware.h"
#include "StateStore.h"
#include "Transports.h"
#include "secrets.h"
#include <esp_mac.h>
#include <esp_random.h>
using namespace locker;
namespace {
Hardware hardware;
StateStore storage;
Transports transports;
class Runtime final : public Ports {
public:
  uint64_t monotonic() const override {
    return monotonicMs();
  }
  uint64_t utc() const override {
    return utcSeconds();
  }
  LoadResult load(State &value) override {
    return storage.load(value);
  }
  bool save(const State &value) override {
    bool ok = storage.save(value);
    if (!ok)
      Serial.println("[NVS] Ghi that bai; dung nhan lenh mo moi.");
    return ok;
  }
  bool pulse(unsigned channel) override {
    bool ok = hardware.pulse(channel);
    Serial.printf("[RELAY] channel=%u result=%s\n", channel + 1, ok ? "STARTED" : "REJECTED");
    return ok;
  }
  int door(unsigned channel) const override {
    return hardware.door(channel);
  }
  bool closed(unsigned channel) const override {
    return hardware.closed(channel);
  }
  uint32_t random() override {
    return esp_random();
  }
  bool connected() const override {
    return transports.online();
  }
  uint32_t connectionGeneration() const override {
    return transports.generation();
  }
  bool publish(Text suffix, Text payload, bool retained) override {
    return transports.publish(suffix, payload, retained);
  }
  void respond(Text id, Text result) override {
    transports.response(id, result);
  }
  void doorNotice(unsigned channel, Text status) override {
    Serial.printf("[DOOR] channel=%u state=%.*s\n", channel, static_cast<int>(status.size()),
                  status.data());
    transports.doorNotice(channel, status);
  }
} runtime;
LockerCore *core = nullptr;
bool started = false;
std::string deviceIdentifier() {
  // Dùng MAC thật để chọn đúng tài khoản; không cho cấu hình đổi danh tính board.
  uint8_t mac[6]{};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
    return "";
  char id[12];
  snprintf(id, sizeof(id), "LKR-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return id;
}
const MqttDeviceAccount *mqttAccountForDevice(const std::string &id) {
  const std::string expectedUsername = "esp_" + id;
  for (const auto &account : MQTT_DEVICE_ACCOUNTS) {
    if (account.deviceIdentifier && id == account.deviceIdentifier && account.username &&
        expectedUsername == account.username && account.password && *account.password)
      return &account;
  }
  return nullptr;
}
} // namespace
void setup() {
  hardware.begin();
  Serial.begin(115200);
  const std::string id = deviceIdentifier();
  if (!deviceName(id)) {
    Serial.println("[BOOT] Device ID khong hop le; khong nhan lenh.");
    return;
  }
  // Không dùng tài khoản của tủ khác khi secrets.h thiếu cấu hình cho board này.
  const auto *account = mqttAccountForDevice(id);
  if (!account) {
    Serial.printf("[BOOT] Khong co tai khoan MQTT hop le cho %s; kiem tra secrets.h.\n",
                  id.c_str());
    return;
  }
  Serial.printf("[MQTT] Device=%s username=%s\n", id.c_str(), account->username);
  static LockerCore engine(runtime, id);
  core = &engine;
  core->begin();
  Serial.printf("[BOOT] id=%s NVS=%s\n", id.c_str(),
                core->ready() ? "READY" : "FAULT (giu nguyen du lieu)");
  Credentials credentials{WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, static_cast<uint16_t>(MQTT_PORT),
                          account->username, account->password};
  started = transports.begin(id, credentials);
  Serial.printf("[BOOT] BLE/network=%s heap=%u maxBlock=%u\n", started ? "READY" : "FAULT",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}
void loop() {
  hardware.tick();
  if (started && core) {
    Input input{};
    for (unsigned i = 0; i < 4 && transports.receive(input); ++i)
      core->handle(input);
    core->tick();
  }
  // Nhường CPU một tick; không đợi Wi-Fi, TLS hoặc cửa trên luồng chính.
  vTaskDelay(1);
}
