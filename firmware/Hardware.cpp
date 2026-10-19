#include "Hardware.h"
#include <time.h>
namespace locker {
uint64_t monotonicMs() {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}
uint64_t utcSeconds() {
  time_t value = time(nullptr);
  return value >= 1704067200 ? static_cast<uint64_t>(value) : 0;
}
void Hardware::off(void *value) {
  auto *relay = static_cast<Relay *>(value);
  digitalWrite(relay->pin, LOW);
  relay->on.store(false);
}
bool Hardware::begin() {
  bool ready = true;
  // Đưa tất cả relay về LOW trước khi khởi tạo mạng hoặc đọc bộ nhớ.
  for (unsigned i = 0; i < Channels; ++i) {
    pinMode(RelayPins[i], OUTPUT);
    digitalWrite(RelayPins[i], LOW);
  }
  for (unsigned i = 0; i < Channels; ++i) {
    pinMode(SensorPins[i], INPUT_PULLUP);
    raw_[i] = digitalRead(SensorPins[i]);
    changed_[i] = monotonicMs();
    auto &relay = relays_[i];
    relay.pin = RelayPins[i];
    esp_timer_create_args_t args{};
    args.callback = off;
    args.arg = &relay;
    args.name = "locker-relay";
    if (esp_timer_create(&args, &relay.timer) != ESP_OK)
      ready = false;
  }
  return ready;
}
void Hardware::tick() {
  uint64_t now = monotonicMs();
  for (unsigned i = 0; i < Channels; ++i) {
    int reading = digitalRead(SensorPins[i]);
    if (reading != raw_[i]) {
      raw_[i] = reading;
      changed_[i] = now;
    }
    if (now - changed_[i] >= DebounceMs)
      stable_[i] = reading;
  }
}
bool Hardware::pulse(unsigned i) {
  if (i >= Channels || !closed(i) || !relays_[i].timer)
    return false;
  auto &relay = relays_[i];
  bool expected = false;
  if (!relay.on.compare_exchange_strong(expected, true))
    return false;
  digitalWrite(relay.pin, HIGH);
  if (esp_timer_start_once(relay.timer, RelayMicros) == ESP_OK)
    return true;
  off(&relay);
  return false;
}
int Hardware::door(unsigned i) const {
  return i < Channels ? stable_[i] : -1;
}
bool Hardware::closed(unsigned i) const {
  return i < Channels && stable_[i] == LOW && raw_[i] == LOW && digitalRead(SensorPins[i]) == LOW &&
         monotonicMs() - changed_[i] >= DebounceMs;
}
} // namespace locker
