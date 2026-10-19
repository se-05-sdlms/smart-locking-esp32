#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
constexpr int LOW = 0, HIGH = 1, OUTPUT = 1, INPUT_PULLUP = 2;
namespace gpio_test {
inline int values[40]{};
inline unsigned writes = 0;
} // namespace gpio_test
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int value) {
  gpio_test::values[pin] = value;
  ++gpio_test::writes;
}
inline int digitalRead(int pin) {
  return gpio_test::values[pin];
}
