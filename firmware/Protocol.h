#pragma once
#include "Config.h"
#include <array>
#include <string>
#include <string_view>
namespace locker {
using Text = std::string_view;
bool split(Text text, char delimiter, Text *fields, size_t count);
bool number(Text text, uint64_t &result);
bool digits(Text text, size_t size);
bool hexId(Text text);
bool printable(Text text, size_t maximum = 512);
bool deviceName(Text text);
std::string phoneNumber(Text text);
void copyText(char *destination, size_t size, Text text);
struct Workflow {
  Text subject, phone, pin, commandId;
  unsigned channel = 0;
  char kind = 0;
  uint64_t expires = 0;
};
bool parseWorkflow(Text text, Text device, Workflow &result);
bool parsePermit(Text text, Text device, uint64_t utc, std::string &phone);
bool parseFrame(Text frame, Text &requestId, Text &body);
struct Record {
  char id[33]{}, subject[33]{}, phone[11]{}, pin[7]{};
  uint8_t channel = 0;
  char kind = 0;
  uint64_t opened = 0, closed = 0, elapsed = 0, sequence = 0;
};
std::string encodeRecord(const Record &record, Text device);
} // namespace locker
