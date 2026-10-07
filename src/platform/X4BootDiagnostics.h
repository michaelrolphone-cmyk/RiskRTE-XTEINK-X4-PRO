#pragma once
#include <cstdint>

namespace X4BootDiagnostics {
enum class Stage : uint32_t {
  Entry, BoardPower, SerialSetup, ClockResume, Packages, StorageMount, Frontlight,
  Navigation, Display, PowerManager, Settings, Rtc, Touch, Fonts, ReaderState,
  Splash, Battery, HomePrepare, HomePresent, Ready, Count
};
// Main boot/loop owner only. No filesystem, device access or startup retry.
void begin(uint32_t resetReason);
void mark(Stage stage);
void fail(const char* reason);
// Called at the existing heartbeat cadence; reports once per connected edge.
void poll(bool serialConnected);
}
