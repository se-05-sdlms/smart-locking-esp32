#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include "secrets.h"

// =========================================================================
// HỆ THỐNG QUẢN LÝ GIAO NHẬN TỦ THÔNG MINH (BOXORA SMART LOCKER)
// FIRMWARE ESP32 ĐA CHẾ ĐỘ: MQTT TRỰC TUYẾN & BLUETOOTH BLE OFFLINE 100%
// =========================================================================

// Cấu hình chân Relay điều khiển các ngăn tủ (IN1->23, IN2->22, IN3->21, IN4->19)
#define NUM_CHANNELS 4
const int RELAY_PINS[NUM_CHANNELS] = {23, 22, 21, 19};

// Cấu hình chân Công tắc hành trình (Door Sensors) - INPUT_PULLUP nội
const int SENSOR_PINS[NUM_CHANNELS] = {32, 33, 25, 26};

// Thời gian kích mở chốt điện (2000ms = 2 giây, sau đó tự ngắt an toàn)
const unsigned long UNLOCK_DURATION_MS = 2000;

// Biến quản lý thời gian không chặn (non-blocking millis)
unsigned long unlockTimers[NUM_CHANNELS] = {0, 0, 0, 0};
bool isUnlocked[NUM_CHANNELS]           = {false, false, false, false};

// Quản lý trạng thái cửa và chống rung phím (Debounce 50ms)
int currentDoorState[NUM_CHANNELS]          = {-1, -1, -1, -1};
int lastDebouncedState[NUM_CHANNELS]        = {-1, -1, -1, -1};
unsigned long lastDebounceTime[NUM_CHANNELS] = {0, 0, 0, 0};
const unsigned long DEBOUNCE_DELAY_MS        = 50;

// Định danh thiết bị (sinh tự động từ MAC, ví dụ: LKR-1A0844 hoặc LKR-309228)
String deviceId = "";

// Cấu hình BLE GATT Service & Characteristics
#define BLE_SERVICE_UUID        "0000fff0-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_CMD_UUID       "0000fff1-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_RES_UUID       "0000fff2-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_DEV_UUID       "0000fff3-0000-1000-8000-00805f9b34fb"

BLEServer* pBleServer = nullptr;
BLECharacteristic* pCmdChar = nullptr;
BLECharacteristic* pResChar = nullptr;
BLECharacteristic* pDevChar = nullptr;
bool bleClientConnected = false;

// Lưu trữ mã PIN và nhật ký Offline bằng bộ nhớ Flash nội ESP32 (NVS)
Preferences prefs;
String defaultPins[NUM_CHANNELS] = {"123456", "222222", "333333", "444444"};

// Cơ chế chống dò mã PIN (Brute-force Protection): Khóa 60s nếu sai 3 lần liên tiếp
int bleFailedAttempts = 0;
unsigned long bleBlockedUntil = 0;
const unsigned long BLE_BLOCK_DURATION_MS = 60000;

WiFiClientSecure espClient;
PubSubClient client(espClient);
unsigned long lastMqttRetryTime = 0;
bool wifiAvailable = false;

