#include "Transports.h"
#include "Hardware.h"
#include "QueuePump.h"
#include <cstring>
namespace locker {
Transports *Transports::instance_ = nullptr;
namespace {
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, NimBLEConnInfo &info) override {
    // Một điện thoại thao tác tại một thời điểm để requestId không bị lẫn giữa hai phiên.
    if (server->getConnectedCount() > 1)
      server->disconnect(info.getConnHandle());
    else
      Serial.println("[BLE] Da ket noi.");
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    NimBLEDevice::startAdvertising();
    Serial.println("[BLE] Da ngat; quang ba lai.");
  }
} serverCallbacks;
class WriteCallbacks : public NimBLECharacteristicCallbacks {
public:
  Transports *transport = nullptr;
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override {
    auto value = characteristic->getValue();
    Text frame(value.c_str(), value.size()), id, body;
    if (!parseFrame(frame, id, body))
      return;
    Input input{};
    input.kind = InputKind::Ble;
    copyText(input.payload, sizeof(input.payload), frame);
    if (!transport->enqueue(input))
      Serial.println("[BLE] Hang doi day; client can thu lai sau timeout.");
  }
} writeCallbacks;
} // namespace
bool Transports::begin(Text device, Credentials credentials) {
  device_ = device;
  credentials_ = credentials;
  instance_ = this;
  incoming_ = xQueueCreate(8, sizeof(Input));
  outgoing_ = xQueueCreate(8, sizeof(Outbound));
  if (!incoming_ || !outgoing_)
    return false;
  if (!NimBLEDevice::init("BOXORA-" + device_))
    return false;
  NimBLEDevice::setMTU(247);
  auto *server = NimBLEDevice::createServer();
  if (!server)
    return false;
  server->setCallbacks(&serverCallbacks, false);
  auto *service = server->createService(ServiceUuid);
  if (!service)
    return false;
  auto *command = service->createCharacteristic(
      CommandUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, 400);
  response_ = service->createCharacteristic(ResponseUuid,
                                            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 512);
  doors_ =
      service->createCharacteristic(DoorUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 80);
  auto *identifier = service->createCharacteristic(DeviceUuid, NIMBLE_PROPERTY::READ, 100);
  if (!command || !response_ || !doors_ || !identifier)
    return false;
  writeCallbacks.transport = this;
  command->setCallbacks(&writeCallbacks);
  identifier->setValue(device_);
  doors_->setValue("DOOR:UNKNOWN");
  if (!server->start())
    return false;
  auto *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(ServiceUuid);
  advertising->enableScanResponse(true);
  advertising->setName("BOXORA-" + device_);
  if (!advertising->start())
    return false;
  return xTaskCreatePinnedToCore(networkEntry, "locker-network", 8192, this, 1, nullptr, 0) ==
         pdPASS;
}
bool Transports::enqueue(const Input &input) {
  return incoming_ && xQueueSend(incoming_, &input, 0) == pdTRUE;
}
bool Transports::receive(Input &input) {
  return incoming_ && xQueueReceive(incoming_, &input, 0) == pdTRUE;
}
bool Transports::publish(Text suffix, Text payload, bool retained) {
  if (!outgoing_ || suffix.empty() || suffix.size() >= sizeof(Outbound::suffix) ||
      !printable(payload))
    return false;
  Outbound message{};
  copyText(message.suffix, sizeof(message.suffix), suffix);
  copyText(message.payload, sizeof(message.payload), payload);
  message.retained = retained;
  return xQueueSend(outgoing_, &message, 0) == pdTRUE;
}
void Transports::response(Text id, Text result) {
  if (!response_)
    return;
  std::string value = std::string(id) + "|" + std::string(result);
  if (value.size() > 512)
    value = std::string(id) + "|FAIL:RESPONSE_TOO_LONG";
  response_->setValue(value);
  // Giữ đầy đủ trong READ; notify chỉ báo có phản hồi mới khi MTU nhỏ.
  response_->notify(reinterpret_cast<const uint8_t *>(value.data()),
                    std::min<size_t>(20, value.size()));
}
void Transports::doorNotice(unsigned channel, Text status) {
  if (!doors_)
    return;
  std::string value = "DOOR:" + std::to_string(channel) + ":" + std::string(status);
  doors_->setValue(value);
  doors_->notify();
}
void Transports::mqttReceived(char *topic, uint8_t *payload, unsigned length) {
  if (instance_ && length <= 512)
    instance_->inbound(topic, Text(reinterpret_cast<char *>(payload), length));
}
void Transports::inbound(Text topic, Text payload) {
  if (!printable(payload))
    return;
  std::string prefix = "lockers/" + device_ + "/";
  if (topic.substr(0, prefix.size()) != prefix)
    return;
  Text suffix = topic.substr(prefix.size());
  Input input{};
  if (suffix == "inventory")
    input.kind = InputKind::Inventory;
  else if (suffix == "pins")
    input.kind = InputKind::Pin;
  else if (suffix == "events/offline/ack")
    input.kind = InputKind::Ack;
  else if (suffix == "workflow/response")
    input.kind = InputKind::Remote;
  else if (suffix.substr(0, 6) == "doors/") {
    uint64_t channel;
    if (!number(suffix.substr(6), channel) || channel < 1 || channel > Channels)
      return;
    input.channel = channel;
    if (payload.substr(0, 3) == "z1|") {
      Text fields[7]; uint64_t target;
      if (!split(payload, '|', fields, 7) || !number(fields[3], target) || target != channel) return;
      input.kind = InputKind::Reset;
    } else if (payload.substr(0, 3) == "c1|") {
      Workflow w;
      if (!parseWorkflow(payload, device_, w) || w.channel != channel)
        return;
      input.kind = InputKind::Workflow;
    } else if (payload == "OPEN" || payload.substr(0, 5) == "OPEN:")
      input.kind = InputKind::Legacy;
    else
      return;
  } else
    return;
  copyText(input.payload, sizeof(input.payload), payload);
  if (!enqueue(input))
    Serial.println("[MQTT] Hang doi lenh day; ban tin khong duoc xu ly.");
}
void Transports::networkEntry(void *self) {
  static_cast<Transports *>(self)->networkLoop();
}
void Transports::networkLoop() {
  // TLS dùng CA có sẵn trong ESP32 core; không thêm khóa hoặc cấu hình chữ ký riêng.
  tls_.useBuiltinCACertBundle();
  tls_.setTimeout(10000);
  tls_.setConnectionTimeout(10000); // ESP32 core tách timeout TCP khỏi timeout của Stream.
  tls_.setHandshakeTimeout(15);
  mqtt_.setServer(credentials_.broker, credentials_.port);
  while (!mqtt_.setBufferSize(1024)) {
    Serial.println("[MQTT] Khong cap phat duoc buffer; BLE van hoat dong, thu lai sau 5 giay.");
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
  mqtt_.setSocketTimeout(10);
  mqtt_.setKeepAlive(30);
  mqtt_.setCallback(mqttReceived);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(credentials_.ssid, credentials_.wifiPassword);
  bool hadWifi = false, hasPending = false;
  Outbound pending{};
  uint64_t nextTry = 0, lastWifiTry = 0, lastClockLog = 0;
  uint32_t backoff = 1000;
  const std::string prefix = "lockers/" + device_ + "/";
  for (;;) {
    uint64_t now = monotonicMs();
    bool wifi = WiFi.status() == WL_CONNECTED;
    online_ = wifi && mqtt_.connected();
    if (wifi != hadWifi) {
      hadWifi = wifi;
      if (wifi) {
        Serial.printf("[WIFI] IP=%s RSSI=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
      } else {
        online_ = false;
        mqtt_.disconnect();
        tls_.stop();
        Serial.println("[WIFI] Da ngat; BLE van hoat dong.");
      }
    }
    if (!wifi) {
      if (now - lastWifiTry >= 15000) {
        lastWifiTry = now;
        WiFi.reconnect();
      }
    } else if (!utcSeconds()) {
      if (now - lastClockLog >= 10000) {
        lastClockLog = now;
        Serial.println("[NTP] Chua co gio UTC; TLS/lenh co han chua san sang.");
      }
    } else if (!mqtt_.connected()) {
      if (now >= nextTry) {
        Serial.printf("[MQTT] Ket noi %s:%u heap=%u maxBlock=%u stack=%u\n", credentials_.broker,
                      credentials_.port, ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                      uxTaskGetStackHighWaterMark(nullptr));
        uint64_t started = monotonicMs();
        if (mqtt_.connect(device_.c_str(), credentials_.user, credentials_.password)) {
          bool subscribed = true;
          for (const char *suffix :
               {"doors/+", "pins", "inventory", "workflow/response", "events/offline/ack"})
            subscribed = mqtt_.subscribe((prefix + suffix).c_str(), 1) && subscribed;
          if (subscribed) {
            ++generation_;
            online_ = true;
            backoff = 1000;
            Serial.println("[MQTT] Da ket noi; da gui subscribe.");
          } else {
            mqtt_.disconnect();
            Serial.println("[MQTT] Subscribe loi; se ket noi lai.");
          }
        } else {
          char error[120]{};
          int code = tls_.lastError(error, sizeof(error));
          Serial.printf("[MQTT] state=%d TLS=%d (%s) elapsed=%llu ms heap=%u maxBlock=%u\n",
                        mqtt_.state(), code, error,
                        static_cast<unsigned long long>(monotonicMs() - started), ESP.getFreeHeap(),
                        ESP.getMaxAllocHeap());
          tls_.stop();
          backoff = std::min<uint32_t>(backoff * 2, 30000);
        }
        nextTry = monotonicMs() + backoff;
      }
    } else {
      mqtt_.loop();
      // Chỉ task này dùng MQTT client. Bản tin lỗi được giữ qua lần reconnect.
      drainOutgoing(
          pending, hasPending,
          [&](Outbound &next) { return xQueueReceive(outgoing_, &next, 0) == pdTRUE; },
          [&](const Outbound &next) {
            return mqtt_.publish((prefix + next.suffix).c_str(), next.payload, next.retained);
          });
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
} // namespace locker
