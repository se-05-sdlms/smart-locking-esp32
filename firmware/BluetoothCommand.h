#pragma once
#include "Protocol.h"
namespace locker {
// b2|HMAC-SHA256 hex|lệnh gốc. Khóa riêng từng tủ, không gửi cho điện thoại.
bool verifyBluetoothCommand(Text envelope, Text keyHex, std::string &command);
}
