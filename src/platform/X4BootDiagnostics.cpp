#include "X4BootDiagnostics.h"
#if defined(BOARD_XTEINK_X4_PRO)
#include <Arduino.h>
#include <Logging.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <cstddef>
#include <cstring>

namespace X4BootDiagnostics {
namespace {
constexpr uint32_t kMagic = 0x58344231; // X4B1, diagnostic record schema.
struct Record {
  uint32_t magic, resetReason, stage, stageAtMs, failureStage, failureAtMs;
  char reason[112];
  uint32_t checksum;
};
static_assert(sizeof(Record) == 140, "Boot diagnostic record layout changed");
RTC_NOINIT_ATTR Record retained;
Record previous{};
bool initialized = false, previousValid = false, wasConnected = false;
uint32_t digest(const Record& record) {
  const auto* bytes = reinterpret_cast<const unsigned char*>(&record);
  uint32_t value = 2166136261u;
  for (size_t i = 0; i < offsetof(Record, checksum); ++i) value = (value ^ bytes[i]) * 16777619u;
  return value;
}
bool valid(const Record& record) {
  return record.magic == kMagic && record.stage < static_cast<uint32_t>(Stage::Count) &&
      record.failureStage <= static_cast<uint32_t>(Stage::Count) &&
      std::memchr(record.reason, 0, sizeof(record.reason)) && record.checksum == digest(record);
}
bool retentionExpected(uint32_t reset) {
  // EN/power-on, brownout and unknown resets do not establish RTC SRAM
  // continuity. Reject their old bytes even if they happen to look valid.
  return reset == ESP_RST_SW || reset == ESP_RST_PANIC || reset == ESP_RST_INT_WDT ||
      reset == ESP_RST_TASK_WDT || reset == ESP_RST_WDT || reset == ESP_RST_DEEPSLEEP;
}
const char* name(uint32_t value) {
  static const char* const names[] = {"entry", "board-power", "serial", "clock-resume", "packages",
      "storage", "frontlight", "navigation", "display", "power-manager", "settings", "rtc", "touch",
      "fonts", "reader-state", "splash", "battery", "home-prepare", "home-present", "ready", "none"};
  return value <= static_cast<uint32_t>(Stage::Count) ? names[value] : "invalid";
}
void report(const char* which, const Record& record) {
  LOG_INF("X4BOOT", "%s reset=%lu stage=%s at_ms=%lu first_failure=%s failure_ms=%lu",
      which, static_cast<unsigned long>(record.resetReason), name(record.stage),
      static_cast<unsigned long>(record.stageAtMs), name(record.failureStage),
      static_cast<unsigned long>(record.failureAtMs));
  if (record.reason[0]) LOG_ERR("X4BOOT", "%s reason=%s", which, record.reason);
}
}
void begin(uint32_t resetReason) {
  previousValid = retentionExpected(resetReason) && valid(retained);
  previous = previousValid ? retained : Record{};
  Record next{};
  next.magic = kMagic;
  next.resetReason = resetReason;
  next.stage = static_cast<uint32_t>(Stage::Entry);
  next.stageAtMs = static_cast<uint32_t>(millis());
  next.failureStage = static_cast<uint32_t>(Stage::Count);
  next.checksum = digest(next);
  retained = next;
  initialized = true;
  wasConnected = false;
}
void mark(Stage stage) {
  if (!initialized || stage >= Stage::Count) return;
  retained.stage = static_cast<uint32_t>(stage);
  retained.stageAtMs = static_cast<uint32_t>(millis());
  retained.checksum = digest(retained);
}
void fail(const char* reason) {
  if (!initialized || retained.failureStage != static_cast<uint32_t>(Stage::Count)) return;
  retained.failureStage = retained.stage;
  retained.failureAtMs = static_cast<uint32_t>(millis());
  const char* text = reason && *reason ? reason : "unspecified failure";
  const size_t count = strnlen(text, sizeof(retained.reason) - 1u);
  std::memcpy(retained.reason, text, count);
  retained.reason[count] = 0;
  retained.checksum = digest(retained);
}
void poll(bool serialConnected) {
  if (!initialized) return;
  if (serialConnected && !wasConnected) {
    report("current", retained);
    if (previousValid) report("previous-retained", previous);
    else LOG_INF("X4BOOT", "previous record unavailable or reset does not establish RTC retention");
  }
  wasConnected = serialConnected;
}
}
#endif
