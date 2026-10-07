/* Read-only X4 Pro CW2017 gauge through i2c.bus. No BATINFO/reset writes. */
#include "RiscBatteryGaugeV1.h"
#include "RiscI2cBusV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include "x4pro_proto.h"
#include <stddef.h>
#include <stdint.h>

/* Cellwise CW2017: CONFIG=0 for normal operation, VCELL unsigned 14-bit.
 * FreeInk/OEM readiness: VERSION=0x0d/0x0f running; 0xa0 is POR, not ready.
 * SOC_H is the integer percentage. Sources/evidence limits: README.md.
 * Each call performs at most four fixed 1+1/1+2 byte synchronous transfers,
 * with no polling or retries. Require the tagged serialized/deadline/drained
 * release contract before any I/O; legacy unsafe bus packages fail closed. */
#define CW2017_VERSION 0x00u
#define CW2017_VCELL 0x02u
#define CW2017_SOC 0x04u
#define CW2017_CONFIG 0x08u
#define CW2017_RUNNING_VERSION 0x0du
#define CW2017_RUNNING_VERSION_MASK 0xfdu
#define CW2017_TRANSFER_TIMEOUT_MS 20u

static const risc_i2c_bus_api_v1 *bus;
static uint64_t claim;
static bool started;
static char last_error_text[64];

static bool is_i2c_bus(const char *name) {
    static const char expected[] = "i2c.bus";
    if (!name) return false;
    /* Include the NUL; even a malformed name cannot cause an unbounded scan. */
    for (size_t i = 0; i < sizeof(expected); ++i)
        if (name[i] != expected[i]) return false;
    return true;
}
static void fail(const char *text) {
    size_t i = 0;
    while (i + 1u < sizeof(last_error_text) && text[i]) {
        last_error_text[i] = text[i]; ++i;
    }
    last_error_text[i] = 0;
}
static bool read_reg(uint8_t reg, uint8_t *out, size_t length) {
    return bus && claim && bus->transact(bus->context, claim, &reg, 1, out, length,
                                        CW2017_TRANSFER_TIMEOUT_MS);
}
static bool read_values(risc_battery_sample_v1 *sample) {
    uint8_t version = 0, config = 0, cell[2] = {0}, soc = 0;
    if (!read_reg(CW2017_VERSION, &version, 1)) { fail("cw2017 version read"); return false; }
    if ((version & CW2017_RUNNING_VERSION_MASK) != CW2017_RUNNING_VERSION) {
        fail(version == 0xa0u ? "cw2017 not ready" : "cw2017 version mismatch");
        return false;
    }
    if (!read_reg(CW2017_CONFIG, &config, 1)) { fail("cw2017 config read"); return false; }
    /* A sleeping/reset gauge can return stale or POR zero measurements. Do
     * not wake/reconfigure it or claim those values are a valid sample. */
    if (config != 0) { fail("cw2017 not in normal mode"); return false; }
    if (!read_reg(CW2017_VCELL, cell, 2)) { fail("cw2017 voltage read"); return false; }
    if (!read_reg(CW2017_SOC, &soc, 1)) { fail("cw2017 soc read"); return false; }
    if ((cell[0] & 0xc0u) || (!cell[0] && !cell[1])) {
        fail("cw2017 invalid voltage"); return false;
    }
    if (soc > 100u) { fail("cw2017 invalid soc"); return false; }
    const uint16_t millivolts = x4pro_cw2017_millivolts(cell[0], cell[1]);
    if (!millivolts) { fail("cw2017 invalid voltage"); return false; }
    sample->millivolts = millivolts;
    sample->percent = soc;
    return true;
}
static bool read(void *context, risc_battery_sample_v1 *out) {
    (void)context;
    if (!started || !out) return false;
    risc_battery_sample_v1 sample = {0};
    if (!read_values(&sample)) return false;
    /* OEM Cw2017PowerHal uses GPIO21 input/no-pull, active HIGH. This is
     * charging only; neither VBUS presence nor charge-full is observable. */
    sample.charging = x4pro_pin_read(X4PRO_PIN_CHG_STAT) ? 1u : 0u;
    *out = sample; /* Commit only after every read and validity check passes. */
    last_error_text[0] = 0;
    return true;
}
static const risc_battery_gauge_api_v1 api = {
    RISC_BATTERY_GAUGE_API_V1, sizeof(api), 0, read
};
static bool release_claim(void) {
    started = false;
    if (claim) {
        /* Retain both the dependency pointer and token if draining fails.
         * quiesce=false keeps this ELF and its bus dependency pinned. */
        if (!bus || !bus->release_device(bus->context, claim)) {
            fail("cw2017 release pending"); return false;
        }
        claim = 0;
    }
    bus = 0;
    return true;
}
static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    /* Never discard an active or retained claim on a repeated start. */
    if (started || bus || claim) { fail("cw2017 already owned"); return false; }
    last_error_text[0] = 0;
    /* This manifest requires exactly one dependency. Bound validation before
     * touching the array, and reject duplicates/extras rather than scan them. */
    if (!dependencies || count != 1u || !is_i2c_bus(dependencies[0].capability_id) ||
        dependencies[0].api_version != RISC_I2C_BUS_API_V1 || !dependencies[0].api) {
        fail("cw2017 i2c dependency"); return false;
    }
    const risc_i2c_bus_api_v1 *candidate = dependencies[0].api;
    if (!risc_i2c_bus_has_safe_contract(candidate)) {
        fail("cw2017 i2c abi"); return false;
    }
    bus = candidate;
    if (!bus->claim_device(bus->context, X4PRO_I2C_CW2017, &claim) || !claim) {
        fail("cw2017 claim"); (void)release_claim(); return false;
    }
    risc_battery_sample_v1 initial = {0};
    if (!read_values(&initial)) { (void)release_claim(); return false; }
    x4pro_pin_input(X4PRO_PIN_CHG_STAT, false);
    started = true;
    return true;
}
static void stop(void) { (void)release_claim(); }
static bool quiesce(void) { return release_claim(); }
static bool last_error(char *destination, size_t capacity) {
    if (!destination || !capacity || !last_error_text[0]) return false;
    size_t i = 0;
    while (i + 1u < capacity && i < sizeof(last_error_text) - 1u && last_error_text[i]) {
        destination[i] = last_error_text[i]; ++i;
    }
    destination[i] = 0;
    return true;
}
static const risc_driver_diagnostics_v2 driver = {
    { RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2), "x4pro-battery",
      "board.battery", 1, &api, start, stop, quiesce },
    last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver.base;
}
