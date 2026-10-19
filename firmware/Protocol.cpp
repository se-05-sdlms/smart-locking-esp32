#include "Protocol.h"
#include <cstring>
#include <limits>
namespace locker {
bool split(Text s, char delimiter, Text *fields, size_t count) {
  size_t start = 0;
  for (size_t i = 0; i < count; ++i) {
    size_t end = s.find(delimiter, start);
    if (i + 1 == count) {
      if (end != Text::npos)
        return false;
      end = s.size();
    } else if (end == Text::npos)
      return false;
    fields[i] = s.substr(start, end - start);
    if (fields[i].empty())
      return false;
    start = end + 1;
  }
  return true;
}
bool number(Text s, uint64_t &result) {
  if (s.empty() || s.size() > 20)
    return false;
  uint64_t value = 0;
  for (char c : s) {
    if (c < '0' || c > '9' || value > (std::numeric_limits<uint64_t>::max() - (c - '0')) / 10)
      return false;
    value = value * 10 + c - '0';
  }
  result = value;
  return true;
}
bool digits(Text s, size_t n) {
  if (s.size() != n)
    return false;
  for (char c : s)
    if (c < '0' || c > '9')
      return false;
  return true;
}
bool hexId(Text s) {
  if (s.size() != 32)
    return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}
bool printable(Text s, size_t max) {
  if (s.empty() || s.size() > max)
    return false;
  for (unsigned char c : s)
    if (c < 32 || c > 126)
      return false;
  return true;
}
bool deviceName(Text s) {
  if (s.empty() || s.size() > 100)
    return false;
  for (char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
          c == '_'))
      return false;
  return true;
}
std::string phoneNumber(Text s) {
  std::string value(s);
  if (s.substr(0, 3) == "+84")
    value = "0" + std::string(s.substr(3));
  return digits(value, 10) && value[0] == '0' ? value : "";
}
void copyText(char *out, size_t size, Text s) {
  if (!size)
    return;
  size_t n = s.size() < size - 1 ? s.size() : size - 1;
  if (n)
    std::memcpy(out, s.data(), n);
  out[n] = 0;
}
bool parseWorkflow(Text s, Text device, Workflow &out) {
  Text f[9];
  uint64_t channel, expiry;
  if (!printable(s) || !split(s, '|', f, 9) || f[0] != "c1" || f[1] != device || !hexId(f[2]) ||
      !number(f[3], channel) || channel < 1 || channel > Channels || f[4].size() != 1 ||
      std::string("DPRG").find(f[4][0]) == std::string::npos || phoneNumber(f[5]).empty() ||
      (f[6] != "-" && !digits(f[6], 6)) || ((f[4] == "R" || f[4] == "G") && !digits(f[6], 6)) ||
      !number(f[7], expiry) || expiry > INT64_MAX || !hexId(f[8]))
    return false;
  out = {f[2], f[5], f[6], f[8], static_cast<unsigned>(channel), f[4][0], expiry};
  return true;
}
bool parsePermit(Text s, Text device, uint64_t utc, std::string &phone) {
  Text f[5];
  uint64_t expiry;
  if (!utc || !printable(s) || !split(s, '|', f, 5) || f[0] != "r1" ||
      (f[1] != "*" && f[1] != device) || !hexId(f[2]) || !number(f[4], expiry) || utc >= expiry)
    return false;
  phone = phoneNumber(f[3]);
  return !phone.empty();
}
bool parseFrame(Text frame, Text &id, Text &body) {
  if (!printable(frame, 400))
    return false;
  size_t separator = frame.find('|');
  uint64_t value;
  if (separator == Text::npos || separator < 1 || separator > 10 || separator + 1 == frame.size())
    return false;
  id = frame.substr(0, separator);
  body = frame.substr(separator + 1);
  return number(id, value) && value != 0;
}
std::string encodeRecord(const Record &e, Text device) {
  if (!e.id[0])
    return "";
  return "o1|" + std::string(e.id) + "|" + e.subject + "|" + std::string(device) + "|" +
         std::to_string(e.channel) + "|" + e.kind + "|" + e.phone + "|" +
         (e.kind == 'D' || e.kind == 'R' ? std::string(e.pin) : "-") + "|" +
         std::to_string(e.opened) + "|" + std::to_string(e.closed) + "|" +
         std::to_string(e.elapsed) + "|" + std::to_string(e.sequence);
}
} // namespace locker
