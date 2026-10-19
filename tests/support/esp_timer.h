#pragma once
#include <cstdint>
#include <vector>
constexpr int ESP_OK = 0;
using callback_t = void (*)(void *);
struct esp_timer_create_args_t {
  callback_t callback = nullptr;
  void *arg = nullptr;
  const char *name = nullptr;
};
struct TestTimer {
  esp_timer_create_args_t args;
  uint64_t due = 0;
  bool active = false;
};
using esp_timer_handle_t = TestTimer *;
namespace timer_test {
inline uint64_t now = 0;
inline bool failStart = false;
inline std::vector<TestTimer *> timers;
inline void advance(uint64_t micros) {
  now += micros;
  for (auto *t : timers)
    if (t->active && now >= t->due) {
      t->active = false;
      t->args.callback(t->args.arg);
    }
}
} // namespace timer_test
inline int64_t esp_timer_get_time() {
  return timer_test::now;
}
inline int esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out) {
  auto *t = new TestTimer{*args};
  timer_test::timers.push_back(t);
  *out = t;
  return ESP_OK;
}
inline int esp_timer_start_once(esp_timer_handle_t t, uint64_t delay) {
  if (timer_test::failStart)
    return -1;
  t->due = timer_test::now + delay;
  t->active = true;
  return ESP_OK;
}
