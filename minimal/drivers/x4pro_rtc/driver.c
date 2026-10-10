/* X4 BM8563 through the existing exclusive i2c.bus owner, never Wire. */
#include "RiscProviderV2.h"
#include "RiscI2cBusV1.h"
#include "RiscRtcClockV2.h"
#include "RiscRtcCalendarV2.h"
#include <TWatchHardwareV1.h>
#include <string.h>

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
#include <pcf8563_rtc_ops.h>

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
    if (!deps || count != 2u) { fail("rtc typed dependencies"); return false; }
    const risc_hardware_device_v1 *hardware = NULL;
    const risc_i2c_bus_api_v1 *candidate = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!deps[i].capability_id || deps[i].api_version != 1u || !deps[i].api) return false;
        if (!strcmp(deps[i].capability_id, "hardware.device") && !hardware) hardware = deps[i].api;
        else if (!strcmp(deps[i].capability_id, "i2c.bus") && !candidate) candidate = deps[i].api;
        else return false;
    }
    if (!hardware || hardware->api_version != 1u || hardware->struct_size < sizeof(*hardware) ||
        !hardware->instance_id || !hardware->compatible || strcmp(hardware->compatible, "riscrte,pcf8563-compatible-rtc") ||
        !hardware->revision || strcmp(hardware->revision, "unspecified") || !hardware->config_type ||
        strcmp(hardware->config_type, "peripheral.i2c") || hardware->config_version != 1u ||
        hardware->config_size != sizeof(tw_hw_i2c_device_v1) || !hardware->config) return false;
    const tw_hw_i2c_device_v1 *config = hardware->config;
    if (config->struct_size != sizeof(*config) || config->bus.struct_size != sizeof(config->bus) ||
        config->bus.kind != RISC_HW_BUS_I2C || !config->bus.instance_id || config->address != 0x51u ||
        config->irq != -1 || config->irq_active_high || config->irq_pull_up || config->reserved) return false;
    if (!risc_i2c_bus_has_safe_contract(candidate)) { fail("rtc i2c unsafe ABI"); return false; }
    bus = candidate;
    if (!bus->claim_device(bus->context, config->address, &claim) || !claim) {
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
