/* Link the actual ordinary provider. Fakes expose only scoped public tables. */
#include <RiscBatteryGaugeV1.h>
#include <RiscI2cBusV1.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include <TWatchHardwareV1.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const risc_driver_v2 *driver;
static const risc_driver_diagnostics_v2 *diagnostics;
static const risc_battery_gauge_api_v1 *gauge;
static uint64_t bus_token, gpio_token, lock_token, serial = 10;
static unsigned claims, releases, transactions, gpio_claims, gpio_reads, gpio_releases;
static unsigned creates, destroys, takes, gives;
static bool owner = true, locked, create_ok = true, lock_ok = true, unlock_ok = true, destroy_ok = true;
static bool claim_ok = true, issue_token = true, release_ok = true;
static bool gpio_claim_ok = true, gpio_issue_token = true, gpio_read_ok = true, gpio_release_ok = true;
static bool charge_high, recurse;
static int fail_register = -1;
static uint8_t version = 0x0d, mode, soc = 55;
static uint16_t cell = 0x3200;
static int bus_context, gpio_context, sync_context;

static bool is_owner(void *context) { assert(context == &sync_context); return owner; }
static bool create(void *context, uint64_t *out) {
    assert(context == &sync_context && owner && !lock_token && out);
    ++creates; *out = 0;
    if (!create_ok) return false;
    *out = lock_token = ++serial; return true;
}
static bool take(void *context, uint64_t token) {
    assert(context == &sync_context && owner && token && token == lock_token);
    ++takes;
    if (locked || !lock_ok) return false;
    locked = true; return true;
}
static bool give(void *context, uint64_t token) {
    assert(context == &sync_context && owner && locked && token == lock_token);
    ++gives;
    if (!unlock_ok) return false;
    locked = false; return true;
}
static bool destroy(void *context, uint64_t token) {
    assert(context == &sync_context && owner && !locked && token == lock_token);
    ++destroys;
    if (!destroy_ok) return false;
    lock_token = 0; return true;
}
static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    assert(context == &bus_context && owner && locked && address == 0x63u && out && !bus_token);
    ++claims;
    *out = issue_token ? (bus_token = ++serial) : 0;
    return claim_ok;
}
static bool release_device(void *context, uint64_t token) {
    assert(context == &bus_context && owner && locked && token && token == bus_token);
    ++releases;
    if (!release_ok) return false;
    bus_token = 0; return true;
}
static bool transact(void *context, uint64_t token, const uint8_t *tx, size_t tn,
                     uint8_t *rx, size_t rn, uint32_t timeout_ms) {
    assert(context == &bus_context && owner && locked && token && token == bus_token);
    /* Only a single register pointer plus a repeated-START read. Every fake
     * rejects reset, profile or any other register-data writes. */
    assert(tx && tn == 1u && rx && timeout_ms == 20u);
    ++transactions;
    const uint8_t reg = tx[0];
    assert(rn == (reg == 0x02u ? 2u : 1u));
    if (recurse) {
        risc_battery_sample_v1 out = {1234, 42, 7}, before = out;
        const unsigned old_transactions = transactions;
        assert(!gauge->read(NULL, &out) && !memcmp(&out, &before, sizeof(out)));
        assert(!driver->quiesce() && !driver->start(NULL, 0));
        assert(transactions == old_transactions && bus_token);
    }
    if (reg == fail_register) { memset(rx, 0xff, rn); return false; }
    switch (reg) {
    case 0x00: rx[0] = version; break;
    case 0x08: rx[0] = mode; break;
    case 0x02: rx[0] = (uint8_t)(cell >> 8); rx[1] = (uint8_t)cell; break;
    case 0x04: rx[0] = soc; break;
    default: assert(!"unexpected gauge register"); return false;
    }
    return true;
}
static bool claim_pin(void *context, uint8_t pin, bool output, bool initial,
                      bool pullup, uint64_t *out) {
    assert(context == &gpio_context && owner && locked && out && !gpio_token);
    assert(pin == 21 && !output && !initial && !pullup);
    assert(transactions >= 4); /* Gauge readiness must precede GPIO changes. */
    ++gpio_claims;
    *out = gpio_issue_token ? (gpio_token = ++serial) : 0;
    return gpio_claim_ok;
}
static bool read_pin(void *context, uint64_t token, bool *high) {
    assert(context == &gpio_context && owner && locked && token && token == gpio_token && high);
    ++gpio_reads; *high = charge_high;
    return gpio_read_ok;
}
static bool release_pin(void *context, uint64_t token) {
    assert(context == &gpio_context && owner && locked && token && token == gpio_token);
    ++gpio_releases;
    if (!gpio_release_ok) return false;
    gpio_token = 0; return true;
}
static risc_i2c_bus_contract_v1 contract = {
    {1, sizeof(contract), &bus_context, claim_device, transact, release_device},
    RISC_I2C_BUS_CONTRACT_TAG, RISC_I2C_BUS_CONTRACT_V1, RISC_I2C_BUS_SAFE_CONTRACT_FLAGS
};
static garden_gpio_v1 gpio = {
    .api_version=1, .struct_size=sizeof(gpio), .context=&gpio_context,
    .claim=claim_pin, .read=read_pin, .release=release_pin
};
static risc_provider_sync_api_v1 sync_api = {
    1, sizeof(sync_api), &sync_context, is_owner, create, take, give, destroy
};
static tw_hw_i2c_device_v1 config = {
    .struct_size=sizeof(config),
    .bus={.struct_size=sizeof(config.bus), .kind=RISC_HW_BUS_I2C, .instance_id=101,
          .frequency_hz=400000, .sclk=-1, .mosi=-1, .miso=-1, .sda=39, .scl=38},
    .address=0x63, .irq=21, .irq_active_high=1
};
static risc_hardware_device_v1 hardware = {
    1, sizeof(hardware), 8, "cellwise,cw2017-readonly-gauge", "unspecified",
    "peripheral.i2c", 1, sizeof(config), &config
};
static risc_provider_dependency_v1 deps[] = {
    {"hardware.device", 1, &hardware}, {"i2c.bus", 1, &contract},
    {"platform.gpio", 1, &gpio}, {"platform.sync", 1, &sync_api}
};
static bool start(void) { return driver->start(deps, 4); }
static void clean(void) { assert(!bus_token && !gpio_token && !lock_token && !locked); }
static void reset_fixture(void) {
    assert(owner && unlock_ok);
    release_ok = gpio_release_ok = destroy_ok = lock_ok = true;
    assert(driver->quiesce()); clean();
    claims = releases = transactions = gpio_claims = gpio_reads = gpio_releases = 0;
    creates = destroys = takes = gives = 0;
    create_ok = claim_ok = issue_token = gpio_claim_ok = gpio_issue_token = gpio_read_ok = true;
    charge_high = recurse = false;
    fail_register = -1; version = 0x0d; mode = 0; soc = 55; cell = 0x3200;
}
static void error_is(const char *expected) {
    char text[64];
    assert(diagnostics->last_error(text, sizeof(text)) && !strcmp(text, expected));
    char tiny[2] = {'x', 'x'};
    assert(diagnostics->last_error(tiny, sizeof(tiny)) && tiny[1] == 0);
    assert(!diagnostics->last_error(NULL, 10) && !diagnostics->last_error(text, 0));
}
static void sample_fails_unchanged(void) {
    risc_battery_sample_v1 out = {1234, 42, 7}, before = out;
    const unsigned old_transactions = transactions;
    assert(!gauge->read(NULL, &out) && !memcmp(&out, &before, sizeof(out)));
    assert(transactions - old_transactions <= 4u);
}
static void start_fails_cleanly(const char *expected) {
    assert(!start()); error_is(expected);
    assert(claims == 1 && releases == 1 && transactions <= 4 && !gpio_claims);
    clean(); sample_fails_unchanged();
    driver->stop(); assert(driver->quiesce() && releases == 1); clean();
}
static void validation(void) {
    reset_fixture();
    assert(!driver->start(NULL, 4) && !driver->start(deps, 0) && !driver->start(deps, 3));
    assert(!driver->start(deps, SIZE_MAX));
    for (size_t i = 0; i < 4; ++i) {
        risc_provider_dependency_v1 saved = deps[i];
        deps[i].capability_id = NULL; assert(!start());
        deps[i].capability_id = ""; assert(!start());
        deps[i].capability_id = "i2c.bus.extra"; assert(!start());
        deps[i] = saved; deps[i].api_version = 2; assert(!start());
        deps[i] = saved; deps[i].api = NULL; assert(!start());
        const uint32_t short_header[2] = {1, 0};
        deps[i].api = short_header; assert(!start());
        deps[i] = deps[(i + 1) % 4]; assert(!start());
        deps[i] = saved;
    }
#define REJECT(field, bad) do { \
    const uint64_t saved = (uint64_t)(field); (field) = (bad); \
    assert(!start()); (field) = saved; \
} while (0)
    REJECT(hardware.api_version, 2); REJECT(hardware.struct_size, sizeof(hardware)-1);
    REJECT(hardware.instance_id, 0); REJECT(hardware.config_version, 2);
    REJECT(hardware.config_size, sizeof(config)-1);
    hardware.compatible = "cellwise,cw2015"; assert(!start());
    hardware.compatible = "cellwise,cw2017-readonly-gauge";
    hardware.revision = "unknown"; assert(!start()); hardware.revision = "unspecified";
    hardware.config_type = "gpio.bank"; assert(!start()); hardware.config_type = "peripheral.i2c";
    hardware.config = NULL; assert(!start()); hardware.config = &config;
    REJECT(config.struct_size, sizeof(config)-1); REJECT(config.bus.struct_size, sizeof(config.bus)-1);
    REJECT(config.bus.kind, RISC_HW_BUS_SPI); REJECT(config.bus.instance_id, 0);
    REJECT(config.bus.mode, 1);
    for (unsigned i = 0; i < 3; ++i) { REJECT(config.bus.reserved[i], 1); }
    REJECT(config.address, 0x62); REJECT(config.chip_id, 0x0d);
    REJECT(config.irq, -1); REJECT(config.irq, 20); REJECT(config.irq_active_high, 0);
    REJECT(config.irq_active_high, 2); REJECT(config.irq_pull_up, 1); REJECT(config.reserved, 1);
    REJECT(contract.base.api_version, 2); REJECT(contract.base.struct_size, sizeof(contract)-1);
    REJECT(contract.contract_tag, 0); REJECT(contract.contract_version, 2);
    for (unsigned bit = 0; bit < 3; ++bit) {
        REJECT(contract.contract_flags, RISC_I2C_BUS_SAFE_CONTRACT_FLAGS & ~(1u << bit));
    }
    contract.base.claim_device = NULL; assert(!start()); contract.base.claim_device = claim_device;
    contract.base.transact = NULL; assert(!start()); contract.base.transact = transact;
    contract.base.release_device = NULL; assert(!start()); contract.base.release_device = release_device;
    const risc_i2c_bus_api_v1 legacy = {1, sizeof(legacy), &bus_context, claim_device, transact, release_device};
    deps[1].api = &legacy; assert(!start()); deps[1].api = &contract;
    REJECT(gpio.api_version, 2);
    REJECT(gpio.struct_size, offsetof(garden_gpio_v1, release)+sizeof(gpio.release)-1);
    gpio.claim = NULL; assert(!start()); gpio.claim = claim_pin;
    gpio.read = NULL; assert(!start()); gpio.read = read_pin;
    gpio.release = NULL; assert(!start()); gpio.release = release_pin;
    REJECT(sync_api.api_version, 2); REJECT(sync_api.struct_size, sizeof(sync_api)-1);
    sync_api.is_owner = NULL; assert(!start()); sync_api.is_owner = is_owner;
    sync_api.create = NULL; assert(!start()); sync_api.create = create;
    sync_api.try_lock = NULL; assert(!start()); sync_api.try_lock = take;
    sync_api.unlock = NULL; assert(!start()); sync_api.unlock = give;
    sync_api.destroy = NULL; assert(!start()); sync_api.destroy = destroy;
