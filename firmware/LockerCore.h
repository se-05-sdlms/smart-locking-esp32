#pragma once
#include "Protocol.h"
namespace locker {
enum class Phase : uint8_t { Empty, Depositing, Stored, PickingUp };
struct Compartment {
  Phase phase = Phase::Empty;
  bool opened = false, returning = false;
  char subject[33]{}, eventId[33]{}, phone[11]{}, pin[7]{}, consumed[33]{};
  uint64_t started = 0, openedUtc = 0;
};
struct State {
  uint32_t version = StateVersion;
  bool bluetooth = false;
  uint8_t inventoryMask = 15;
  uint8_t commandCursor = 0;
  uint64_t sequence = 0;
  Compartment compartments[Channels]{};
  Record journal[JournalSlots]{};
  char commands[CommandSlots][33]{};
};
enum class LoadResult { Fresh, Loaded, Fault };
enum class InputKind : uint8_t { Ble, Workflow, Inventory, Pin, Ack, Remote, Legacy, Reset };
struct Input {
  InputKind kind;
  uint8_t channel = 0;
  char payload[513]{};
};
// Các cổng duy nhất chạm đến phần cứng, lưu trữ và truyền thông. Core không dùng Wi-Fi/BLE/EF.
class Ports {
public:
  virtual ~Ports() = default;
  virtual uint64_t monotonic() const = 0;
  virtual uint64_t utc() const = 0;
  virtual LoadResult load(State &) = 0;
  virtual bool save(const State &) = 0;
  virtual bool pulse(unsigned index) = 0;
  virtual int door(unsigned index) const = 0; // -1: chưa ổn định; 0: đóng; 1: mở.
  virtual bool closed(unsigned index) const = 0;
  virtual uint32_t random() = 0;
  virtual bool connected() const = 0;
  virtual uint32_t connectionGeneration() const = 0;
  virtual bool publish(Text suffix, Text payload, bool retained = false) = 0;
  virtual void respond(Text requestId, Text result) = 0;
  virtual void doorNotice(unsigned channel, Text status) = 0;
};
class LockerCore {
public:
  LockerCore(Ports &ports, Text device);
  void begin();
  void handle(const Input &input);
  void tick();
  bool ready() const {
    return ready_;
  }
  const State &state() const {
    return state_;
  }
  static bool validState(const State &state);

private:
  Ports &io_;
  std::string device_;
  State state_{};
  bool ready_ = false;
  int observed_[Channels] = {-1, -1, -1, -1}, queuedDoor_[Channels] = {-1, -1, -1, -1};
  bool warned_[Channels]{};
  uint8_t wrong_ = 0;
  uint64_t blocked_ = 0, lastReplay_ = 0;
  uint32_t generation_ = 0;
  std::string pendingRequest_;
  uint64_t pendingUntil_ = 0;
  bool persist();
  unsigned freeRecords() const;
  unsigned reservedRecords() const;
  bool unsent(unsigned index) const;
  std::string newId();
  std::string newPin();
  std::string workflow(Text command);
  std::string resetCompartment(Text command);
  std::string offlineReturn(Text body);
  std::string pickup(Text pin);
  std::string wrongPin();
  bool acknowledge(Text value);
  bool provision(Text value, std::string &acknowledgment);
  std::string status() const;
  void ble(Text frame);
};
} // namespace locker
