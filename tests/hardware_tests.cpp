#include "Hardware.h"
#include <iostream>
#include <stdexcept>
using namespace locker;
unsigned count = 0;
void check(bool ok, const char *m) {
  ++count;
  if (!ok)
    throw std::runtime_error(m);
}
int main() {
  try {
    for (int pin : RelayPins)
      gpio_test::values[pin] = HIGH;
    Hardware board;
    check(board.begin(), "timer initialization");
    for (int pin : RelayPins)
      check(gpio_test::values[pin] == LOW, "boot energized relay");
    check(board.door(0) == -1 && !board.pulse(0), "initial sensor must settle");
    timer_test::advance(49000);
    board.tick();
    check(!board.closed(0), "debounce before 50 ms");
    timer_test::advance(1000);
    board.tick();
    check(board.closed(0), "closed stable after 50 ms");
    check(board.pulse(0) && gpio_test::values[23] == HIGH, "relay ON");
    check(!board.pulse(0), "duplicate pulse extended relay");
    timer_test::advance(1999000);
    check(gpio_test::values[23] == HIGH, "relay stopped early");
    timer_test::advance(1000);
    check(gpio_test::values[23] == LOW, "timer must stop relay without main loop");
    gpio_test::values[32] = HIGH;
    board.tick();
    timer_test::advance(49000);
    board.tick();
    check(board.door(0) == LOW && !board.closed(0), "sensor bounce");
    gpio_test::values[32] = LOW;
    board.tick();
    timer_test::advance(50000);
    board.tick();
    check(board.door(0) == LOW, "short spike became open");
    gpio_test::values[32] = HIGH;
    board.tick();
    timer_test::advance(50000);
    board.tick();
    check(board.door(0) == HIGH && !board.pulse(0), "open door accepted pulse");
    gpio_test::values[32] = LOW;
    board.tick();
    timer_test::advance(50000);
    board.tick();
    timer_test::failStart = true;
    check(!board.pulse(0) && gpio_test::values[23] == LOW, "timer failure leaves relay high");
    check(!board.pulse(4) && board.door(4) == -1, "invalid channel");
    for (auto *timer : timer_test::timers)
      delete timer;
    std::cout << "PASS: " << count
              << " hardware adapter checks (GPIO, debounce, independent relay timer)\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
