#include "BoardX4Pro.h"
#include "FrontlightLevels.h"
#include "../../src/native/NativeBatteryGauge.h"
extern bool halStoragePrepareForSleep();
extern void halStorageMediaUnavailable();

namespace BoardX4Pro {
namespace {
BoardCapabilities kCaps{};
const risc_frontlight_api_v1* frontlight = nullptr;
const BatteryProfile kProfile{};
}
const char* id() { return "xteink-x4-pro"; }
const char* displayName() { return "Xteink X4 Pro"; }
const char* firmwareMarker() { return "RISCRTE_BOARD_ID:xteink-x4-pro"; }
const BoardCapabilities& capabilities() { return kCaps; }
void begin() {}
void beginI2C() {}
bool attachFrontlight(const risc_frontlight_api_v1* api) {
  if (!api || api->api_version != RISC_FRONTLIGHT_API_V1 ||
      api->struct_size < sizeof(*api) || !api->set_level || !api->get_level) return false;
  frontlight = api;
  kCaps.hasBacklight = true;
  return true;
}
void initBacklight() {}
void setBacklightLevel(uint8_t level) {
  if (frontlight) (void)frontlight->set_level(frontlight->context, frontlightTenthsPercent(level), 1000);
}
void restoreBacklightLevel(uint8_t level) { setBacklightLevel(level); }
void prepareSdBus() {}
void disableGpsLora() {}
bool prepareForSleep() { return halStoragePrepareForSleep(); }
void deinitForSleep() { halStorageMediaUnavailable(); }
const BatteryProfile& batteryProfile() { return kProfile; }
bool beginBatteryManagement() { return isBatteryManagementReady(); }
bool isBatteryManagementReady() {
  NativeBatterySnapshot sample{};
  return nativeBatteryReadSnapshot(&sample);
}
bool readBatteryState(BatteryState* state) {
  if (!state) return false;
  *state = BatteryState{};
  NativeBatterySnapshot sample{};
  if (!nativeBatteryReadSnapshot(&sample)) return false;
  state->gaugeReady = state->gaugeReadOk = true;
  state->socPercent = sample.percent;
  state->batteryVoltageMv = state->gaugeVoltageMv = sample.millivolts;
  state->charging = sample.charging;
  if (sample.charging) state->gaugeState = BatteryGaugeState::Charge;
  // Charging is measured; cable attachment, completion and charger details
  // are not. In particular 100% is not proof of chargeDone or a full flag.
  return true;
}
bool shutdownBatteryPower() { return false; }
bool pca9535Present() { return false; }
bool readPca9535Pin(uint8_t, bool* high) {
  if (high) *high = false;
  return false;
}
bool writePca9535Pin(uint8_t, bool) { return false; }
bool setPca9535PinMode(uint8_t, uint8_t) { return false; }
bool readButton() { return false; }
bool readBQ27220Reg16(uint8_t, uint16_t* value) {
  if (value) *value = 0;
  return false;
}
bool readBQ25896Reg8(uint8_t, uint8_t* value) {
  if (value) *value = 0;
  return false;
}
bool readBatteryStateOfCharge(uint16_t* soc) {
  if (!soc) return false;
  *soc = 0;
  NativeBatterySnapshot sample{};
  if (!nativeBatteryReadSnapshot(&sample)) return false;
  *soc = sample.percent;
  return true;
}
bool readBatteryCurrentMa(int16_t* current) {
  if (current) *current = 0;
  return false;
}
bool readBatteryAverageCurrentMa(int16_t* current) {
  if (current) *current = 0;
  return false;
}
bool isUsbConnected() { return false; }
}
