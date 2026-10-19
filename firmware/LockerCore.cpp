#include "LockerCore.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
namespace locker {
namespace {
bool terminated(const char *s, size_t n) {
  return std::memchr(s, 0, n) != nullptr;
}
bool active(Phase p) {
  return p == Phase::Depositing || p == Phase::PickingUp;
}
} // namespace
LockerCore::LockerCore(Ports &ports, Text device) : io_(ports), device_(device) {}
bool LockerCore::validState(const State &s) {
  if (s.version != StateVersion || s.inventoryMask > 15 || s.sequence > INT64_MAX)
    return false;
  for (const auto &c : s.compartments) {
    if (static_cast<unsigned>(c.phase) > 3 || !terminated(c.subject, 33) ||
        !terminated(c.eventId, 33) || !terminated(c.phone, 11) || !terminated(c.pin, 7) ||
        !terminated(c.consumed, 33))
      return false;
    if (c.consumed[0] && !hexId(c.consumed))
      return false;
    if (c.phase != Phase::Empty &&
        (!hexId(c.subject) || phoneNumber(c.phone).empty() || !digits(c.pin, 6)))
      return false;
    if (active(c.phase) && !hexId(c.eventId))
      return false;
  }
  for (unsigned i = 0; i < JournalSlots; ++i) {
    const auto &e = s.journal[i];
    if (!e.id[0])
      continue;
    if (!terminated(e.id, 33) || !terminated(e.subject, 33) || !terminated(e.phone, 11) ||
        !terminated(e.pin, 7) || !hexId(e.id) || !hexId(e.subject) ||
        phoneNumber(e.phone).empty() || !digits(e.pin, 6) || e.channel < 1 ||
        e.channel > Channels || std::string("DPRG").find(e.kind) == std::string::npos ||
        !e.sequence || e.sequence > s.sequence || e.elapsed < 50 || e.elapsed > INT64_MAX ||
        e.opened > INT64_MAX || e.closed > INT64_MAX ||
        (e.opened && e.closed && e.closed < e.opened))
      return false;
    for (unsigned j = 0; j < i; ++j)
      if (s.journal[j].id[0] &&
          (e.sequence == s.journal[j].sequence || std::strcmp(e.id, s.journal[j].id) == 0))
        return false;
  }
  for (const auto &command : s.commands)
    if (!terminated(command, 33) || (command[0] && !hexId(command)))
      return false;
  return true;
}
void LockerCore::begin() {
  if (!deviceName(device_))
    return;
  LoadResult result = io_.load(state_);
  if (result == LoadResult::Fault)
    return;
  if (result == LoadResult::Fresh) {
    state_ = State{};
    ready_ = true;
    persist();
    return;
  }
  ready_ = validState(state_);
  if (ready_)
    for (auto &c : state_.compartments)
      if (active(c.phase))
        c.started = io_.monotonic();
}
bool LockerCore::persist() {
  if (!ready_ || !io_.save(state_)) {
    ready_ = false;
    return false;
  }
  return true;
}
unsigned LockerCore::freeRecords() const {
  unsigned n = 0;
  for (const auto &e : state_.journal)
    if (!e.id[0])
      ++n;
  return n;
}
unsigned LockerCore::reservedRecords() const {
  unsigned n = 0;
  for (const auto &c : state_.compartments)
    n += c.phase == Phase::Depositing ? 2 : c.phase == Phase::Empty ? 0 : 1;
  return n;
}
bool LockerCore::unsent(unsigned index) const {
  for (const auto &e : state_.journal) {
    if (e.id[0] && e.channel == index + 1)
      return true;
  }
  return false;
}
std::string LockerCore::newId() {
  char out[33];
  for (unsigned i = 0; i < 4; ++i)
    std::snprintf(out + 8 * i, 9, "%08lx", static_cast<unsigned long>(io_.random()));
  return out;
}
std::string LockerCore::newPin() {
  for (;;) {
    uint32_t value = io_.random();
    if (value >= 4294000000U)
      continue;
    char pin[7];
    std::snprintf(pin, sizeof(pin), "%06lu", static_cast<unsigned long>(value % 1000000));
    bool duplicate = false;
    for (const auto &c : state_.compartments)
      if (c.phase != Phase::Empty && Text(c.pin) == pin)
        duplicate = true;
    if (!duplicate)
      return pin;
  }
}
std::string LockerCore::workflow(Text payload) {
  Workflow w;
  if (!parseWorkflow(payload, device_, w))
    return "FAIL:COMMAND";
  if (!io_.utc())
    return "FAIL:CLOCK_NOT_READY";
  if (io_.utc() >= w.expires)
    return "FAIL:EXPIRED";
  for (const auto &id : state_.commands)
    if (w.commandId == id) {
      std::string pin;
      const auto &current = state_.compartments[w.channel - 1];
      if (current.phase != Phase::Empty && w.subject == current.subject)
        pin = current.pin;
      else
        for (const auto &e : state_.journal)
          if (e.id[0] && w.subject == e.subject && e.kind == w.kind) {
            pin = e.pin;
            break;
          }
      return "ALREADY_ACCEPTED:" + std::string(w.subject) + (pin.empty() ? "" : ":" + pin);
    }
  unsigned index = w.channel - 1;
  auto &c = state_.compartments[index];
  bool deposit = w.kind == 'D' || w.kind == 'R', returning = w.kind == 'R' || w.kind == 'G';
  if (w.subject == c.consumed)
    return "FAIL:ALREADY_USED";
  if (deposit && c.phase != Phase::Empty && w.subject == c.subject && c.returning == returning)
    return "ALREADY_ACCEPTED:" + std::string(w.subject) + ":" + c.pin;
  if (!io_.closed(index))
    return "FAIL:DOOR_OPEN";
  if (deposit && (c.phase != Phase::Empty || unsent(index)))
    return "FAIL:CHANNEL_BUSY";
  if (!deposit && c.phase != Phase::Empty &&
      (c.phase != Phase::Stored || w.subject != c.subject || c.returning != returning))
    return "FAIL:WRONG_TRANSACTION";
  if (!deposit && c.phase == Phase::Empty && unsent(index))
    return "FAIL:SYNC_REQUIRED";
  unsigned needed = deposit ? 2 : c.phase == Phase::Empty ? 1 : 0;
  if (freeRecords() < reservedRecords() + needed || state_.sequence == INT64_MAX)
    return "FAIL:SYNC_REQUIRED";
  std::string pin = w.pin != "-"               ? std::string(w.pin)
                    : c.phase == Phase::Stored ? std::string(c.pin)
                                               : newPin();
  for (unsigned i = 0; i < Channels; ++i)
    if (i != index && state_.compartments[i].phase != Phase::Empty &&
        Text(state_.compartments[i].pin) == pin)
      return "FAIL:PIN_CONFLICT";
  Compartment before = c;
  auto cursor = state_.commandCursor;
  unsigned slot = cursor % CommandSlots;
  char old[33];
  std::memcpy(old, state_.commands[slot], 33);
  c.phase = deposit ? Phase::Depositing : Phase::PickingUp;
  c.returning = returning;
  c.opened = false;
  c.started = io_.monotonic();
  c.openedUtc = 0;
  warned_[index] = false;
  copyText(c.subject, 33, w.subject);
  copyText(c.phone, 11, phoneNumber(w.phone));
  copyText(c.pin, 7, pin);
  copyText(c.eventId, 33, newId());
  copyText(state_.commands[slot], 33, w.commandId);
  ++state_.commandCursor;
  if (!persist())
    return "FAIL:STORAGE";
  if (!io_.pulse(index)) {
    c = before;
    state_.commandCursor = cursor;
    std::memcpy(state_.commands[slot], old, 33);
    persist();
    return "FAIL:RELAY_TIMER";
  }
  return "WORKFLOW_OK:" + std::to_string(w.channel) + ":" + std::string(w.subject) + ":" + pin;
}
std::string LockerCore::offlineReturn(Text body) {
  Text f[2];
  std::string phone;
  if (!split(body, ':', f, 2) || !hexId(f[1]) || !parsePermit(f[0], device_, io_.utc(), phone))
    return "FAIL:RESIDENT_PERMISSION";
  if (io_.connected())
    return "FAIL:USE_PREPARE";
  for (const auto &c : state_.compartments) {
    if (f[1] == c.consumed)
      return "FAIL:ALREADY_USED";
    if (c.phase != Phase::Empty && f[1] == c.subject) {
      if (!c.returning || phone != c.phone || c.phase == Phase::PickingUp)
        return "FAIL:WRONG_TRANSACTION";
      return "ALREADY_ACCEPTED:" + std::string(f[1]) + ":" + c.pin;
    }
  }
  if (freeRecords() < reservedRecords() + 2 || state_.sequence == INT64_MAX)
    return "FAIL:SYNC_REQUIRED";
  for (unsigned i = 0; i < Channels; ++i) {
    auto &c = state_.compartments[i];
    if (c.phase != Phase::Empty || (state_.inventoryMask & (1 << i)) || unsent(i) || !io_.closed(i))
      continue;
    Compartment before = c;
    c.phase = Phase::Depositing;
    c.returning = true;
    c.opened = false;
    copyText(c.subject, 33, f[1]);
    copyText(c.phone, 11, phone);
    copyText(c.pin, 7, newPin());
    copyText(c.eventId, 33, newId());
    c.started = io_.monotonic();
    c.openedUtc = 0;
    warned_[i] = false;
    if (!persist())
      return "FAIL:STORAGE";
    if (!io_.pulse(i)) {
      c = before;
      persist();
      return "FAIL:RELAY_TIMER";
    }
    return "RETURN_OK:" + std::to_string(i + 1) + ":" + c.subject + ":" + c.pin;
  }
  return "FAIL:NO_COMPARTMENT";
}
std::string LockerCore::wrongPin() {
  if (++wrong_ >= 3) {
    wrong_ = 0;
    blocked_ = io_.monotonic() + 60000;
    return "FAIL:LOCKED:60";
  }
  return "FAIL:WRONG_PIN";
}
std::string LockerCore::pickup(Text pin) {
  if (!digits(pin, 6))
    return wrongPin();
  for (unsigned i = 0; i < Channels; ++i) {
    auto &c = state_.compartments[i];
    if (c.phase != Phase::Stored || c.returning || pin != c.pin)
      continue;
    if (!io_.closed(i))
      return "FAIL:CHANNEL_BUSY_OR_OPEN";
    if (freeRecords() < reservedRecords() || state_.sequence == INT64_MAX)
      return "FAIL:SYNC_REQUIRED";
    Compartment before = c;
    c.phase = Phase::PickingUp;
    c.opened = false;
    c.started = io_.monotonic();
    c.openedUtc = 0;
    warned_[i] = false;
    copyText(c.eventId, 33, newId());
    if (!persist())
      return "FAIL:STORAGE";
    if (!io_.pulse(i)) {
      c = before;
      persist();
      return "FAIL:RELAY_TIMER";
    }
    wrong_ = 0;
    return "PICKUP_OK:" + std::to_string(i + 1) + ":" + c.subject;
  }
  return wrongPin();
}
bool LockerCore::acknowledge(Text payload) {
  Text f[2];
  if (!split(payload, '|', f, 2) || f[0] != "k1" || !hexId(f[1]))
    return false;
  for (auto &e : state_.journal)
    if (f[1] == e.id) {
      e = Record{};
      return persist();
    }
  return true; // ACK từ app và MQTT có thể đến lặp.
}
bool LockerCore::provision(Text payload, std::string &ack) {
  Text f[4];
  uint64_t channel;
  if (!split(payload, ':', f, 4) || !number(f[0], channel) || channel < 1 || channel > Channels ||
      !hexId(f[1]) || phoneNumber(f[2]).empty() || !digits(f[3], 6))
    return false;
  auto &c = state_.compartments[channel - 1];
  if (!(c.phase == Phase::Stored && !c.returning && f[1] == c.subject && f[3] == c.pin &&
        phoneNumber(f[2]) == c.phone)) {
    if (c.phase != Phase::Empty || f[1] == c.consumed || unsent(channel - 1) ||
        !io_.closed(channel - 1) || freeRecords() < reservedRecords() + 1)
      return false;
    for (const auto &other : state_.compartments)
      if (other.phase != Phase::Empty && f[3] == other.pin)
        return false;
    c.phase = Phase::Stored;
    c.returning = false;
    copyText(c.subject, 33, f[1]);
    copyText(c.phone, 11, phoneNumber(f[2]));
    copyText(c.pin, 7, f[3]);
    if (!persist())
      return false;
  }
  ack = "q1|" + std::string(f[1]) + "|" + std::string(f[3]);
  return true;
}
std::string LockerCore::status() const {
  std::string out = io_.connected() ? "STATUS:ONLINE:" : "STATUS:OFFLINE:";
  for (unsigned i = 0; i < Channels; ++i) {
    const auto &c = state_.compartments[i];
    bool pending = unsent(i), managed = state_.inventoryMask & (1 << i);
    const char *reason = c.phase == Phase::Depositing  ? "DEPOSITING"
                         : c.phase == Phase::Stored    ? "OCCUPIED"
                         : c.phase == Phase::PickingUp ? "PICKING_UP"
                         : pending                     ? "WAIT_SYNC"
                         : managed                     ? "UNCONFIRMED"
                         : io_.closed(i)               ? "READY"
                                                       : "DOOR_OPEN";
    out += std::to_string(i + 1) + ":" +
           (c.phase != Phase::Empty || pending || managed ? "BUSY" : "EMPTY") + ":" +
           (io_.door(i) < 0 ? "UNKNOWN"
            : io_.closed(i) ? "CLOSED"
                            : "OPEN") +
           ":" + reason + ",";
  }
  return out;
}
void LockerCore::ble(Text frame) {
  Text id, body;
  if (!parseFrame(frame, id, body))
    return;
  if (body == "STATUS") {
    io_.respond(id, status());
    return;
  }
  if (!ready_) {
    io_.respond(id, "FAIL:NOT_CONFIGURED_OR_STORAGE");
    return;
  }
  if (body.substr(0, 4) == "ACK:") {
    io_.respond(id, acknowledge(body.substr(4)) ? "ACKED" : "FAIL:ACK");
    return;
  }
  if (io_.monotonic() < blocked_) {
    io_.respond(id, "FAIL:LOCKED:" + std::to_string((blocked_ - io_.monotonic() + 999) / 1000));
    return;
  }
  if (!state_.bluetooth) {
    io_.respond(id, "FAIL:BLUETOOTH_DISABLED");
    return;
  }
  std::string result;
  if (body.substr(0, 5) == "EXEC:")
    result = workflow(body.substr(5));
  else if (body.substr(0, 7) == "RETURN:")
    result = offlineReturn(body.substr(7));
  else if (body.substr(0, 7) == "PICKUP:")
    result = pickup(body.substr(7));
  else if (body.substr(0, 8) == "PREPARE:") {
    Text f[3];
    std::string phone;
    if (!split(body.substr(8), ':', f, 3) || !parsePermit(f[0], device_, io_.utc(), phone) ||
        (f[1] != "N" && f[1] != "R" && f[1] != "P") || !hexId(f[2]))
      result = "FAIL:REQUEST";
    else if (!io_.connected())
      result = "FAIL:NETWORK_OFFLINE";
    else if (!pendingRequest_.empty())
      result = "FAIL:REQUEST_PENDING";
    else if (!io_.publish("workflow/request", std::string(id) + ":" + std::string(f[1]) + ":" +
                                                  std::string(f[2]) + ":" + std::string(f[0])))
      result = "FAIL:QUEUE_FULL";
    else {
      pendingRequest_ = id;
      pendingUntil_ = io_.monotonic() + 60000;
      result = "PENDING";
    }
  } else if (body == "LIST" || body.substr(0, 5) == "LIST:") {
    uint64_t offset = 0;
    if (body != "LIST" && (!number(body.substr(5), offset) || offset >= JournalSlots))
      result = "FAIL:INDEX";
    else {
      result = "LIST:";
      for (size_t i = offset; i < JournalSlots && i < offset + 8; ++i)
        if (state_.journal[i].id[0])
          result += std::to_string(i) + ":" + state_.journal[i].subject + ",";
    }
  } else if (body.substr(0, 8) == "RECEIPT:") {
    Text f[2];
    uint64_t index;
    if (!split(body.substr(8), ':', f, 2) || !number(f[0], index) || index >= JournalSlots)
      result = "FAIL:INDEX";
    else if (!state_.journal[index].id[0])
      result = "EMPTY";
    else if (!digits(f[1], 6) || f[1] != state_.journal[index].pin)
      result = wrongPin();
    else {
      wrong_ = 0;
      result = encodeRecord(state_.journal[index], device_);
    }
  } else
    result = "FAIL:UNSUPPORTED";
  io_.respond(id, result);
}
void LockerCore::handle(const Input &input) {
  if (!terminated(input.payload, sizeof(input.payload)))
    return;
  Text body(input.payload);
  if (input.kind == InputKind::Ble) {
    ble(body);
    return;
  }
  if (!ready_ || !printable(body))
    return;
  switch (input.kind) {
  case InputKind::Workflow:
    workflow(body);
    break;
  case InputKind::Inventory: {
    Text f[2];
    uint64_t enabled, mask;
    if (split(body, ':', f, 2) && number(f[0], enabled) && enabled <= 1 && number(f[1], mask) &&
        mask <= 15 && (state_.bluetooth != (enabled == 1) || state_.inventoryMask != mask)) {
      state_.bluetooth = enabled == 1;
      state_.inventoryMask = static_cast<uint8_t>(mask);
      persist();
    }
    break;
  }
  case InputKind::Pin: {
    std::string ack;
    if (provision(body, ack))
      io_.publish("events/pin-ready", ack);
    break;
  }
  case InputKind::Ack:
    acknowledge(body);
    break;
  case InputKind::Remote: {
    size_t sep = body.find('|');
    if (sep == Text::npos || pendingRequest_.empty() || body.substr(0, sep) != pendingRequest_)
      break;
    std::string id = pendingRequest_;
    pendingRequest_.clear();
    if (io_.monotonic() >= pendingUntil_) {
      io_.respond(id, "FAIL:NETWORK_TIMEOUT");
      break;
    }
    Text reply = body.substr(sep + 1);
    io_.respond(id, reply.substr(0, 5) == "FAIL:" ? std::string(reply) : workflow(reply));
    break;
  }
  case InputKind::Legacy: {
    if (input.channel < 1 || input.channel > Channels || !io_.closed(input.channel - 1))
      break;
    auto &c = state_.compartments[input.channel - 1];
    if (body == "OPEN:OPERATOR") {
      io_.pulse(input.channel - 1);
      break;
    }
    if (body.substr(0, 5) != "OPEN:" && body != "OPEN")
      break;
    if (c.phase == Phase::Stored && !c.returning && body.size() > 5 && body.substr(5) == c.subject)
      pickup(c.pin);
    else if (c.phase == Phase::Empty && !unsent(input.channel - 1)) {
      state_.inventoryMask |= 1 << (input.channel - 1);
      if (persist())
        io_.pulse(input.channel - 1);
    }
    break;
  }
  default:
    break;
  }
}
void LockerCore::tick() {
  uint64_t now = io_.monotonic();
  if (!pendingRequest_.empty() && now >= pendingUntil_) {
    io_.respond(pendingRequest_, "FAIL:NETWORK_TIMEOUT");
    pendingRequest_.clear();
  }
  uint32_t generation = io_.connectionGeneration();
  if (generation != generation_) {
    generation_ = generation;
    std::fill(std::begin(queuedDoor_), std::end(queuedDoor_), -1);
  }
  for (unsigned i = 0; i < Channels; ++i) {
    int reading = io_.door(i);
    if (reading < 0)
      continue;
    if (reading != observed_[i]) {
      observed_[i] = reading;
      io_.doorNotice(i + 1, reading ? "OPEN" : "CLOSED");
    }
    if (io_.connected() && reading != queuedDoor_[i] &&
        io_.publish("doors/" + std::to_string(i + 1) + "/status", reading ? "OPEN" : "CLOSED",
                    true))
      queuedDoor_[i] = reading;
    if (!ready_)
      continue;
    auto &c = state_.compartments[i];
    if (!active(c.phase))
      continue;
    // Thời gian chờ chỉ dùng để báo lỗi, không tự xác nhận giao dịch hoặc giải phóng ngăn.
    uint64_t elapsed = now >= c.started ? now - c.started : 0;
    if (!warned_[i] && elapsed >= (c.opened ? 120000 : 60000)) {
      warned_[i] = true;
      io_.doorNotice(i + 1, c.opened ? "OPEN_TOO_LONG" : "WAIT_OPEN_TIMEOUT");
    }
    if (reading == 1 && !c.opened) {
      c.opened = true;
      c.openedUtc = io_.utc();
      persist();
    } else if (reading == 0 && c.opened) {
      for (auto &e : state_.journal)
        if (!e.id[0]) {
          e = Record{};
          copyText(e.id, 33, c.eventId);
          copyText(e.subject, 33, c.subject);
          copyText(e.phone, 11, c.phone);
          copyText(e.pin, 7, c.pin);
          e.channel = i + 1;
          e.kind = c.returning ? (c.phase == Phase::Depositing ? 'R' : 'G')
                               : (c.phase == Phase::Depositing ? 'D' : 'P');
          e.opened = c.openedUtc;
          e.closed = io_.utc();
          if (e.closed && e.opened > e.closed)
            e.closed = 0;
          e.elapsed = std::max<uint64_t>(50, elapsed);
          e.sequence = ++state_.sequence;
          if (c.phase == Phase::Depositing)
            c.phase = Phase::Stored;
          else {
            std::string consumed = c.subject;
            c = Compartment{};
            copyText(c.consumed, 33, consumed);
          }
          persist();
          break;
        }
    }
  }
  if (ready_ && io_.connected() && now - lastReplay_ >= 3000) {
    lastReplay_ = now;
    for (const auto &e : state_.journal)
      if (e.id[0] && !io_.publish("events/offline", encodeRecord(e, device_)))
        break;
  }
}
} // namespace locker