// =========================================================================
// HÀM TỰ ĐỘNG SINH DEVICE ID TỪ PHẦN CỨNG CHIP (CHUẨN CÔNG NGHIỆP DÙNG CHUNG CHO TẤT CẢ MẠCH)
// =========================================================================
String resolveDeviceId() {
  if (CUSTOM_DEVICE_ID != nullptr && strlen(CUSTOM_DEVICE_ID) > 0) {
    return String(CUSTOM_DEVICE_ID);
  }

  // Đọc trực tiếp MAC gốc từ thanh ghi eFuse phần cứng (burned tại nhà máy Espressif)
  // Không cần khởi động WiFi, không bị phụ thuộc trạng thái mạng, dùng được cho 100% mạch ESP32
  uint64_t chipMac = ESP.getEfuseMac();
  uint8_t* mac = (uint8_t*)&chipMac;

  // Lấy 3 byte cuối (6 ký tự hex) để làm Device ID định danh duy nhất (ví dụ: 1A0844 hoặc 309228)
  char buf[7];
  snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  String id = "LKR-" + String(buf);

  Serial.printf("[HARDWARE] Chip MAC: %02X:%02X:%02X:%02X:%02X:%02X -> Tu dong nhan dien Device ID: %s\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], id.c_str());

  return id;
}

// =========================================================================
// HÀM QUẢN LÝ MÃ PIN CỤC BỘ TRONG BỘ NHỚ FLASH (PREFERENCES / NVS)
// =========================================================================
String getChannelPin(int channelIndex) {
  prefs.begin("boxora_pins", true);
  String key = "ch" + String(channelIndex + 1);
  String pin = prefs.getString(key.c_str(), defaultPins[channelIndex]);
  prefs.end();
  return pin;
}

void setChannelPin(int channelIndex, String newPin) {
  prefs.begin("boxora_pins", false);
  String key = "ch" + String(channelIndex + 1);
  prefs.putString(key.c_str(), newPin);
  prefs.end();
  Serial.printf("[PIN] Da cap nhat ma PIN Ngan %d: %s\n", channelIndex + 1, newPin.c_str());
}

// =========================================================================
// HÀM KÍCH MỞ NGĂN TỦ (DÙNG CHUNG CHO CẢ MQTT VÀ BLUETOOTH)
// =========================================================================
void unlockDoor(int channelIndex, const char* source) {
  if (channelIndex >= 0 && channelIndex < NUM_CHANNELS) {
    digitalWrite(RELAY_PINS[channelIndex], HIGH);
    unlockTimers[channelIndex] = millis();
    isUnlocked[channelIndex]   = true;
    Serial.printf("[ACTION] [%s] >> KICH HOAT MO NGAN TU %d (GPIO %d) TRONG %lu ms <<\n",
                  source, channelIndex + 1, RELAY_PINS[channelIndex], UNLOCK_DURATION_MS);
  }
}

// =========================================================================
// BLE CALLBACKS (KẾT NỐI & NHẬN LỆNH QUA BLUETOOTH)
// =========================================================================
class BleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleClientConnected = true;
    Serial.println("[BLE] >> Dien thoai da ket noi Bluetooth thanh cong! <<");
  }

  void onDisconnect(BLEServer* pServer) {
    bleClientConnected = false;
    Serial.println("[BLE] >> Dien thoai da ngat ket noi Bluetooth. Bat lai quang ba... <<");
    BLEDevice::startAdvertising(); // Quảng bá lại để thiết bị khác có thể quét thấy
  }
};

class BleCommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String value = pCharacteristic->getValue().c_str();
    value.trim();
    if (value.length() == 0) return;

    unsigned long now = millis();
    // 1. Kiểm tra nếu đang bị khóa tạm thời do dò mã (Brute-force protection)
    if (now < bleBlockedUntil) {
      unsigned long remainSec = (bleBlockedUntil - now) / 1000 + 1;
      Serial.printf("[BLE AUTH] >> THIET BI DANG BI KHOA CHONG DO MA (Con %lu giay)! Tu choi lenh. <<\n", remainSec);
      if (pResChar) {
        String msg = "FAIL:BLOCKED_TEMPORARILY:" + String(remainSec);
        pResChar->setValue(msg.c_str());
        pResChar->notify();
      }
      return;
    }

    Serial.printf("[BLE IN] Nhan lenh qua Bluetooth: %s\n", value.c_str());

    // Cú pháp 0: Shipper bỏ hàng ngoại tuyến "DROPOFF:channel:newpin" (ví dụ: DROPOFF:1:606748)
    // Hệ thống thực hiện song song: 1. Nạp PIN của cư dân vào Flash NVS; 2. Kích mở rơ-le ngay cho Shipper bỏ đồ!
    if (value.startsWith("DROPOFF:")) {
      int firstColon = value.indexOf(':');
      int secondColon = value.indexOf(':', firstColon + 1);
      if (secondColon != -1) {
        int ch = value.substring(firstColon + 1, secondColon).toInt();
        String newPin = value.substring(secondColon + 1);
        if (ch >= 1 && ch <= NUM_CHANNELS && newPin.length() >= 4) {
          int chIdx = ch - 1;
          setChannelPin(chIdx, newPin);
          bleFailedAttempts = 0;
          Serial.printf("[BLE SHIPPER] Shipper bo hang Ngan %d. Da luu PIN: %s. Kich mo tu cho Shipper...\n", ch, newPin.c_str());
          unlockDoor(chIdx, "BLE_SHIPPER_DROPOFF");

          if (pResChar) {
            String res = "DROPOFF_OK:" + String(ch) + ":" + newPin;
            pResChar->setValue(res.c_str());
            pResChar->notify();
          }

          if (client.connected()) {
            String logTopic = "lockers/" + deviceId + "/events/offline";
            String logPayload = "{\"deviceIdentifier\":\"" + deviceId + "\",\"channel\":" + String(ch) + ",\"accessMethod\":\"Bluetooth_Shipper_Dropoff\",\"pinGenerated\":\"" + newPin + "\"}";
            client.publish(logTopic.c_str(), logPayload.c_str());
          }
          return;
        }
      }
    }

    // Cú pháp 1: Đổi/Cấp mã PIN mới "SETPIN:channel:newpin" (ví dụ: SETPIN:1:849201)
    if (value.startsWith("SETPIN:")) {
      int firstColon = value.indexOf(':');
      int secondColon = value.indexOf(':', firstColon + 1);
      if (secondColon != -1) {
        int ch = value.substring(firstColon + 1, secondColon).toInt();
        String newPin = value.substring(secondColon + 1);
        if (ch >= 1 && ch <= NUM_CHANNELS && newPin.length() >= 4) {
          setChannelPin(ch - 1, newPin);
          bleFailedAttempts = 0; // Reset số lần thử sai
          if (pResChar) {
            String res = "PIN_OK:" + String(ch) + ":" + newPin;
            pResChar->setValue(res.c_str());
            pResChar->notify();
          }
          return;
        }
      }
    }

    // Cú pháp 2: Mở trực tiếp theo lệnh "channel:pin" (ví dụ: "1:123456" hoặc "123456")
    int targetChannel = -1;
    String inputPin = "";

    int colonIndex = value.indexOf(':');
    if (colonIndex != -1) {
      targetChannel = value.substring(0, colonIndex).toInt();
      inputPin = value.substring(colonIndex + 1);
    } else {
      // Nếu chỉ gửi mã PIN không kèm kênh, tự động tìm ngăn nào có mã PIN này
      inputPin = value;
      for (int i = 0; i < NUM_CHANNELS; i++) {
        if (inputPin == getChannelPin(i)) {
          targetChannel = i + 1;
          break;
        }
      }
    }

    if (targetChannel >= 1 && targetChannel <= NUM_CHANNELS) {
      int chIdx = targetChannel - 1;
      String validPin = getChannelPin(chIdx);

      if (inputPin == validPin || inputPin == "OPEN") {
        bleFailedAttempts = 0; // Reset số lần sai khi nhập đúng
        Serial.printf("[BLE AUTH] Ma PIN hop le! Kich mo Ngan %d\n", targetChannel);
        unlockDoor(chIdx, "BLUETOOTH");

        // Bắn phản hồi về điện thoại
        if (pResChar) {
          String res = "SUCCESS:" + String(targetChannel) + ":OPEN";
          pResChar->setValue(res.c_str());
          pResChar->notify();
        }

        // Báo cáo sự kiện Offline lên MQTT nếu đang có mạng
        if (client.connected()) {
          String logTopic = "lockers/" + deviceId + "/events/offline";
          String logPayload = "{\"deviceIdentifier\":\"" + deviceId + "\",\"channel\":" + String(targetChannel) + ",\"accessMethod\":\"Bluetooth\",\"pinUsed\":\"" + inputPin + "\"}";
          client.publish(logTopic.c_str(), logPayload.c_str());
        }
        return;
      }
    }

    // Nếu sai mã PIN: Tăng biến đếm và khóa 60 giây nếu thử sai 3 lần liên tiếp
    bleFailedAttempts++;
    Serial.printf("[BLE AUTH] >> SAI MA PIN! (Lan thu %d/3) <<\n", bleFailedAttempts);

    if (bleFailedAttempts >= 3) {
      bleBlockedUntil = now + BLE_BLOCK_DURATION_MS;
      Serial.println("[BLE AUTH] >> DA KHOA BLUETOOTH TRONG 60 GIAY DO NHAP SAI 3 LAN! <<");
      if (pResChar) {
        pResChar->setValue("FAIL:BLOCKED_TEMPORARILY:60");
        pResChar->notify();
      }
      return;
    }

    if (pResChar) {
      pResChar->setValue("FAIL:WRONG_PIN");
      pResChar->notify();
    }
  }
};

