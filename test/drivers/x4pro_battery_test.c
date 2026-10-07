/* Link the real driver separately: assertions observe only its public ABI. */
#include "RiscBatteryGaugeV1.h"
#include "RiscI2cBusV1.h"
#include "x4pro_pins.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const risc_driver_v2 *driver;
static const risc_driver_diagnostics_v2 *diagnostics;
static const risc_battery_gauge_api_v1 *gauge;
static risc_i2c_bus_contract_v1 contract;
#define bus (contract.base)
static risc_provider_dependency_v1 dependency;
static uint64_t active_token, serial = 10;
static unsigned claims, releases, transactions, input_calls, pin_reads;
static bool claim_ok, issue_token, release_ok, charge_high;
static int fail_register;
static uint8_t version, config, soc;
static uint16_t cell;
static int context_cookie;

void x4pro_pin_input(uint32_t pin, bool pullup) {
    assert(pin == 21u && pin == X4PRO_PIN_CHG_STAT && !pullup);
    ++input_calls;
}
bool x4pro_pin_read(uint32_t pin) {
    assert(pin == X4PRO_PIN_CHG_STAT && input_calls == 1u);
    ++pin_reads;
    return charge_high;
}
static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    assert(context == &context_cookie && address == 0x63u && out && !active_token);
    ++claims;
    *out = issue_token ? (active_token = ++serial) : 0;
    return claim_ok;
}
static bool release_device(void *context, uint64_t token) {
    assert(context == &context_cookie && token && token == active_token);
    ++releases;
    if (!release_ok) return false;
    active_token = 0;
    return true;
}
static bool transact(void *context, uint64_t token, const uint8_t *write_bytes,
                     size_t write_length, uint8_t *read_bytes, size_t read_length,
                     uint32_t timeout_ms) {
    assert(context == &context_cookie && token && token == active_token);
    /* Only register-pointer writes, with fixed finite reads. No configuration,
     * reset, profile or other chip writes may escape this fake. */
    assert(write_bytes && write_length == 1u && read_bytes && timeout_ms == 20u);
    ++transactions;
    const uint8_t reg = write_bytes[0];
    assert(read_length == (reg == 0x02u ? 2u : 1u));
    if (reg == fail_register) { memset(read_bytes, 0xff, read_length); return false; }
    switch (reg) {
    case 0x00: read_bytes[0] = version; break;
    case 0x08: read_bytes[0] = config; break;
    case 0x02: read_bytes[0] = (uint8_t)(cell >> 8); read_bytes[1] = (uint8_t)cell; break;
    case 0x04: read_bytes[0] = soc; break;
    default: assert(!"unexpected gauge register"); return false;
    }
    return true;
}
static void reset_fixture(void) {
    release_ok = true;
    assert(driver->quiesce());
    assert(!active_token);
    claims = releases = transactions = input_calls = pin_reads = 0;
    claim_ok = issue_token = release_ok = true;
    charge_high = false;
    fail_register = -1;
    version = 0x0d; config = 0; soc = 55; cell = 0x3200;
    contract = (risc_i2c_bus_contract_v1){
        {RISC_I2C_BUS_API_V1, sizeof(contract), &context_cookie, claim_device, transact, release_device},
        RISC_I2C_BUS_CONTRACT_TAG, RISC_I2C_BUS_CONTRACT_V1, RISC_I2C_BUS_SAFE_CONTRACT_FLAGS};
    dependency = (risc_provider_dependency_v1){"i2c.bus", RISC_I2C_BUS_API_V1, &bus};
}
static void error_is(const char *expected) {
    char text[64];
    assert(diagnostics->last_error(text, sizeof(text)));
    assert(strcmp(text, expected) == 0);
    char short_text[2] = {'x', 'x'};
    assert(diagnostics->last_error(short_text, sizeof(short_text)) && short_text[1] == 0);
    assert(!diagnostics->last_error(NULL, 10));
    assert(!diagnostics->last_error(text, 0));
}
static void no_activity(void) {
    assert(!claims && !releases && !transactions && !input_calls && !pin_reads);
    assert(driver->quiesce());
    driver->stop();
    assert(!releases);
}
static void sample_fails_unchanged(void) {
    risc_battery_sample_v1 out = {1234, 42, 7};
    const risc_battery_sample_v1 before = out;
    const unsigned pins_before = pin_reads, transfers_before = transactions;
    assert(!gauge->read(gauge->context, &out));
    assert(memcmp(&out, &before, sizeof(out)) == 0);
    assert(pin_reads == pins_before && transactions - transfers_before <= 4u);
}
static void start_fails_cleanly(const char *expected) {
    assert(!driver->start(&dependency, 1));
    error_is(expected);
    assert(claims == 1u && releases == 1u && !active_token && !input_calls);
    assert(transactions <= 4u);
    sample_fails_unchanged();
    driver->stop();
    assert(driver->quiesce());
    assert(releases == 1u); /* no double release after failed activation */
}
static void dependencies_test(void) {
    reset_fixture();
    assert(!driver->start(NULL, 0));
    assert(!driver->start(NULL, 1));
    assert(!driver->start(&dependency, 0));
    assert(!driver->start(&dependency, SIZE_MAX));
    risc_provider_dependency_v1 duplicate[2] = {dependency, dependency};
    assert(!driver->start(duplicate, 2));
    dependency.capability_id = NULL; assert(!driver->start(&dependency, 1));
    dependency.capability_id = ""; assert(!driver->start(&dependency, 1));
    dependency.capability_id = "i2c.bus.extra"; assert(!driver->start(&dependency, 1));
    const char unterminated[8] = {'i','2','c','.','b','u','s','x'};
    dependency.capability_id = unterminated; assert(!driver->start(&dependency, 1));
    dependency.capability_id = "i2c.bus";
    dependency.api_version = 99; assert(!driver->start(&dependency, 1));
    dependency.api_version = 1; dependency.api = NULL;
    assert(!driver->start(&dependency, 1));
    error_is("cw2017 i2c dependency");
    no_activity();

    reset_fixture();
    /* A physically short ABI header must be rejected before reading callbacks. */
    const uint32_t header[2] = {1u, 0u};
    dependency.api = header; assert(!driver->start(&dependency, 1));
    dependency.api = &bus;
    bus.struct_size = sizeof(bus) - 1u; assert(!driver->start(&dependency, 1));
    bus.struct_size = sizeof(contract); bus.api_version = 99;
    assert(!driver->start(&dependency, 1));
    bus.api_version = 1; bus.claim_device = NULL; assert(!driver->start(&dependency, 1));
    bus.claim_device = claim_device; bus.transact = NULL; assert(!driver->start(&dependency, 1));
    bus.transact = transact; bus.release_device = NULL; assert(!driver->start(&dependency, 1));
    bus.release_device = release_device;
    contract.contract_tag ^= 1; assert(!driver->start(&dependency, 1));
    contract.contract_tag ^= 1; contract.contract_version = 2;
    assert(!driver->start(&dependency, 1)); contract.contract_version = 1;
    for (unsigned bit = 0; bit < 3; ++bit) {
        contract.contract_flags = RISC_I2C_BUS_SAFE_CONTRACT_FLAGS & ~(1u << bit);
        assert(!driver->start(&dependency, 1));
    }
    /* Legacy size and unrelated oversized suffixes fail before any I/O. */
    const risc_i2c_bus_api_v1 legacy = {1, sizeof(legacy), &context_cookie,
                                     claim_device, transact, release_device};
    dependency.api = &legacy; assert(!driver->start(&dependency, 1));
    error_is("cw2017 i2c abi");
    no_activity();
}
static void startup_test(void) {
    reset_fixture(); claim_ok = issue_token = false;
    assert(!driver->start(&dependency, 1));
    error_is("cw2017 claim");
    assert(claims == 1 && !releases && !transactions && !active_token);
    reset_fixture(); issue_token = false;
    assert(!driver->start(&dependency, 1));
    assert(claims == 1 && !releases && !transactions);
    /* Defensively retain/clean a token even if a broken provider returns false. */
    reset_fixture(); claim_ok = false;
    start_fails_cleanly("cw2017 claim");

    for (unsigned v = 0; v <= 255u; ++v) {
        if (v == 0x0du || v == 0x0fu) continue;
        reset_fixture(); version = (uint8_t)v;
        start_fails_cleanly(v == 0xa0u ? "cw2017 not ready" : "cw2017 version mismatch");
    }
    const uint8_t running[] = {0x0d, 0x0f};
    for (size_t i = 0; i < sizeof(running); ++i) {
        reset_fixture(); version = running[i];
        assert(driver->start(&dependency, 1));
        assert(claims == 1u && transactions == 4u && input_calls == 1u);
    }
    const int regs[] = {0x00, 0x08, 0x02, 0x04};
    const char *errors[] = {"cw2017 version read", "cw2017 config read",
                           "cw2017 voltage read", "cw2017 soc read"};
    for (size_t i = 0; i < sizeof(regs)/sizeof(regs[0]); ++i) {
        reset_fixture(); fail_register = regs[i];
        start_fails_cleanly(errors[i]);
        fail_register = -1;
        assert(driver->start(&dependency, 1)); /* retry after failure is usable */
        assert(claims == 2 && active_token && input_calls == 1);
    }
    const uint8_t modes[] = {0xf0, 0xc0, 0x30, 0x01};
    for (size_t i = 0; i < sizeof(modes); ++i) {
        reset_fixture(); config = modes[i];
        start_fails_cleanly("cw2017 not in normal mode");
    }
    reset_fixture(); cell = 0; start_fails_cleanly("cw2017 invalid voltage");
    reset_fixture(); cell = 1; start_fails_cleanly("cw2017 invalid voltage"); /* rounds to zero mV */
    reset_fixture(); cell = 0xffff; start_fails_cleanly("cw2017 invalid voltage");
    reset_fixture(); soc = 101; start_fails_cleanly("cw2017 invalid soc");
    reset_fixture(); soc = 255; start_fails_cleanly("cw2017 invalid soc");
}
static void samples_test(void) {
    reset_fixture();
    sample_fails_unchanged();
    assert(driver->start(&dependency, 1));
    assert(claims == 1 && transactions == 4 && input_calls == 1);
    assert(!gauge->read(NULL, NULL) && transactions == 4);
    /* Repeated start, even with nonsense dependencies, cannot orphan the claim. */
    const uint64_t original = active_token;
    assert(!driver->start(NULL, SIZE_MAX));
    assert(!driver->start(&dependency, 1));
    assert(claims == 1 && active_token == original && !releases && input_calls == 1);
    risc_battery_sample_v1 out = {0};
    assert(gauge->read(NULL, &out));
    assert(out.millivolts == 4000 && out.percent == 55 && out.charging == 0);
    charge_high = true; version = 0x0f; soc = 100;
    assert(gauge->read(NULL, &out));
    assert(out.percent == 100 && out.charging == 1);
    soc = 0; assert(gauge->read(NULL, &out) && out.percent == 0);
    const int regs[] = {0x00, 0x08, 0x02, 0x04};
    for (size_t i = 0; i < sizeof(regs)/sizeof(regs[0]); ++i) {
        fail_register = regs[i]; sample_fails_unchanged();
    }
    fail_register = -1;
    version = 0xa0; sample_fails_unchanged(); error_is("cw2017 not ready");
    version = 0xff; sample_fails_unchanged();
    version = 0x0d; config = 0xf0; sample_fails_unchanged();
    config = 0; cell = 0; sample_fails_unchanged();
    cell = 1; sample_fails_unchanged(); /* nonzero raw must not publish zero mV */
    cell = 0x7200; sample_fails_unchanged();
    cell = 0x3200; soc = 101; sample_fails_unchanged();
    soc = 255; sample_fails_unchanged(); error_is("cw2017 invalid soc");
    soc = 55; assert(gauge->read(NULL, &out));
    char error[64]; assert(!diagnostics->last_error(error, sizeof(error)));
    assert(driver->quiesce()); driver->stop(); assert(driver->quiesce());
    assert(releases == 1 && !active_token);
    sample_fails_unchanged();
}
static void retained_cleanup_test(void) {
    reset_fixture();
    assert(driver->start(&dependency, 1));
    const uint64_t original = active_token;
    release_ok = false;
    assert(!driver->quiesce());
    assert(active_token == original && releases == 1);
    error_is("cw2017 release pending");
    sample_fails_unchanged();
    assert(!driver->start(&dependency, 1) && claims == 1);
    driver->stop();
    assert(active_token == original && releases == 2);
    release_ok = true;
    assert(driver->quiesce() && !active_token && releases == 3);
    driver->stop(); assert(driver->quiesce()); assert(releases == 3);
    /* Full restart receives a fresh token after the retained one finally drains. */
    input_calls = 0;
    assert(driver->start(&dependency, 1) && active_token != original);
    assert(claims == 2);
    reset_fixture(); fail_register = 0; release_ok = false;
    assert(!driver->start(&dependency, 1));
    const uint64_t failed_start_token = active_token;
    assert(failed_start_token && releases == 1 && !input_calls);
    error_is("cw2017 release pending");
    assert(!driver->start(&dependency, 1) && claims == 1);
    assert(!driver->quiesce() && releases == 2 && active_token == failed_start_token);
    release_ok = true;
    driver->stop();
    assert(!active_token && releases == 3);
    driver->stop(); assert(driver->quiesce()); assert(releases == 3);
    fail_register = -1;
    assert(driver->start(&dependency, 1) && claims == 2);
    assert(driver->quiesce());
}
int main(void) {
    assert(!t5_driver_get(1));
    driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->struct_size == sizeof(risc_driver_diagnostics_v2));
    assert(strcmp(driver->driver_id, "x4pro-battery") == 0);
    assert(strcmp(driver->capability_id, "board.battery") == 0);
    diagnostics = (const risc_driver_diagnostics_v2 *)driver;
    gauge = driver->capability;
    assert(gauge && gauge->api_version == 1 && gauge->struct_size == sizeof(*gauge));
    dependencies_test(); startup_test(); samples_test(); retained_cleanup_test();
    puts("x4pro battery: dependency/ABI, readiness, atomic samples, GPIO, retained cleanup/retry passed");
    return 0;
}
