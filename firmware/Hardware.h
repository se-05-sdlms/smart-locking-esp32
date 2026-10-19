#pragma once
#include "Config.h"
#include <Arduino.h>
#include <atomic>
#include <esp_timer.h>
namespace locker {
class Hardware {
public:
  bool begin();
  void tick();
  bool pulse(unsigned index);
  int door(unsigned index) const;
  bool closed(unsigned index) const;

private:
  struct Relay {
    int pin = -1;
    esp_timer_handle_t timer = nullptr;
    std::atomic<bool> on{false};
  } relays_[Channels];
  int raw_[Channels]{}, stable_[Channels] = {-1, -1, -1, -1};
  uint64_t changed_[Channels]{};
  static void off(void *relay);
};
uint64_t monotonicMs();
uint64_t utcSeconds();
} // namespace locker
