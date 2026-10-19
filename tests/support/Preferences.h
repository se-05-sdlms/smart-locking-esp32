#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
namespace nvs_test {
inline bool beginOk = true, writeOk = true, present = false;
inline unsigned writes = 0;
inline std::vector<uint8_t> bytes;
inline void reset() {
  beginOk = writeOk = true;
  present = false;
  writes = 0;
  bytes.clear();
}
} // namespace nvs_test
class Preferences {
public:
  bool begin(const char *, bool) {
    return nvs_test::beginOk;
  }
  bool isKey(const char *) {
    return nvs_test::present;
  }
  size_t getBytesLength(const char *) {
    return nvs_test::bytes.size();
  }
  size_t getBytes(const char *, void *out, size_t n) {
    if (n != nvs_test::bytes.size())
      return 0;
    std::memcpy(out, nvs_test::bytes.data(), n);
    return n;
  }
  size_t putBytes(const char *, const void *in, size_t n) {
    ++nvs_test::writes;
    if (!nvs_test::writeOk)
      return 0;
    auto p = static_cast<const uint8_t *>(in);
    nvs_test::bytes.assign(p, p + n);
    nvs_test::present = true;
    return n;
  }
};
