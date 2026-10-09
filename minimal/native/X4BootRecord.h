#pragma once
#include <cstddef>
#include <cstdint>

// Diagnostics only: no field in this retained image controls boot or GPIOs.
// A checksum-valid image is evidence of writes, never proof that RTC memory
// survived a complete power loss or that the preceding boot was on battery.
namespace X4Boot {
constexpr uint32_t Magic = 0x58344232;
enum Phase : uint32_t { AppMain = 1, RailReady, Variant, SetupGate };
struct Record {
  uint32_t magic, checksum, boot, phase, operation, reset, raw0, raw1, wake;
  uint32_t gpioBefore, holdBefore, brownout, entryUs, variantUs, gateUs;
};
static_assert(sizeof(Record) == 60, "X4 boot breadcrumb size changed");
inline uint32_t checksum(const Record& record) {
  const auto* bytes = reinterpret_cast<const unsigned char*>(&record);
  uint32_t hash = 2166136261u;
  for (size_t i = offsetof(Record, boot); i < sizeof(record); ++i)
    hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
inline bool valid(const Record& record) {
  return record.magic == Magic && record.boot && record.phase >= AppMain &&
    record.phase <= SetupGate && record.operation <= 7 && record.checksum == checksum(record);
}
inline void changing(Record& record) {
  record.magic = 0;
  asm volatile("" ::: "memory");
}
inline void seal(Record& record) {
  record.checksum = checksum(record);
  asm volatile("" ::: "memory");
  record.magic = Magic;
}
inline void mark(Record& record, Phase phase, uint32_t operation = 0) {
  changing(record); record.phase = phase; record.operation = operation; seal(record);
}
}
