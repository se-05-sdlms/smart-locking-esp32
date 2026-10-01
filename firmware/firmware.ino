#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "secrets.h"

// =========================================================================
// HỆ THỐNG QUẢN LÝ GIAO NHẬN TỦ THÔNG MINH (SE-05 SDLMS)
// FIRMWARE ĐIỀU KHIỂN PHẦN CỨNG ESP32 (CHUẨN QUỐC TẾ PLUG & PLAY)
// =========================================================================

// Cấu hình chân Relay điều khiển các ngăn tủ (thẳng hàng song song đối diện: IN1->23, IN2->22, IN3->21, IN4->19)
#define NUM_CHANNELS 4
const int RELAY_PINS[NUM_CHANNELS] = {23, 22, 21, 19};

// Cấu hình chân Công tắc hành trình (Door Sensors) - hỗ trợ INPUT_PULLUP nội của ESP32
const int SENSOR_PINS[NUM_CHANNELS] = {32, 33, 25, 26};

// Thời gian kích mở chốt điện (2000ms = 2 giây, sau đó tự ngắt để bảo vệ cuộn hút)
const unsigned long UNLOCK_DURATION_MS = 2000;

// Biến quản lý thời gian đa nhiệm (non-blocking millis) cho từng ngăn tủ
unsigned long unlockTimers[NUM_CHANNELS] = {0, 0, 0, 0};
bool isUnlocked[NUM_CHANNELS]           = {false, false, false, false};

// Quản lý trạng thái cửa và chống rung phím (Debounce 50ms)
int currentDoorState[NUM_CHANNELS]          = {-1, -1, -1, -1};
int lastDebouncedState[NUM_CHANNELS]        = {-1, -1, -1, -1};
unsigned long lastDebounceTime[NUM_CHANNELS] = {0, 0, 0, 0};
const unsigned long DEBOUNCE_DELAY_MS        = 50;

// Định danh thiết bị (được tự động sinh từ MAC chip hoặc lấy từ secrets.h)
String deviceId = "";

WiFiClientSecure espClient;
PubSubClient client(espClient);

// =========================================================================
// HÀM TỰ ĐỘNG SINH DEVICE ID TỪ 6 KÝ TỰ CUỐI CỦA MAC ADDRESS (CHUẨN ARDUINO 100%)
// =========================================================================
String resolveDeviceId() {
  if (CUSTOM_DEVICE_ID != nullptr && strlen(CUSTOM_DEVICE_ID) > 0) {
    return String(CUSTOM_DEVICE_ID);
  }

  WiFi.mode(WIFI_STA);
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();

  // Lấy 6 ký tự cuối của MAC (ví dụ: MAC 20:50:0D:1A:08:44 -> Lấy "1A0844")
  if (mac.length() >= 6) {
    return "LKR-" + mac.substring(mac.length() - 6);
  }
  return "LKR-ESP32";
}

// =========================================================================
// HÀM KẾT NỐI WIFI
// =========================================================================
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.printf("[WIFI] Dang ket noi toi SSID: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("[WIFI] Da ket noi WiFi thanh cong!");
  Serial.printf("[WIFI] IP Address: %s\n", WiFi.localIP().toString().c_str());
}

// =========================================================================
// HÀM IN BANNER THÔNG TIN HỆ THỐNG KHI KHỞI ĐỘNG
// =========================================================================
void printBanner() {
  Serial.println();
  Serial.println("==========================================================");
  Serial.println("   SMART LOCKING SYSTEM (SE-05 SDLMS) - ESP32 FIRMWARE    ");
  Serial.println("==========================================================");
  Serial.printf ("   Hardware Chip MAC : %s\n", WiFi.macAddress().c_str());
  Serial.printf ("   DEVICE IDENTIFIER : %s\n", deviceId.c_str());
  Serial.printf ("   IP Address        : %s\n", WiFi.localIP().toString().c_str());
  Serial.printf ("   MQTT Broker       : %s:%d\n", MQTT_SERVER, MQTT_PORT);
  Serial.printf ("   MQTT Topic        : lockers/%s/doors/+\n", deviceId.c_str());
  Serial.println("   Status            : ONLINE & READY");
  Serial.println("==========================================================");
  Serial.println("   HUONG DAN CHO ADMIN WEB:");
  Serial.printf ("   -> Nhap ma thiet bi [%s] vao trang Web Admin.\n", deviceId.c_str());
  Serial.println("==========================================================");
  Serial.println();
}

