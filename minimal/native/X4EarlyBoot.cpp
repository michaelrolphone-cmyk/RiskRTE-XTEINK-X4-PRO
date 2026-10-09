// X4-only native composition. Ordinary providers still require full admission.
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <rom/rtc.h>
#include <soc/gpio_reg.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>
#include <cstdio>
#include "X4BootRecord.h"
#include "X4NativeBuildIdentity.h"

extern "C" const char risc_x4_native_composition_identity[] = X4_NATIVE_COMPOSITION_IDENTITY;
// The selected Runtime environments provide this bounded, sole-owner sink.
// It is used only by the setup gate, after Runtime starts its diagnostics.
namespace RiscDiagnostics { void line(const char* text); }

extern "C" { RTC_NOINIT_ATTR X4Boot::Record risc_x4_boot_record; }

namespace {
const char* startupError = "x4-app-main-not-entered";
bool attempted = false, variant = false, reported = false, previousValid = false;
constexpr gpio_num_t peripheralRail = GPIO_NUM_1;
X4Boot::Record& retained = risc_x4_boot_record;
X4Boot::Record previous;

void prepareRail() {
  // GPIO1 is the documented peripheral/touch enable. A CPU self-latch has not
  // been established. Keep the previous pad hold until digital HIGH is ready.
  startupError = "x4-gpio1-rtc-deinit";
  X4Boot::mark(retained, X4Boot::AppMain, 1);
  if (rtc_gpio_deinit(peripheralRail) != ESP_OK) return;
  startupError = "x4-gpio1-stage-high";
  X4Boot::mark(retained, X4Boot::AppMain, 2);
  if (gpio_set_level(peripheralRail, 1) != ESP_OK) return;
  gpio_config_t config{};
  config.pin_bit_mask = uint64_t(1) << peripheralRail;
  config.mode = GPIO_MODE_INPUT_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  startupError = "x4-gpio1-configure";
  X4Boot::mark(retained, X4Boot::AppMain, 3);
  if (gpio_config(&config) != ESP_OK) return;
  startupError = "x4-gpio1-confirm-high";
  X4Boot::mark(retained, X4Boot::AppMain, 4);
  if (gpio_set_level(peripheralRail, 1) != ESP_OK) return;
  startupError = "x4-gpio1-unhold";
  X4Boot::mark(retained, X4Boot::AppMain, 5);
  if (gpio_hold_dis(peripheralRail) != ESP_OK) return;
  startupError = "x4-gpio1-hold";
  X4Boot::mark(retained, X4Boot::AppMain, 6);
  if (gpio_hold_en(peripheralRail) != ESP_OK) return;
  startupError = "x4-gpio1-readback-low";
  X4Boot::mark(retained, X4Boot::AppMain, 7);
  if (gpio_get_level(peripheralRail) != 1) return;
  startupError = nullptr;
  X4Boot::mark(retained, X4Boot::RailReady);
}

void report() {
  char line[256];
  std::snprintf(line, sizeof(line),
    "X4_BOOT boot=%lu reset=%lu raw0=%lu raw1=%lu wake=%lu entry_us=%lu variant_us=%lu gate_us=%lu error=%s",
    (unsigned long)retained.boot, (unsigned long)retained.reset, (unsigned long)retained.raw0,
    (unsigned long)retained.raw1, (unsigned long)retained.wake, (unsigned long)retained.entryUs,
    (unsigned long)retained.variantUs, (unsigned long)retained.gateUs, startupError ? startupError : "none");
  RiscDiagnostics::line(line);
  std::snprintf(line, sizeof(line),
    "X4_BOOT pads gpio_in_before=0x%08lx hold_before=0x%08lx brownout=0x%08lx gpio1_now=%d",
    (unsigned long)retained.gpioBefore, (unsigned long)retained.holdBefore,
    (unsigned long)retained.brownout, gpio_get_level(peripheralRail));
  RiscDiagnostics::line(line);
  std::snprintf(line, sizeof(line),
    "X4_BOOT previous checksum_valid=%u boot=%lu phase=%lu operation=%lu reset=%lu entry_us=%lu variant_us=%lu gate_us=%lu",
    unsigned(previousValid), (unsigned long)previous.boot, (unsigned long)previous.phase,
    (unsigned long)previous.operation, (unsigned long)previous.reset, (unsigned long)previous.entryUs,
    (unsigned long)previous.variantUs, (unsigned long)previous.gateUs);
  RiscDiagnostics::line(line);
  if(previousValid && previous.milestoneCount) {
    std::snprintf(line,sizeof(line),
      "X4_BOOT previous-stage boot=%lu kind=%lu count=%lu us=%llu first_display=%u first_display_us=%llu truncated=%u",
      (unsigned long)previous.boot,(unsigned long)previous.milestoneKind,(unsigned long)previous.milestoneCount,
      (unsigned long long)previous.milestoneUs,unsigned(previous.displayCompleted),
      (unsigned long long)previous.firstDisplayUs,unsigned(previous.messageTruncated));
    RiscDiagnostics::line(line);
    std::snprintf(line,sizeof(line),"X4_BOOT previous-line %s",previous.milestone);
    RiscDiagnostics::line(line);
  }
}
}

extern "C" void __real_app_main(void);
extern "C" void __wrap_app_main(void) {
  if (!attempted) {
    attempted = true;
    previousValid = X4Boot::valid(retained);
    if (previousValid) previous = retained;
    X4Boot::changing(retained);
    retained = {};
    retained.boot = previousValid && previous.boot != UINT32_MAX ? previous.boot + 1 : 1;
    retained.phase = X4Boot::AppMain;
    retained.reset = uint32_t(esp_reset_reason());
    retained.raw0 = uint32_t(rtc_get_reset_reason(0));
    retained.raw1 = uint32_t(rtc_get_reset_reason(1));
    retained.wake = uint32_t(esp_sleep_get_wakeup_cause());
    retained.gpioBefore = REG_READ(GPIO_IN_REG);
    retained.holdBefore = REG_READ(RTC_CNTL_PAD_HOLD_REG);
    retained.brownout = REG_READ(RTC_CNTL_BROWN_OUT_REG);
    retained.entryUs = uint32_t(esp_timer_get_time());
    X4Boot::seal(retained);
    prepareRail();
  }
  // All original Arduino initialization and task creation is still executed.
  // Failure stays latched for Runtime's existing startup gate; no rail cleanup.
  __real_app_main();
}

extern "C" void initVariant(void) {
  if (variant || !attempted) return;
  variant = true;
  X4Boot::changing(retained);
  retained.variantUs = uint32_t(esp_timer_get_time());
  retained.phase = X4Boot::Variant;
  X4Boot::seal(retained);
}

extern "C" const char* risc_native_startup_error(void) {
  if (!attempted || !variant) return startupError ? startupError : "x4-init-variant-not-entered";
  if (!reported) {
    reported = true;
    X4Boot::changing(retained);
    retained.gateUs = uint32_t(esp_timer_get_time());
    retained.phase = X4Boot::SetupGate;
    X4Boot::seal(retained);
    report();
  }
  return startupError;
}

// Called only by the selected Runtime diagnostic owner, before USB availability
// can discard a line. No retained field authorizes hardware or boot behavior.
extern "C" void risc_native_diagnostic_observer(const char* line) {
  if(!attempted || !variant || X4Boot::classify(retained,line)==X4Boot::None)return;
  (void)X4Boot::observe(retained,line,uint64_t(esp_timer_get_time()));
}
