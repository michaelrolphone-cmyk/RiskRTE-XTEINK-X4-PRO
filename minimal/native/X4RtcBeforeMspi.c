// X4-only startup experiment. Adapt the ordering, not the power settings, from
// ESP-IDF 5b71b949be35ae2577bf8989febc120df2520037 to the pinned IDF 4.4 SDK.
// This file must be linked only with all four --wrap flags in link-flags.txt.
// No heap, scheduler, NVS, GPIO, USB or diagnostics calls are safe here.
#include <stdbool.h>
#include <stdint.h>
#include <esp_attr.h>
#include <soc/rtc.h>
#include <esp_rom_sys.h>

extern void __real_spi_flash_init_chip_state(void);
extern void __real_rtc_clk_recalib_bbpll(void);
extern void __real_rtc_init(rtc_config_t cfg);
extern void __real_esp_clk_init(void);

enum {
    X4_RTC_ORDER_INITIAL = 0,
    X4_RTC_ORDER_POWER_BEGIN = 1,
    X4_RTC_ORDER_POWER_READY = 2,
    X4_RTC_ORDER_FLASH_READY = 3,
    X4_RTC_ORDER_RECALIB_SKIPPED = 4,
    X4_RTC_ORDER_CLOCK_BEGIN = 5,
    X4_RTC_ORDER_COMPLETE = 6,
    X4_RTC_ORDER_UNEXPECTED = 7
};

// The bootloader loads these zero-initialized DRAM values before application
// entry. DRAM remains available across cache/PLL work.
// Retention is not claimed: these are current-boot inspection fields only.
DRAM_ATTR volatile uint32_t risc_x4_rtc_order_state = 0;
DRAM_ATTR volatile uint32_t risc_x4_rtc_order_skipped = 0;
const char risc_x4_rtc_order_experiment[] =
    "X4_STARTUP_EXPERIMENT:rtc-before-mspi:1:upstream-5b71b949";

void IRAM_ATTR __wrap_spi_flash_init_chip_state(void)
{
    const bool first = risc_x4_rtc_order_state == X4_RTC_ORDER_INITIAL;
    if (first) {
        risc_x4_rtc_order_state = X4_RTC_ORDER_POWER_BEGIN;
        // Preserve the original PLL recalibration, but perform it with RTC
        // power initialization before MSPI timing is trained, as upstream does.
        __real_rtc_clk_recalib_bbpll();
        rtc_config_t cfg = RTC_CONFIG_DEFAULT();
        if (esp_rom_get_reset_reason(0) == RESET_REASON_CHIP_POWER_ON)
            cfg.cali_ocode = 1;
        __real_rtc_init(cfg);
        risc_x4_rtc_order_state = X4_RTC_ORDER_POWER_READY;
    }
    __real_spi_flash_init_chip_state();
    if (first) risc_x4_rtc_order_state = X4_RTC_ORDER_FLASH_READY;
}

void IRAM_ATTR __wrap_rtc_clk_recalib_bbpll(void)
{
    // Skip only cpu_start.c's original immediately-following startup call.
    // Subsequent callers retain the original function behavior.
    if (risc_x4_rtc_order_state == X4_RTC_ORDER_FLASH_READY) {
        risc_x4_rtc_order_state = X4_RTC_ORDER_RECALIB_SKIPPED;
        return;
    }
    __real_rtc_clk_recalib_bbpll();
}

void IRAM_ATTR __wrap_rtc_init(rtc_config_t cfg)
{
    // The one suppressed call is scoped to the original esp_clk_init body.
    // Never suppress rtc_init from unrelated or later callers.
    if (risc_x4_rtc_order_state == X4_RTC_ORDER_CLOCK_BEGIN &&
            risc_x4_rtc_order_skipped == 0) {
        risc_x4_rtc_order_skipped = 1;
        return;
    }
    __real_rtc_init(cfg);
}

void IRAM_ATTR __wrap_esp_clk_init(void)
{
    const bool reordered = risc_x4_rtc_order_state == X4_RTC_ORDER_RECALIB_SKIPPED;
    if (reordered) risc_x4_rtc_order_state = X4_RTC_ORDER_CLOCK_BEGIN;
    // All RTC slow/fast clock selection, watchdog feeding, CPU/APB frequency
    // changes and cycle-count adjustments remain in the pinned SDK body.
    __real_esp_clk_init();
    if (reordered) risc_x4_rtc_order_state = risc_x4_rtc_order_skipped == 1
        ? X4_RTC_ORDER_COMPLETE : X4_RTC_ORDER_UNEXPECTED;
}