#undef REJECT
    owner = false; assert(!start()); owner = true;
    assert(!creates && !claims && !transactions && !gpio_claims && !gpio_reads && !releases);
    assert(driver->quiesce()); clean();
    /* Dependency order and additive GPIO suffix size do not create a new ABI. */
    risc_provider_dependency_v1 saved = deps[0]; deps[0] = deps[3]; deps[3] = saved;
    gpio.struct_size = offsetof(garden_gpio_v1, release)+sizeof(gpio.release);
    assert(start() && driver->quiesce()); clean();
    saved = deps[0]; deps[0] = deps[3]; deps[3] = saved; gpio.struct_size = sizeof(gpio);
    reset_fixture(); create_ok = false; assert(!start() && !claims); clean();
    reset_fixture(); lock_ok = false; assert(!start() && lock_token && !claims && !locked);
    assert(!start() && !driver->quiesce()); lock_ok = true; assert(driver->quiesce()); clean();
}
static void startup(void) {
    reset_fixture(); claim_ok = issue_token = false;
    assert(!start() && claims == 1 && !releases && !transactions); clean();
    reset_fixture(); issue_token = false;
    assert(!start() && claims == 1 && !releases && !transactions); clean();
    reset_fixture(); claim_ok = false; start_fails_cleanly("cw2017 claim");
    for (unsigned value = 0; value < 256; ++value) {
        reset_fixture(); version = (uint8_t)value;
        if (value == 0x0d || value == 0x0f) {
            assert(start() && transactions == 4 && gpio_claims == 1 && !gpio_reads);
        } else start_fails_cleanly(value == 0xa0 ? "cw2017 not ready" : "cw2017 version mismatch");
    }
    for (unsigned value = 1; value < 256; ++value) {
        reset_fixture(); mode = (uint8_t)value; start_fails_cleanly("cw2017 not in normal mode");
    }
    const int regs[] = {0, 8, 2, 4};
    const char *errors[] = {"cw2017 version read", "cw2017 config read", "cw2017 voltage read", "cw2017 soc read"};
    for (unsigned i = 0; i < 4; ++i) {
        reset_fixture(); fail_register = regs[i]; start_fails_cleanly(errors[i]);
        fail_register = -1; assert(start() && claims == 2);
    }
    const uint16_t invalid_cells[] = {0, 1, 0x4000, 0x7fff, 0xffff};
    for (unsigned i = 0; i < sizeof(invalid_cells)/sizeof(invalid_cells[0]); ++i) {
        reset_fixture(); cell = invalid_cells[i]; start_fails_cleanly("cw2017 invalid voltage");
    }
    reset_fixture(); soc = 101; start_fails_cleanly("cw2017 invalid soc");
    reset_fixture(); soc = 255; start_fails_cleanly("cw2017 invalid soc");
    reset_fixture(); gpio_claim_ok = gpio_issue_token = false;
    assert(!start() && gpio_claims == 1 && releases == 1 && !gpio_releases); clean();
    reset_fixture(); gpio_issue_token = false;
    assert(!start() && gpio_claims == 1 && releases == 1 && !gpio_releases); clean();
    reset_fixture(); gpio_claim_ok = false;
    assert(!start() && gpio_claims == 1 && releases == 1 && gpio_releases == 1); clean();
}
static void samples(void) {
    reset_fixture(); sample_fails_unchanged(); assert(start() && transactions == 4);
    assert(!gauge->read(NULL, NULL) && transactions == 4);
    const uint64_t saved_bus = bus_token, saved_gpio = gpio_token;
    assert(!start() && !driver->start(NULL, SIZE_MAX));
    assert(bus_token == saved_bus && gpio_token == saved_gpio && claims == 1 && gpio_claims == 1);
    risc_battery_sample_v1 out = {0};
    assert(gauge->read(NULL, &out) && out.millivolts == 4000 && out.percent == 55 && !out.charging);
    charge_high = true; version = 0x0f; soc = 100;
    assert(gauge->read(NULL, &out) && out.percent == 100 && out.charging == 1);
    recurse = true; assert(gauge->read(NULL, &out)); recurse = false;
    const unsigned old_transactions = transactions, old_takes = takes, old_gpio_reads = gpio_reads;
    owner = false; sample_fails_unchanged(); assert(!driver->quiesce()); driver->stop(); owner = true;
    assert(transactions == old_transactions && takes == old_takes && gpio_reads == old_gpio_reads);
    const int regs[] = {0, 8, 2, 4};
    for (unsigned i = 0; i < 4; ++i) {
        fail_register = regs[i]; sample_fails_unchanged(); assert(gpio_reads == old_gpio_reads);
    }
    fail_register = -1;
    for (unsigned value = 0; value < 256; ++value) {
        version = (uint8_t)value;
        if (value == 0x0d || value == 0x0f) assert(gauge->read(NULL, &out));
        else sample_fails_unchanged();
    }
    version = 0x0d;
    for (unsigned value = 1; value < 256; ++value) { mode = (uint8_t)value; sample_fails_unchanged(); }
    mode = 0;
    for (unsigned value = 0; value <= 0xffff; ++value) {
        cell = (uint16_t)value;
        if (value >= 2 && value <= 0x3fff) {
            assert(gauge->read(NULL, &out));
            assert(out.millivolts == (uint16_t)((value * 5u + 8u) >> 4));
        } else sample_fails_unchanged();
    }
    cell = 0x3200;
    for (unsigned value = 0; value < 256; ++value) {
        soc = (uint8_t)value;
        if (value <= 100) assert(gauge->read(NULL, &out) && out.percent == value);
        else sample_fails_unchanged();
    }
    soc = 55; gpio_read_ok = false; sample_fails_unchanged(); error_is("cw2017 charging read");
    gpio_read_ok = true; assert(gauge->read(NULL, &out));
    char text[64]; assert(!diagnostics->last_error(text, sizeof(text)));
    assert(driver->quiesce()); clean(); sample_fails_unchanged();
}
static void lifecycle(void) {
    reset_fixture(); assert(start());
    const uint64_t saved_bus = bus_token, saved_gpio = gpio_token, saved_lock = lock_token;
    release_ok = gpio_release_ok = false;
    assert(!driver->quiesce() && bus_token == saved_bus && gpio_token == saved_gpio && lock_token == saved_lock);
    error_is("cw2017 release pending"); sample_fails_unchanged(); assert(!start());
    const unsigned old_transactions = transactions;
    driver->stop(); assert(releases == 2 && gpio_releases == 2 && transactions == old_transactions);
    release_ok = true; assert(!driver->quiesce() && !bus_token && gpio_token == saved_gpio);
    error_is("cw2017 gpio release pending");
    gpio_release_ok = true; assert(driver->quiesce()); clean();
    assert(releases == 3 && gpio_releases == 4);
    driver->stop(); assert(driver->quiesce() && releases == 3 && gpio_releases == 4);
    assert(start() && bus_token != saved_bus && gpio_token != saved_gpio && lock_token != saved_lock);
    reset_fixture(); fail_register = 0; release_ok = false;
    assert(!start() && bus_token && lock_token && !gpio_token && releases == 1);
    error_is("cw2017 release pending"); sample_fails_unchanged(); assert(!start());
    release_ok = true; driver->stop(); clean();
    fail_register = -1; assert(start());
    destroy_ok = false; assert(!driver->quiesce() && !bus_token && !gpio_token && lock_token && !locked);
    error_is("cw2017 sync destroy pending"); sample_fails_unchanged(); assert(!start());
    destroy_ok = true; assert(driver->quiesce()); clean();
    reset_fixture(); gpio_claim_ok = false; gpio_release_ok = false;
    assert(!start() && gpio_token && !bus_token && lock_token);
    gpio_release_ok = true; assert(driver->quiesce()); clean();
}
static void unlock_retained(bool during_start) {
    reset_fixture();
    if (during_start) { unlock_ok = false; assert(!start()); }
    else { assert(start()); unlock_ok = false; sample_fails_unchanged(); }
    error_is("cw2017 sync retained");
    const uint64_t saved_bus = bus_token, saved_gpio = gpio_token, saved_lock = lock_token;
    const unsigned old_transactions = transactions, old_takes = takes, old_gives = gives;
    assert(saved_bus && saved_gpio && saved_lock && locked);
    unlock_ok = true;
    assert(!driver->quiesce() && !start()); driver->stop(); sample_fails_unchanged();
    assert(bus_token == saved_bus && gpio_token == saved_gpio && lock_token == saved_lock && locked);
    assert(transactions == old_transactions && takes == old_takes && gives == old_gives);
    assert(!releases && !gpio_releases && !destroys);
}
int main(int argc, char **argv) {
    assert(argc == 2 && !t5_driver_get(1));
    driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->struct_size == sizeof(risc_driver_diagnostics_v2));
    assert(!strcmp(driver->driver_id, "x4pro-battery") && !strcmp(driver->capability_id, "board.battery"));
    diagnostics = (const risc_driver_diagnostics_v2 *)driver;
    gauge = driver->capability;
    assert(gauge && gauge->api_version == 1 && gauge->struct_size == sizeof(*gauge));
    if (!strcmp(argv[1], "validation")) validation();
    else if (!strcmp(argv[1], "startup")) startup();
    else if (!strcmp(argv[1], "samples")) samples();
    else if (!strcmp(argv[1], "lifecycle")) lifecycle();
    else if (!strcmp(argv[1], "unlock-retained")) unlock_retained(false);
    else if (!strcmp(argv[1], "start-unlock-retained")) unlock_retained(true);
    else assert(!"Unknown scenario");
    printf("X4 ordinary battery PASS: %s\n", argv[1]);
}
