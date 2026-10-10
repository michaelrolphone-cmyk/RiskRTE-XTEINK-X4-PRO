/* Read-only CW2017 gauge via scoped i2c.bus/GPIO/sync tables. No raw MMIO,
 * OS imports, BATINFO/profile writes, wake or reset operations. */
#include <RiscBatteryGaugeV1.h>
#include <RiscI2cBusV1.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include <TWatchHardwareV1.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CW2017_COMPATIBLE "cellwise,cw2017-readonly-gauge"
#define CW2017_VERSION 0x00u
#define CW2017_VCELL 0x02u
#define CW2017_SOC 0x04u
#define CW2017_CONFIG 0x08u
#define CW2017_TRANSFER_TIMEOUT_MS 20u

static const risc_i2c_bus_api_v1 *bus;
static const garden_gpio_v1 *gpio;
static const risc_provider_sync_api_v1 *sync_api;
static uint64_t bus_claim, gpio_claim, mutex;
static bool started, retained;
static char last_error_text[64];

static void fail(const char *text) {
    size_t i = 0;
    while (i + 1u < sizeof(last_error_text) && text[i]) {
        last_error_text[i] = text[i]; ++i;
    }
    last_error_text[i] = 0;
}
static bool enter(void) {
    return !retained && sync_api && mutex && sync_api->is_owner(sync_api->context) &&
        sync_api->try_lock(sync_api->context, mutex);
}
static bool leave(void) {
    if (!sync_api->unlock(sync_api->context, mutex)) {
        retained = true; fail("cw2017 sync retained"); return false;
    }
    return true;
}
static bool read_reg(uint8_t reg, uint8_t *out, size_t length) {
    return bus && bus_claim && bus->transact(bus->context, bus_claim, &reg, 1, out, length,
                                            CW2017_TRANSFER_TIMEOUT_MS);
}
static bool read_values(risc_battery_sample_v1 *sample) {
    uint8_t version = 0, config = 0, cell[2] = {0}, soc = 0;
    if (!read_reg(CW2017_VERSION, &version, 1)) { fail("cw2017 version read"); return false; }
    /* OEM-derived running revisions. The datasheet POR value is not ready. */
    if ((version & 0xfdu) != 0x0du) {
        fail(version == 0xa0u ? "cw2017 not ready" : "cw2017 version mismatch");
        return false;
    }
    if (!read_reg(CW2017_CONFIG, &config, 1)) { fail("cw2017 config read"); return false; }
    if (config != 0) { fail("cw2017 not in normal mode"); return false; }
    if (!read_reg(CW2017_VCELL, cell, 2)) { fail("cw2017 voltage read"); return false; }
    if (!read_reg(CW2017_SOC, &soc, 1)) { fail("cw2017 soc read"); return false; }
    if ((cell[0] & 0xc0u) || (!cell[0] && !cell[1])) {
        fail("cw2017 invalid voltage"); return false;
    }
    if (soc > 100u) { fail("cw2017 invalid soc"); return false; }
    const uint16_t raw = (uint16_t)(((uint16_t)cell[0] << 8) | cell[1]);
    /* Source-preserving 312.5 uV/count conversion, nearest integer mV. */
    const uint16_t millivolts = (uint16_t)(((uint32_t)raw * 5u + 8u) >> 4);
    if (!millivolts) { fail("cw2017 invalid voltage"); return false; }
    sample->millivolts = millivolts;
    sample->percent = soc;
    return true;
}
static bool read_sample(void *context, risc_battery_sample_v1 *out) {
    (void)context;
    if (!out || !enter()) return false;
    if (!started) { (void)leave(); return false; }
    risc_battery_sample_v1 sample = {0};
    bool charging = false;
    bool okay = read_values(&sample);
    if (okay && !gpio->read(gpio->context, gpio_claim, &charging)) {
        fail("cw2017 charging read"); okay = false;
    }
    sample.charging = charging ? 1u : 0u;
    if (!leave() || !okay) return false;
    *out = sample; /* Commit only after every read, check and guard release. */
    last_error_text[0] = 0;
    return true;
}
/* Called only under the guard. False retains the exact token and dependency;
 * a later quiesce/stop may retry without making further gauge reads. */
