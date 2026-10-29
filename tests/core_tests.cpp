#include "LockerCore.h"
#include "QueuePump.h"
#include "StateStore.h"
#include "BluetoothCommand.h"
#include <openssl/hmac.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
using namespace locker;
namespace {
unsigned checks = 0;
void check(bool ok, const char *message) {
  ++checks;
  if (!ok)
    throw std::runtime_error(message);
}
const std::string Subject = "11111111111111111111111111111111",
                  Other = "22222222222222222222222222222222";
struct Fake : Ports {
  uint64_t now = 1000, epoch = 1792440000;
  bool online = false, saveOk = true, pulseOk = true, sendOk = true;
  uint32_t generation = 0;
  unsigned pulses = 0, writes = 0;
  int doors[4] = {0, 0, 0, 0};
  LoadResult loaded = LoadResult::Fresh;
  State saved{};
  std::mt19937 rng{42};
  std::string response;
  std::vector<std::pair<std::string, std::string>> messages, notices;
  uint64_t monotonic() const override {
    return now;
  }
  uint64_t utc() const override {
    return epoch ? epoch + now / 1000 : 0;
  }
  LoadResult load(State &s) override {
    if (loaded == LoadResult::Loaded)
      s = saved;
    return loaded;
  }
  bool save(const State &s) override {
    ++writes;
    if (!saveOk)
      return false;
    saved = s;
    return true;
  }
  bool pulse(unsigned i) override {
    if (!pulseOk || doors[i] != 0)
      return false;
    ++pulses;
    return true;
  }
  int door(unsigned i) const override {
    return doors[i];
  }
  bool closed(unsigned i) const override {
    return doors[i] == 0;
  }
  uint32_t random() override {
    return rng();
  }
  bool connected() const override {
    return online;
  }
  Text commandSigningKey() const override { return "1111111111111111111111111111111111111111111111111111111111111111"; }
  uint32_t connectionGeneration() const override {
    return generation;
  }
  bool publish(Text t, Text p, bool = false) override {
    if (!sendOk)
      return false;
    messages.emplace_back(t, p);
    return true;
  }
  void respond(Text, Text value) override {
    response = value;
  }
  void doorNotice(unsigned ch, Text value) override {
    notices.emplace_back(std::to_string(ch), value);
  }
};
void input(LockerCore &c, InputKind kind, const std::string &payload, unsigned channel = 0) {
  Input m{};
  m.kind = kind;
  m.channel = channel;
  copyText(m.payload, sizeof(m.payload), payload);
  c.handle(m);
}
std::string ble(LockerCore &c, Fake &f, const std::string &body) {
  f.response.clear();
  std::string frame = body;
  const size_t prefix = body.substr(0, 5) == "EXEC:" ? 5 : body.substr(0, 6) == "RESET:" ? 6 : 0;
  if (prefix && body.substr(prefix, 3) != "b2|") {
    unsigned char key[32], tag[32]; std::fill_n(key, 32, 0x11); unsigned size = 0;
    const std::string message = "boxora-ble-v2|" + body.substr(prefix);
    HMAC(EVP_sha256(), key, 32, reinterpret_cast<const unsigned char *>(message.data()), message.size(), tag, &size);
    std::string signature;
    const char *hex = "0123456789abcdef";
    for (unsigned i=0;i<size;++i) {signature += hex[tag[i] >> 4];signature += hex[tag[i] & 15];}
    frame = body.substr(0,prefix) + "b2|" + signature + "|" + body.substr(prefix);
  }
  input(c, InputKind::Ble, "1|" + frame);
  return f.response;
}
std::string command(char kind, const std::string &subject = Subject, unsigned channel = 1,
                    std::string pin = "-", std::string id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") {
  return "c1|LKR-TEST|" + subject + "|" + std::to_string(channel) + "|" + kind + "|0901234567|" +
         pin + "|1792443600|" + id;
}
void enable(LockerCore &c) {
  input(c, InputKind::Inventory, "1:0");
}
void cycle(LockerCore &c, Fake &f, unsigned i = 0) {
  f.now += 100;
  f.doors[i] = 1;
  c.tick();
  f.now += 100;
  f.doors[i] = 0;
  c.tick();
}
std::string permit() {
  return "r1|*|33333333333333333333333333333333|0901234567|1792443600";
}
void protocolTests() {
  uint64_t n = 0;
  check(number("18446744073709551615", n) && n == UINT64_MAX, "uint64 limit");
  check(!number("18446744073709551616", n) && !number("-1", n) && !number("", n), "invalid number");
  Workflow w;
  check(parseWorkflow(command('D'), "LKR-TEST", w) && w.kind == 'D' && w.channel == 1, "parse c1");
  check(!parseWorkflow(command('D'), "OTHER", w), "wrong cabinet");
  check(!parseWorkflow(command('D') + "|extra", "LKR-TEST", w), "extra field");
  check(!parseWorkflow(command('R', Subject, 1, "bad"), "LKR-TEST", w), "return PIN");
  Text id, body;
  check(parseFrame("1|STATUS", id, body) && id == "1", "BLE frame");
  check(!parseFrame("0|INVENTORY:1:0", id, body) && !parseFrame("1|", id, body) &&
            !parseFrame("2|STATUS\n", id, body),
        "BLE internal command exclusion");
}
void malformedInputTests() {
  std::mt19937 random(7);
  for (unsigned i = 0; i < 10000; ++i) {
    std::string text(random() % 520, ' ');
    for (char &c : text)
      c = static_cast<char>(random() % 128);
    Workflow w;
    Text id, body;
    uint64_t n;
    parseWorkflow(text, "LKR-TEST", w);
    parseFrame(text, id, body);
    number(text, n);
  }
  char output[4] = {'x', 'x', 'x', 'x'};
  copyText(output, sizeof(output), Text{});
  check(output[0] == 0, "empty view copy");
  copyText(output, sizeof(output), "123456");
  check(std::string(output) == "123", "bounded copy terminator");
  check(true, "10000 malformed input parser cases completed");
}
void nvsTests() {
  nvs_test::reset();
  StateStore store;
  State s{}, clean{};
  check(store.load(s) == LoadResult::Fresh, "fresh NVS");
  check(store.save(s), "write NVS");
  auto original = nvs_test::bytes;
  unsigned writes = nvs_test::writes;
  nvs_test::bytes = {1, 2, 3};
  auto broken = nvs_test::bytes;
  StateStore wrongSize;
  check(wrongSize.load(s) == LoadResult::Fault && nvs_test::bytes == broken &&
            nvs_test::writes == writes,
        "wrong size must preserve NVS");
  nvs_test::bytes = original;
  s = clean;
  s.version = 0;
  std::memcpy(nvs_test::bytes.data(), &s, sizeof(s));
  StateStore wrongVersion;
  check(wrongVersion.load(s) == LoadResult::Fault, "wrong version");
  s = clean;
  s.compartments[0].phase = Phase::Stored;
  copyText(s.compartments[0].subject, 33, Subject);
  copyText(s.compartments[0].phone, 11, "0901234567");
  copyText(s.compartments[0].pin, 7, "badpin");
  std::memcpy(nvs_test::bytes.data(), &s, sizeof(s));
  StateStore corrupt;
  check(corrupt.load(s) == LoadResult::Fault, "corrupt PIN");
  nvs_test::writeOk = false;
  check(!corrupt.save(State{}), "write failure");
  Fake f;
  LockerCore c(f, "LKR-TEST");
  c.begin();
  check(c.ready() && c.state().inventoryMask == 15 && !c.state().bluetooth,
        "safe initial configuration");
  Fake fault;
  fault.loaded = LoadResult::Fault;
  LockerCore bad(fault, "LKR-TEST");
  bad.begin();
  check(!bad.ready() && fault.writes == 0, "fault must not initialize");
}
void workflowTests() {
  Fake f;
  LockerCore c(f, "LKR-TEST");
  c.begin();
  check(ble(c, f, "EXEC:" + command('D')) == "FAIL:BLUETOOTH_DISABLED" && f.pulses == 0,
        "disabled BLE");
  enable(c);
  auto result = ble(c, f, "EXEC:" + command('D'));
  check(result.find("WORKFLOW_OK:1:") == 0 && f.pulses == 1, "D starts");
  c.tick();
  check(!c.state().journal[0].id[0], "relay alone is not completion");
  check(ble(c, f, "EXEC:" + command('D')).find("ALREADY_ACCEPTED:") == 0 && f.pulses == 1,
        "duplicate does not pulse");
  check(f.response.find(c.state().compartments[0].pin) != std::string::npos,
        "retry recovers lost response PIN");
  cycle(c, f);
  check(c.state().compartments[0].phase == Phase::Stored && c.state().journal[0].kind == 'D',
        "D door cycle");
  std::string pin = c.state().compartments[0].pin;
  check(ble(c, f, "RECEIPT:0:000bad") == "FAIL:WRONG_PIN", "receipt PIN required");
  check(ble(c, f, "RECEIPT:0:" + pin).find("o1|") == 0, "receipt read");
  std::string ack = "k1|" + std::string(c.state().journal[0].id);
  input(c, InputKind::Ack, ack);
  input(c, InputKind::Ack, ack);
  check(!c.state().journal[0].id[0], "idempotent ACK");
  check(ble(c, f, "PICKUP:" + pin) == "FAIL:PHONE_INTERNET_REQUIRED", "local PIN opening disabled");
  check(ble(c, f, "EXEC:" + command('P', Subject, 1, pin, "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")).find("WORKFLOW_OK:") == 0,
        "authenticated online pickup");
  cycle(c, f);
  check(c.state().compartments[0].phase == Phase::Empty && c.state().journal[0].kind == 'P',
        "P door cycle");
  check(ble(c, f, "PICKUP:" + pin) == "FAIL:PHONE_INTERNET_REQUIRED", "PIN cannot bypass online authorization");
  input(c, InputKind::Ack, "k1|" + std::string(c.state().journal[0].id));
  check(ble(c, f, "EXEC:" + command('D', Subject, 1, "-", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")) ==
            "FAIL:ALREADY_USED",
        "consumed transaction");
  check(ble(c, f, "EXEC:" + command('R', Other, 1, "471839", "cccccccccccccccccccccccccccccccc"))
                .find("WORKFLOW_OK:") == 0,
        "R starts");
  cycle(c, f);
  check(ble(c, f, "PICKUP:471839").find("PICKUP_OK:") != 0,
        "resident PIN does not open guest return");
  input(c, InputKind::Workflow,
        command('G', Other, 1, "471839", "dddddddddddddddddddddddddddddddd"));
  cycle(c, f);
  check(c.state().compartments[0].phase == Phase::Empty && c.state().journal[1].kind == 'G',
        "guest G cycle");
}
void failureTests() {
  Fake f;
  LockerCore c(f, "LKR-TEST");
  c.begin();
  enable(c);
  f.doors[0] = 1;
  check(ble(c, f, "EXEC:" + command('D')) == "FAIL:DOOR_OPEN" && f.pulses == 0,
        "open sensor blocks relay");
  f.doors[0] = 0;
  f.pulseOk = false;
  check(ble(c, f, "EXEC:" + command('D')) == "FAIL:RELAY_TIMER" &&
            c.state().compartments[0].phase == Phase::Empty,
        "relay failure rollback");
  f.pulseOk = true;
  check(ble(c, f, "EXEC:" + command('D')).find("WORKFLOW_OK:") == 0, "retry rolled back command");
  f.now += 60000;
  c.tick();
  check(std::any_of(f.notices.begin(), f.notices.end(),
                    [](const auto &n) { return n.second == "WAIT_OPEN_TIMEOUT"; }) &&
            !c.state().journal[0].id[0],
        "door timeout does not complete");
  Fake write;
  LockerCore fail(write, "LKR-TEST");
  fail.begin();
  enable(fail);
  write.saveOk = false;
  check(ble(fail, write, "EXEC:" + command('D')) == "FAIL:STORAGE" && write.pulses == 0 &&
            !fail.ready(),
        "save before pulse");
  Fake clock;
  LockerCore noClock(clock, "LKR-TEST");
  noClock.begin();
  enable(noClock);
  clock.epoch = 0;
  check(ble(noClock, clock, "EXEC:" + command('D')) == "FAIL:CLOCK_NOT_READY" && clock.pulses == 0,
        "unknown UTC");
  clock.epoch = 1892440000;
  check(ble(noClock, clock, "EXEC:" + command('D')) == "FAIL:EXPIRED", "expired command");
  Fake lock;
  LockerCore blocked(lock, "LKR-TEST");
  blocked.begin();
  enable(blocked);
  ble(blocked, lock, "EXEC:" + command('D'));
  cycle(blocked, lock);
  ble(blocked, lock, "RECEIPT:0:999999");
  ble(blocked, lock, "RECEIPT:0:999999");
  check(ble(blocked, lock, "RECEIPT:0:999999") == "FAIL:LOCKED:60", "three receipt PIN attempts");
  lock.now += 60000;
  check(ble(blocked, lock, "RECEIPT:0:" + std::string(blocked.state().journal[0].pin)).find("o1|") == 0,
        "receipt lock expires");
}
void offlineAndRelayTests() {
  Fake f;
  LockerCore c(f, "LKR-TEST"); c.begin(); enable(c);
  for (bool online : {false,true}) {
    f.online=online;
    for (const auto &body : {"RETURN:"+permit()+":"+Subject, std::string("PICKUP:471839"), "PREPARE:"+permit()+":N:"+Subject})
      check(ble(c,f,body)=="FAIL:PHONE_INTERNET_REQUIRED" && f.pulses==0, "phone offline commands rejected");
  }
  input(c,InputKind::Ble,"1|EXEC:"+command('R'));
  check(f.response=="FAIL:COMMAND_AUTH" && f.pulses==0,"unsigned BLE cannot open");
  input(c,InputKind::Ble,"1|RESET:z1|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|LKR-TEST|1|"+Subject+"|1792443600|-");
  check(f.response=="FAIL:COMMAND_AUTH" && f.pulses==0,"unsigned BLE cannot reset");
  input(c,InputKind::Remote,"1|"+command('R'));
  check(f.pulses==0,"retired proxy response cannot open");
  Fake pin;
  LockerCore provision(pin, "LKR-TEST");
  provision.begin();
  enable(provision);
  input(provision, InputKind::Pin, "1:" + Subject + ":0901234567:000001");
  check(pin.messages.back().second == "q1|" + Subject + "|000001" && pin.pulses == 0,
        "PIN provision and leading zero");
  input(provision, InputKind::Pin, "1:" + Subject + ":0901234567:999999");
  check(std::string(provision.state().compartments[0].pin) == "000001",
        "PIN not silently replaced");
}
void capacityAndReplayTests() {
  Fake full;
  full.loaded = LoadResult::Loaded;
  full.saved.sequence = JournalSlots;
  for (unsigned i = 0; i < JournalSlots; ++i) {
    auto &e = full.saved.journal[i];
    std::snprintf(e.id, 33, "%032x", i + 1);
    copyText(e.subject, 33, Subject);
    copyText(e.phone, 11, "0901234567");
    copyText(e.pin, 7, "000001");
    e.channel = 1;
    e.kind = 'D';
    e.elapsed = 100;
    e.sequence = i + 1;
  }
  LockerCore capacity(full, "LKR-TEST");
  capacity.begin();
  enable(capacity);
  check(capacity.ready(), "valid full journal rejected");
  check(ble(capacity, full, "EXEC:" + command('D', Other, 2)) == "FAIL:SYNC_REQUIRED" &&
            full.pulses == 0,
        "full journal must block new deposit");
  input(capacity, InputKind::Ack, "k1|00000000000000000000000000000001");
  check(ble(capacity, full, "EXEC:" + command('D', Other, 2)) == "FAIL:SYNC_REQUIRED",
        "deposit must reserve pickup journal slot too");
  input(capacity, InputKind::Ack, "k1|00000000000000000000000000000002");
  check(ble(capacity, full, "EXEC:" + command('D', Other, 2)).find("WORKFLOW_OK:") == 0,
        "two journal slots should allow deposit");

  Fake f;
  LockerCore c(f, "LKR-TEST");
  c.begin();
  enable(c);
  ble(c, f, "EXEC:" + command('D'));
  cycle(c, f);
  Fake reboot;
  reboot.loaded = LoadResult::Loaded;
  reboot.saved = f.saved;
  LockerCore loaded(reboot, "LKR-TEST");
  loaded.begin();
  check(loaded.ready() && loaded.state().compartments[0].phase == Phase::Stored &&
            reboot.pulses == 0,
        "stored PIN persistence without automatic pulse");
  f.online = true;
  f.generation = 1;
  f.now = 4000;
  f.sendOk = false;
  c.tick();
  check(c.state().journal[0].id[0] != 0, "queue failure erased journal");
  f.sendOk = true;
  f.now += 3000;
  c.tick();
  std::string copy;
  for (const auto &message : f.messages)
    if (message.first == "events/offline")
      copy = message.second;
  check(copy.find("o1|") == 0, "journal not replayed after queue recovers");
  input(c, InputKind::Ack, "k1|" + std::string(c.state().journal[0].id));
  check(!c.state().journal[0].id[0] && copy.find("o1|") == 0, "outbound copy changed after ACK");
  size_t before = f.messages.size();
  ++f.generation;
  c.tick();
  check(f.messages.size() >= before + 4, "reconnect must resend unchanged door states");
}

void queueTests() {
  std::deque<int> queue{1, 2};
  int pending = 0;
  bool has = false, canSend = false;
  std::vector<int> sent;
  auto pop = [&](int &n) {
    if (queue.empty())
      return false;
    n = queue.front();
    queue.pop_front();
    return true;
  };
  auto send = [&](int n) {
    if (!canSend)
      return false;
    sent.push_back(n);
    return true;
  };
  drainOutgoing(pending, has, pop, send);
  check(has && pending == 1 && queue.size() == 1 && sent.empty(), "failed publication retained");
  drainOutgoing(pending, has, pop, send);
  check(has && queue.size() == 1, "reconnect keeps pending");
  canSend = true;
  drainOutgoing(pending, has, pop, send);
  check(!has && queue.empty() && sent == std::vector<int>({1, 2}), "retry ordered");
}
void contractFixtures(const std::string &folder) {
  std::ifstream commands(folder + "/commands.txt");
  check(commands.good(), "BE commands fixture missing");
  std::ofstream proofs(folder + "/events.txt");
  Fake f;
  LockerCore c(f, "LKR-TEST");
  c.begin();
  enable(c);
  std::string line;
  while (std::getline(commands, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    unsigned before = f.pulses;
    input(c, InputKind::Workflow, line);
    check(f.pulses == before + 1, "actual BE command rejected");
    cycle(c, f);
    const Record *event = nullptr;
    for (const auto &e : c.state().journal)
      if (e.id[0]) {
        event = &e;
        break;
      }
    check(event != nullptr, "BE command produced no door event");
    proofs << encodeRecord(*event, "LKR-TEST") << "\n";
    input(c, InputKind::Ack, "k1|" + std::string(event->id));
  }
  check(c.state().sequence == 4, "D/P/R/G contract fixtures");
}
} // namespace
int main(int argc, char **argv) {
  try {
    protocolTests();
    malformedInputTests();
    nvsTests();
    workflowTests();
    failureTests();
    offlineAndRelayTests();
    queueTests();
    capacityAndReplayTests();
    if (argc > 1) {
      contractFixtures(argv[1]);
      std::ifstream signedCommands(std::string(argv[1]) + "/ble-commands.txt");
      std::ifstream rawCommands(std::string(argv[1]) + "/commands.txt");
      std::string signedLine, rawLine, decoded;
      unsigned count = 0;
      while (std::getline(signedCommands, signedLine) && std::getline(rawCommands, rawLine)) {
        if (!signedLine.empty() && signedLine.back() == '\r') signedLine.pop_back();
        if (!rawLine.empty() && rawLine.back() == '\r') rawLine.pop_back();
        check(verifyBluetoothCommand(signedLine, std::string(64, '1'), decoded) && decoded == rawLine, "real BE signature must verify");
        check(!verifyBluetoothCommand(signedLine, std::string(64, '2'), decoded), "wrong device key must fail");
        signedLine.back() = signedLine.back() == 'a' ? 'b' : 'a';
        check(!verifyBluetoothCommand(signedLine, std::string(64, '1'), decoded), "tampered command must fail");
        check(!verifyBluetoothCommand(rawLine, std::string(64, '1'), decoded), "unsigned command must fail");
        ++count;
      }
      check(count == 4, "all signed D/P/R/G fixtures required");
    }
    std::cout << "PASS: " << checks
              << " checks (protocol, NVS, D/P/R/G, PIN, relay, door cycle, timeout, retry, ACK)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
  }
}
