#pragma once
#include "LockerCore.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <atomic>
namespace locker {
struct Credentials {
  const char *ssid;
  const char *wifiPassword;
  const char *broker;
  uint16_t port;
  const char *user;
  const char *password;
};
struct Outbound {
  char suffix[40]{};
  char payload[513]{};
  bool retained = false;
};
class Transports {
public:
  bool begin(Text device, Credentials credentials);
  bool receive(Input &input);
  bool publish(Text suffix, Text payload, bool retained);
  void response(Text id, Text result);
  void doorNotice(unsigned channel, Text status);
  bool online() const {
    return online_.load();
  }
  uint32_t generation() const {
    return generation_.load();
  }
  bool enqueue(const Input &input);

private:
  QueueHandle_t incoming_ = nullptr, outgoing_ = nullptr;
  std::string device_;
  Credentials credentials_{};
  std::atomic<bool> online_{false};
  std::atomic<uint32_t> generation_{0};
  NimBLECharacteristic *response_ = nullptr;
  NimBLECharacteristic *doors_ = nullptr;
  WiFiClientSecure tls_;
  PubSubClient mqtt_{tls_};
  static Transports *instance_;
  static void networkEntry(void *self);
  static void mqttReceived(char *topic, uint8_t *payload, unsigned length);
  void networkLoop();
  void inbound(Text topic, Text payload);
};
} // namespace locker
