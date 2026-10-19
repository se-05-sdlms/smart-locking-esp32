#pragma once
#include <cstddef>
#include <cstdint>
namespace locker {
// GPIO và mức tín hiệu đã được chủ dự án xác nhận theo README.
inline constexpr int RelayPins[] = {23, 22, 21, 19};
inline constexpr int SensorPins[] = {32, 33, 25, 26};
inline constexpr unsigned Channels = 4, JournalSlots = 16, CommandSlots = 32;
inline constexpr uint64_t RelayMicros = 2000000, DebounceMs = 50;
inline constexpr char ServiceUuid[] = "0000fff0-0000-1000-8000-00805f9b34fb";
inline constexpr char CommandUuid[] = "0000fff1-0000-1000-8000-00805f9b34fb";
inline constexpr char ResponseUuid[] = "0000fff2-0000-1000-8000-00805f9b34fb";
inline constexpr char DeviceUuid[] = "0000fff3-0000-1000-8000-00805f9b34fb";
inline constexpr char DoorUuid[] = "0000fff4-0000-1000-8000-00805f9b34fb";
inline constexpr uint32_t StateVersion = 0x4c4b0001;
} // namespace locker
