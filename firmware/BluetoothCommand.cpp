#include "BluetoothCommand.h"
#include <array>
#ifdef ARDUINO
#include <mbedtls/md.h>
#else
#include <openssl/hmac.h>
#endif
namespace locker {
namespace {
int hex(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}
bool decode(Text input, std::array<unsigned char, 32> &output) {
  if (input.size() != 64) return false;
  for (size_t i = 0; i < output.size(); ++i) {
    int high = hex(input[i * 2]), low = hex(input[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[i] = static_cast<unsigned char>((high << 4) | low);
  }
  return true;
}
}
bool verifyBluetoothCommand(Text envelope, Text keyHex, std::string &command) {
  command.clear();
  if (envelope.size() < 69 || envelope.substr(0, 3) != "b2|" || envelope[67] != '|') return false;
  const Text body = envelope.substr(68);
  if (body.empty() || body.size() > 300 || !printable(body)) return false;
  std::array<unsigned char, 32> key{}, supplied{}, expected{};
  if (!decode(keyHex, key) || !decode(envelope.substr(3, 64), supplied)) return false;
  const std::string message = "boxora-ble-v2|" + std::string(body);
#ifdef ARDUINO
  const auto *algorithm = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!algorithm || mbedtls_md_hmac(algorithm, key.data(), key.size(),
      reinterpret_cast<const unsigned char *>(message.data()), message.size(), expected.data()) != 0) return false;
#else
  unsigned size = 0;
  if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
      reinterpret_cast<const unsigned char *>(message.data()), message.size(), expected.data(), &size) || size != 32) return false;
#endif
  unsigned difference = 0;
  for (size_t i = 0; i < expected.size(); ++i) difference |= expected[i] ^ supplied[i];
  if (difference) return false;
  command.assign(body);
  return true;
}
}