// =========================================================================
// HÀM XỬ LÝ LỆNH TỪ MQTT (CALLBACK)
// Chuẩn topic nhận: lockers/{deviceId}/doors/{kênh}
// =========================================================================
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();

  String topicStr = String(topic);
  Serial.printf("[MQTT] Nhan lenh tai topic [%s]: %s\n", topic, message.c_str());

  if (message != "OPEN") {
    Serial.println("[MQTT] Bo qua lenh (chi chap nhan payload 'OPEN')");
    return;
  }

  // Tách số kênh ngăn tủ sau dấu gạch chéo cuối cùng (ví dụ: lockers/LKR-8C3B12/doors/1 -> channel = 1)
  int lastSlash = topicStr.lastIndexOf('/');
  int channel = -1;
  if (lastSlash != -1 && lastSlash < (int)topicStr.length() - 1) {
    channel = topicStr.substring(lastSlash + 1).toInt();
  }

  int channelIndex = channel - 1; // Đổi 1-indexed (1..4) sang 0-indexed (0..3)

  if (channelIndex >= 0 && channelIndex < NUM_CHANNELS) {
    digitalWrite(RELAY_PINS[channelIndex], HIGH);
    unlockTimers[channelIndex] = millis();
    isUnlocked[channelIndex]   = true;
    Serial.printf("[ACTION] >> KICH HOAT MO NGAN TU %d (GPIO %d) TRONG %lu ms <<\n",
                  channel, RELAY_PINS[channelIndex], UNLOCK_DURATION_MS);
  } else {
    Serial.printf("[WARNING] So ngan tu %d khong hop le (ho tro tu 1 den %d)\n", channel, NUM_CHANNELS);
  }
}

// =========================================================================
// HÀM PHÁT BẢN TIN TRẠNG THÁI CỬA (OPEN / CLOSED) LÊN MQTT
// Chuẩn topic: lockers/{deviceId}/doors/{kênh}/status
// =========================================================================
void publishDoorStatus(int channelIndex, const char* status) {
  String statusTopic = "lockers/" + deviceId + "/doors/" + String(channelIndex + 1) + "/status";
  // Gửi với retain = true để Server/Web vừa kết nối là biết ngay trạng thái hiện tại
  client.publish(statusTopic.c_str(), status, true);
  Serial.printf("[DOOR SENSOR] Ngan tu %d -> Trang thai: %s (Topic: %s)\n",
                channelIndex + 1, status, statusTopic.c_str());
}

// =========================================================================
// HÀM KẾT NỐI MQTT & ĐĂNG KÝ TOPIC DUY NHẤT
// =========================================================================
void reconnect() {
  while (!client.connected()) {
    Serial.print("[MQTT] Dang ket noi toi EMQX Broker...");

    // Kết nối với ClientID là deviceId
    if (client.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println(" Thanh cong!");

      // ĐĂNG KÝ 1 TOPIC CHUẨN DUY NHẤT: lockers/{deviceId}/doors/+
      String subscribeTopic = "lockers/" + deviceId + "/doors/+";
      client.subscribe(subscribeTopic.c_str());
      Serial.printf("[MQTT] Da dang ky lang nghe: %s\n", subscribeTopic.c_str());

      printBanner();

      // Đồng bộ trạng thái hiện tại ban đầu của toàn bộ các ngăn tủ lên Broker
      for (int i = 0; i < NUM_CHANNELS; i++) {
        publishDoorStatus(i, (lastDebouncedState[i] == LOW) ? "CLOSED" : "OPEN");
      }
    } else {
      Serial.printf(" That bai, rc=%d. Thu lai sau 5 giay...\n", client.state());
      delay(5000);
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

  // Khởi tạo các chân Công tắc hành trình (kích hoạt điện trở kéo lên nội INPUT_PULLUP)
  for (int i = 0; i < NUM_CHANNELS; i++) {
    pinMode(SENSOR_PINS[i], INPUT_PULLUP);
    int initialVal = digitalRead(SENSOR_PINS[i]);
    currentDoorState[i]   = initialVal;
    lastDebouncedState[i] = initialVal;
  }

  // Bỏ qua xác thực chứng chỉ TLS/SSL trên EMQX port 8883
  espClient.setInsecure();

  setup_wifi();

  // Nhận diện mã thiết bị duy nhất sau khi WiFi đã được khởi tạo
  deviceId = resolveDeviceId();

  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
}

// =========================================================================
// VÒNG LẶP CHÍNH (LOOP) - NON-BLOCKING MILLIS CHO TOÀN BỘ CÁC CỬA
// =========================================================================
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();

  // 1. Kiểm tra thời gian kích mở chốt của từng cửa (tự ngắt relay sau 2s)
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (isUnlocked[i] && (now - unlockTimers[i] >= UNLOCK_DURATION_MS)) {
      digitalWrite(RELAY_PINS[i], LOW); // Tự động ngắt điện relay
      isUnlocked[i] = false;
      Serial.printf("[ACTION] << NGAN TU %d DA DONG VA KHOA LAI AN TOAN >>\n", i + 1);
    }
  }

  // 2. Đọc cảm biến công tắc hành trình với thuật toán chống dội phím (Debounce)
  for (int i = 0; i < NUM_CHANNELS; i++) {
    int reading = digitalRead(SENSOR_PINS[i]);

    // Nếu phát hiện tín hiệu thay đổi mức logic
    if (reading != currentDoorState[i]) {
      lastDebounceTime[i] = now;
      currentDoorState[i] = reading;
    }

    // Nếu tín hiệu ổn định vượt quá thời gian debounce (50ms)
    if ((now - lastDebounceTime[i]) > DEBOUNCE_DELAY_MS) {
      if (reading != lastDebouncedState[i]) {
        lastDebouncedState[i] = reading;
        // LOW = lẫy bị đè (Cửa đóng) | HIGH = lẫy nhả ra (Cửa mở)
        publishDoorStatus(i, (reading == LOW) ? "CLOSED" : "OPEN");
      }
    }
  }
}
