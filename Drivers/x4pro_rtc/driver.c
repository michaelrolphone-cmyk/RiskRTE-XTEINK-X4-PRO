/* X4 BM8563 through the existing exclusive i2c.bus owner, never Wire. */
#include "RiscProviderV2.h"
#include "RiscI2cBusV1.h"
#include "RiscRtcClockV2.h"
#include "RiscRtcCalendarV2.h"
#include "x4pro_pins.h"

static const risc_i2c_bus_api_v1 *bus;
static uint64_t claim;
static bool started;
static char last_error_text[64];
static void fail(const char *text) {
    size_t i = 0;
    while (i + 1u < sizeof(last_error_text) && text[i]) {
        last_error_text[i] = text[i]; ++i;
    }
    last_error_text[i] = 0;
}
#include "../common/pcf8563_rtc_ops.h"

static bool quiesce(void) {
    started = false;
    /* Read-only startup/read/teardown never alters the chip or its alarms.
     * A failed drain retains the exact claim and dependency, pinning both. */
    if (claim && (!bus || !bus->release_device(bus->context, claim))) {
        fail("rtc release pending"); return false;
    }
    claim = 0;
    bus = NULL;
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || bus || claim) { fail("rtc already owned"); return false; }
    static const char name[] = "i2c.bus";
    if (!deps || count != 1u || !deps[0].capability_id ||
        deps[0].api_version != RISC_I2C_BUS_API_V1) {
        fail("rtc i2c dependency"); return false;
    }
    for (size_t i = 0; i < sizeof(name); ++i)
        if (deps[0].capability_id[i] != name[i]) { fail("rtc i2c dependency"); return false; }
    const risc_i2c_bus_api_v1 *candidate = deps[0].api;
    if (!risc_i2c_bus_has_safe_contract(candidate)) { fail("rtc i2c unsafe ABI"); return false; }
    bus = candidate;
    if (!bus->claim_device(bus->context, X4PRO_I2C_RTC, &claim) || !claim) {
        fail("rtc claim failed"); (void)quiesce(); return false;
    }
    uint8_t control;
    if (!read_regs(0, &control, 1)) {
        fail("rtc absent/read I/O"); (void)quiesce(); return false;
    }
    /* Presence, not valid-time, admits the provider: an explicit NTP/manual
     * write must be able to repair VL/STOP. No boot initialization writes. */
    started = true;
    last_error_text[0] = 0;
    return true;
}
static void stop(void) { (void)quiesce(); }
static bool last_error(char *out, size_t capacity) {
    if (!out || !capacity || !last_error_text[0]) return false;
    size_t i = 0;
    while (i + 1u < capacity && i < sizeof(last_error_text) - 1u && last_error_text[i]) {
        out[i] = last_error_text[i]; ++i;
    }
    out[i] = 0;
    return true;
}
static const risc_rtc_clock_api_v2 api = {
    RISC_RTC_CLOCK_API_V2, sizeof(api), NULL, read_time, write_time, alarm, alarm_pending
};
static const risc_driver_diagnostics_v2 driver = {
    { RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), "x4pro-rtc",
      RISC_RTC_CLOCK_CAPABILITY, RISC_RTC_CLOCK_API_V2, &api, start, stop, quiesce },
    last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.base : NULL;
}