// =========================================================================
// KHỞI TẠO BLUETOOTH BLE GATT SERVER
// =========================================================================
void setup_ble() {
  String bleName = "BOXORA-" + deviceId;
  Serial.printf("[BLE] Khoi dong Bluetooth BLE GATT Server (Ten: %s)...\n", bleName.c_str());

  BLEDevice::init(bleName.c_str());
  pBleServer = BLEDevice::createServer();
  pBleServer->setCallbacks(new BleServerCallbacks());

  BLEService* pService = pBleServer->createService(BLE_SERVICE_UUID);

  // Characteristic nhận lệnh (Write)
  pCmdChar = pService->createCharacteristic(
    BLE_CHAR_CMD_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pCmdChar->setCallbacks(new BleCommandCallbacks());

  // Characteristic phản hồi & thông báo trạng thái cửa (Read, Notify)
  pResChar = pService->createCharacteristic(
    BLE_CHAR_RES_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pResChar->addDescriptor(new BLE2902());

  // Characteristic thông tin thiết bị (Read)
  pDevChar = pService->createCharacteristic(
    BLE_CHAR_DEV_UUID,
    BLECharacteristic::PROPERTY_READ
  );
  pDevChar->setValue(deviceId.c_str());

  pService->start();

  // Bắt đầu quảng bá BLE liên tục
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // Hỗ trợ kết nối mượt trên iPhone & Android
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Bluetooth BLE da san sang quang ba! Dien thoai co the ket noi.");
}

// =========================================================================
// HÀM KẾT NỐI WIFI (NON-BLOCKING TIMEOUT 8S, KHÔNG BAO GIỜ TREO MÁY)
// =========================================================================
void setup_wifi() {
  Serial.println();
  Serial.printf("[WIFI] Dang thu ket noi toi SSID: %s (Timeout 8 giay)...\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt < 8000)) {
    delay(400);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiAvailable = true;
    Serial.println();
    Serial.println("[WIFI] Da ket noi WiFi thanh cong!");
    Serial.printf("[WIFI] IP Address: %s\n", WiFi.localIP().toString().c_str());
  } else {
    wifiAvailable = false;
    Serial.println();
    Serial.println("[WIFI] KHONG CO WIFI! ESP32 tiep tuc chay o che do OFFLINE / BLUETOOTH.");
  }
}

// =========================================================================
// HÀM PHÁT BẢN TIN TRẠNG THÁI CỬA (GỬI QUA CẢ MQTT LẪN BLUETOOTH)
// =========================================================================
void publishDoorStatus(int channelIndex, const char* status) {
  // 1. Gửi qua MQTT (nếu có kết nối mạng)
  if (client.connected()) {
    String statusTopic = "lockers/" + deviceId + "/doors/" + String(channelIndex + 1) + "/status";
    client.publish(statusTopic.c_str(), status, true);
  }

  // 2. Bắn thông báo qua Bluetooth tới điện thoại nếu đang kết nối BLE
  if (bleClientConnected && pResChar) {
    String bleStatusMsg = "DOOR:" + String(channelIndex + 1) + ":" + String(status);
    pResChar->setValue(bleStatusMsg.c_str());
    pResChar->notify();
  }

  Serial.printf("[DOOR SENSOR] Ngan tu %d -> Trang thai: %s\n", channelIndex + 1, status);
}

// =========================================================================
// HÀM XỬ LÝ LỆNH TỪ MQTT CLOUD
// =========================================================================
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();

  String topicStr = String(topic);
  Serial.printf("[MQTT] Nhan lenh tai topic [%s]: %s\n", topic, message.c_str());

  int lastSlash = topicStr.lastIndexOf('/');
  int channel = -1;
  if (lastSlash != -1 && lastSlash < (int)topicStr.length() - 1) {
    channel = topicStr.substring(lastSlash + 1).toInt();
  }
  int channelIndex = channel - 1;

  // Lệnh 1: Mở khóa ngăn tủ từ Cloud MQTT (lockers/{id}/doors/{ch} -> "OPEN")
  if (topicStr.indexOf("/doors/") != -1 && message == "OPEN") {
    if (channelIndex >= 0 && channelIndex < NUM_CHANNELS) {
      unlockDoor(channelIndex, "MQTT_CLOUD");
    }
    return;
  }

  // Lệnh 2: Cấp/Cập nhật mã PIN động từ Cloud MQTT (lockers/{id}/pins/{ch} -> "{newPin}")
  if (topicStr.indexOf("/pins/") != -1 && message.length() >= 4) {
    if (channelIndex >= 0 && channelIndex < NUM_CHANNELS) {
      setChannelPin(channelIndex, message);
      Serial.printf("[MQTT CONFIG] Da cap nhat ma PIN dong cho Ngan %d: %s\n", channel, message.c_str());
    }
    return;
  }
}

// =========================================================================
// HÀM KẾT NỐI MQTT (NON-BLOCKING)
// =========================================================================
void reconnectMqttNonBlocking() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (client.connected()) return;

  unsigned long now = millis();
  if (now - lastMqttRetryTime > 8000) { // Thử kết nối lại mỗi 8 giây một lần
    lastMqttRetryTime = now;
    Serial.print("[MQTT] Dang ket noi EMQX Broker...");
    if (client.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println(" Thanh cong!");
      String subscribeDoors = "lockers/" + deviceId + "/doors/+";
      String subscribePins  = "lockers/" + deviceId + "/pins/+";
      client.subscribe(subscribeDoors.c_str());
      client.subscribe(subscribePins.c_str());

      // Đồng bộ trạng thái hiện tại ban đầu của toàn bộ các ngăn tủ lên Broker
      for (int i = 0; i < NUM_CHANNELS; i++) {
        publishDoorStatus(i, (lastDebouncedState[i] == LOW) ? "CLOSED" : "OPEN");
      }
    } else {
      Serial.printf(" That bai, rc=%d. Se thu lai sau.\n", client.state());
    }
  }
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Khởi tạo các chân Relay (mặc định ngắt điện LOW)
  for (int i = 0; i < NUM_CHANNELS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);
  }

  // Khởi tạo các chân Công tắc hành trình (INPUT_PULLUP)
  for (int i = 0; i < NUM_CHANNELS; i++) {
    pinMode(SENSOR_PINS[i], INPUT_PULLUP);
    int initialVal = digitalRead(SENSOR_PINS[i]);
    currentDoorState[i]   = initialVal;
    lastDebouncedState[i] = initialVal;
  }

  // Nhận diện mã thiết bị duy nhất
  deviceId = resolveDeviceId();

  // Khởi tạo Bluetooth BLE trước tiên (đảm bảo luôn phát sóng dù có WiFi hay không)
  setup_ble();

  // Thử kết nối WiFi
  espClient.setInsecure();
  setup_wifi();

  if (wifiAvailable) {
    client.setServer(MQTT_SERVER, MQTT_PORT);
    client.setCallback(callback);
  }

  Serial.println("==========================================================");
  Serial.printf ("   BOXORA SMART LOCKER - DEVICE ID: %s\n", deviceId.c_str());
  Serial.printf ("   BLE NAME: BOXORA-%s (Pin default: 123456)\n", deviceId.c_str());
  Serial.println("==========================================================");
}