static bool release_claims(void) {
    started = false;
    bool okay = true;
    if (gpio_claim) {
        if (!gpio->release(gpio->context, gpio_claim)) {
            fail("cw2017 gpio release pending"); okay = false;
        } else gpio_claim = 0;
    }
    if (bus_claim) {
        if (!bus->release_device(bus->context, bus_claim)) {
            fail("cw2017 release pending"); okay = false;
        } else bus_claim = 0;
    }
    return okay;
}
static bool finish_cleanup(void) {
    if (!sync_api->destroy(sync_api->context, mutex)) {
        fail("cw2017 sync destroy pending"); return false;
    }
    mutex = 0;
    bus = NULL; gpio = NULL; sync_api = NULL;
    return true;
}
static bool quiesce(void) {
    if (retained) return false;
    if (!mutex) return !bus_claim && !gpio_claim;
    if (!enter()) return false;
    const bool okay = release_claims();
    if (!leave() || !okay) return false;
    return finish_cleanup();
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || retained || bus || gpio || sync_api || mutex || bus_claim || gpio_claim) {
        fail("cw2017 already owned"); return false;
    }
    last_error_text[0] = 0;
    if (!deps || count != 4u) { fail("cw2017 typed dependencies"); return false; }
    const risc_hardware_device_v1 *hardware = NULL;
    const risc_i2c_bus_api_v1 *candidate_bus = NULL;
    const garden_gpio_v1 *candidate_gpio = NULL;
    const risc_provider_sync_api_v1 *sync = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!deps[i].capability_id || deps[i].api_version != 1u || !deps[i].api) {
            fail("cw2017 typed dependencies"); return false;
        }
        const char *name = deps[i].capability_id;
        if (!strcmp(name, "hardware.device") && !hardware) hardware = deps[i].api;
        else if (!strcmp(name, "i2c.bus") && !candidate_bus) candidate_bus = deps[i].api;
        else if (!strcmp(name, "platform.gpio") && !candidate_gpio) candidate_gpio = deps[i].api;
        else if (!strcmp(name, RISC_PROVIDER_SYNC_CAPABILITY) && !sync) sync = deps[i].api;
        else { fail("cw2017 typed dependencies"); return false; }
    }
    if (!hardware || hardware->api_version != 1u || hardware->struct_size < sizeof(*hardware) ||
        !hardware->instance_id || !hardware->compatible || strcmp(hardware->compatible, CW2017_COMPATIBLE) ||
        !hardware->revision || strcmp(hardware->revision, "unspecified") ||
        !hardware->config_type || strcmp(hardware->config_type, "peripheral.i2c") ||
        hardware->config_version != 1u || hardware->config_size != sizeof(tw_hw_i2c_device_v1) ||
        !hardware->config) { fail("cw2017 hardware descriptor"); return false; }
    const tw_hw_i2c_device_v1 *config = hardware->config;
    /* chip_id=0: VERSION is a running-state check, not a fixed silicon ID.
     * The loader scopes i2c.bus to this nonzero bus instance. GPIO authority is
     * only this descriptor's charge-status input; no bus pins are claimed. */
    if (config->struct_size != sizeof(*config) || config->bus.struct_size != sizeof(config->bus) ||
        config->bus.kind != RISC_HW_BUS_I2C || !config->bus.instance_id || config->bus.mode ||
        config->bus.reserved[0] || config->bus.reserved[1] || config->bus.reserved[2] ||
        config->address != 0x63u || config->chip_id || config->irq != 21 ||
        config->irq_active_high != 1u || config->irq_pull_up || config->reserved) {
        fail("cw2017 hardware config"); return false;
    }
    if (!risc_i2c_bus_has_safe_contract(candidate_bus)) { fail("cw2017 i2c abi"); return false; }
    if (!candidate_gpio || candidate_gpio->api_version != 1u ||
        candidate_gpio->struct_size < offsetof(garden_gpio_v1, release) + sizeof(candidate_gpio->release) ||
        !candidate_gpio->claim || !candidate_gpio->read || !candidate_gpio->release ||
        !sync || sync->api_version != 1u || sync->struct_size < sizeof(*sync) ||
        !sync->is_owner || !sync->create || !sync->try_lock || !sync->unlock || !sync->destroy ||
        !sync->is_owner(sync->context)) { fail("cw2017 platform abi"); return false; }
    bus = candidate_bus; gpio = candidate_gpio; sync_api = sync;
    if (!sync_api->create(sync_api->context, &mutex) || !mutex) {
        fail("cw2017 sync create");
        if (mutex) return false;
        bus = NULL; gpio = NULL; sync_api = NULL; return false;
    }
    if (!enter()) { fail("cw2017 sync busy"); return false; }
    bool okay = bus->claim_device(bus->context, config->address, &bus_claim) && bus_claim;
    if (!okay) fail("cw2017 claim");
    risc_battery_sample_v1 initial = {0};
    if (okay) okay = read_values(&initial);
    /* Preserve read-only startup: validate the gauge before claiming GPIO21.
     * HIGH means charging only, not VBUS presence or full/healthy battery. */
    if (okay && (!gpio->claim(gpio->context, (uint8_t)config->irq, false, false, false,
                              &gpio_claim) || !gpio_claim)) {
        fail("cw2017 gpio claim"); okay = false;
    }
    if (!okay) {
        const bool released = release_claims();
        if (leave() && released) (void)finish_cleanup();
        return false;
    }
    started = true;
    if (!leave()) return false;
    last_error_text[0] = 0;
    return true;
}
static void stop(void) { (void)quiesce(); }
static bool last_error(char *destination, size_t capacity) {
    if (!destination || !capacity || !last_error_text[0]) return false;
    size_t i = 0;
    while (i + 1u < capacity && i < sizeof(last_error_text) - 1u && last_error_text[i]) {
        destination[i] = last_error_text[i]; ++i;
    }
    destination[i] = 0;
    return true;
}
static const risc_battery_gauge_api_v1 api = {
    RISC_BATTERY_GAUGE_API_V1, sizeof(api), NULL, read_sample
};
static const risc_driver_diagnostics_v2 driver = {
    { RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), "x4pro-battery",
      "board.battery", 1, &api, start, stop, quiesce }, last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.base : NULL;
}
