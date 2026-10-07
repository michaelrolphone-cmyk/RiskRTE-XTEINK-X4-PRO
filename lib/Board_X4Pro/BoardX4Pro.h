#pragma once
#include <Arduino.h>
#include <BoardCapabilities.h>
#include <RiscFrontlightV1.h>

namespace BoardX4Pro {
const char* id();
const char* displayName();
const char* firmwareMarker();
const BoardCapabilities& capabilities();

class ScopedI2CLock {
 public:
  ScopedI2CLock() = default;
  explicit ScopedI2CLock(uint32_t) {}
  bool acquired() const { return false; }
  ~ScopedI2CLock() = default;
};

void begin();
void beginI2C();
bool attachFrontlight(const risc_frontlight_api_v1* api);
void initBacklight();
void setBacklightLevel(uint8_t level);
void restoreBacklightLevel(uint8_t level);
void prepareSdBus();
void disableGpsLora();
bool prepareForSleep();
void deinitForSleep();

struct BatteryProfile {
  uint16_t inputLimitMa = 0;
  uint16_t capacityMah = 0;  // Not supplied by board.battery v1.
  uint16_t chargeCurrentMa = 0;
  uint16_t prechargeCurrentMa = 0;
  uint16_t terminationCurrentMa = 0;
  uint16_t chargeVoltageMv = 0;
  uint16_t chargeTerminationVoltageDeltaMv = 0;
  uint16_t systemMinVoltageMv = 0;
  int16_t currentThresholdMa = 0;
};
enum class BatteryChargeStatus : uint8_t {
  NotCharging = 0,
  Precharge = 1,
  FastCharge = 2,
  Done = 3,
  Unknown = 0xFF,
};
enum class BatteryGaugeState : uint8_t {
  Sleep = 0,
  Full = 1,
  Charge = 2,
  Discharge = 3,
  Relax = 4,
  Unknown = 0xFF,
};
struct BatteryState {
  // X4 supplies only gauge validity, SOC, battery voltage and charging. The
  // existing shared structure has no per-field known flags: zero/false in
  // other fields means unsupported, not measured absence. Detailed telemetry
  // remains disabled; chargerStatus and non-charging gaugeState stay Unknown.
  bool chargerReady = false;
  bool gaugeReady = false;
  bool chargerReadOk = false;
  bool gaugeReadOk = false;
  bool vbusConnected = false;
  bool chargeEnabled = false;
  bool charging = false;
  bool chargeDone = false;
  bool gaugeBatteryFullFlag = false;
  bool gaugeGaugingFullFlag = false;
  bool gaugeTaperFlag = false;
  bool gaugeChargeInhibit = false;
  uint8_t chargerVbusStatus = 0;
  BatteryChargeStatus chargerStatus = BatteryChargeStatus::Unknown;
  BatteryGaugeState gaugeState = BatteryGaugeState::Unknown;
  uint16_t inputLimitMa = 0;
  uint16_t chargeCurrentMa = 0;
  uint16_t prechargeCurrentMa = 0;
  uint16_t terminationCurrentMa = 0;
  uint16_t chargerAdcCurrentMa = 0;
  uint16_t chargeVoltageMv = 0;
  uint16_t systemVoltageMv = 0;
  uint16_t batteryVoltageMv = 0;
  uint16_t vbusVoltageMv = 0;
  uint16_t gaugeVoltageMv = 0;
  uint16_t gaugeChargeVoltageMv = 0;
  uint16_t gaugeTaperCurrentMa = 0;
  uint16_t socPercent = 0;
  uint16_t sohPercent = 0;
  uint16_t fullCapacityMah = 0;
  uint16_t remainingCapacityMah = 0;
  uint16_t temperatureDk = 0;
  uint16_t batteryStatusRaw = 0;
  uint16_t gaugingStatusRaw = 0;
  int16_t currentMa = 0;
  int16_t averageCurrentMa = 0;
};
const BatteryProfile& batteryProfile();
// Cache-only on X4: begin does not initialize hardware or guarantee availability.
// It reports whether the owner loop has published a still-valid sample. Reads
// return false before that happens, on failure/expiry, or for null outputs.
bool beginBatteryManagement();
bool isBatteryManagementReady();
bool readBatteryState(BatteryState* state);
bool shutdownBatteryPower();
bool pca9535Present();
bool readPca9535Pin(uint8_t pin, bool* high);
bool writePca9535Pin(uint8_t pin, bool high);
bool setPca9535PinMode(uint8_t pin, uint8_t mode);
bool readButton();
bool readBQ27220Reg16(uint8_t reg, uint16_t* value);
bool readBQ25896Reg8(uint8_t reg, uint8_t* value);
bool readBatteryStateOfCharge(uint16_t* soc);
bool readBatteryCurrentMa(int16_t* current);
bool readBatteryAverageCurrentMa(int16_t* current);
bool isUsbConnected();
}

namespace BoardX4ProPins {
static constexpr uint16_t DisplayWidth = 800;
static constexpr uint16_t DisplayHeight = 480;
static constexpr uint16_t LogicalWidth = 480;
static constexpr uint16_t LogicalHeight = 800;
static constexpr uint8_t PowerButton = 3;
static constexpr uint8_t TouchInterrupt = 10;
static constexpr uint8_t SdCs = 0xFF;
static constexpr uint8_t RtcAddress = 0x51;
}