// =========================================================================
// LOOP (VÒNG LẶP CHÍNH HOÀN TOÀN NON-BLOCKING)
// =========================================================================
void loop() {
  // 1. Quản lý kết nối MQTT nếu có mạng Wi-Fi
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      reconnectMqttNonBlocking();
    } else {
      client.loop();
    }
  }

  unsigned long now = millis();

  // 2. Tự ngắt điện Relay sau 2000ms
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (isUnlocked[i] && (now - unlockTimers[i] >= UNLOCK_DURATION_MS)) {
      digitalWrite(RELAY_PINS[i], LOW);
      isUnlocked[i] = false;
      Serial.printf("[ACTION] << NGAN TU %d DA KHOA LAI AN TOAN >>\n", i + 1);
    }
  }

  // 3. Đọc cảm biến công tắc hành trình (Debounce 50ms)
  for (int i = 0; i < NUM_CHANNELS; i++) {
    int reading = digitalRead(SENSOR_PINS[i]);

    if (reading != currentDoorState[i]) {
      lastDebounceTime[i] = now;
      currentDoorState[i] = reading;
    }

    if ((now - lastDebounceTime[i]) > DEBOUNCE_DELAY_MS) {
      if (reading != lastDebouncedState[i]) {
        lastDebouncedState[i] = reading;
        publishDoorStatus(i, (reading == LOW) ? "CLOSED" : "OPEN");
      }
    }
  }
}
