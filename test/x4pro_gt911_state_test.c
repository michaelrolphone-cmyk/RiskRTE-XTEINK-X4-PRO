#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscTouchV1.h"
#include "x4pro_pins.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern const risc_driver_v2 *t5_driver_get(uint32_t);
static bool pins[49];
static uint64_t now;
static uint8_t status, point[8];
static uint8_t claimed;
static bool reject_release, reject_probe;
static unsigned release_calls, claim_calls, pin_writes;

void fake_pin_output(uint32_t pin, bool level) {
    assert(pin < 49 && pin != X4PRO_PIN_PERIPH_EN); // Bootstrap is the only keep-alive writer.
    ++pin_writes; pins[pin] = level;
}
void fake_pin_level(uint32_t pin, bool level) { fake_pin_output(pin, level); }
void fake_pin_input(uint32_t pin, bool pullup) { (void)pin; (void)pullup; }
static uint64_t tick(void *ctx) { (void)ctx; return now; }
static void sleep_ms(void *ctx, uint32_t ms) { (void)ctx; now += ms; }
static bool claim(void *ctx, uint8_t addr, uint64_t *out) {
    (void)ctx; ++claim_calls;
    if (addr != X4PRO_I2C_GT911 || claimed) return false;
    claimed = addr; *out = 1; return true;
}
static bool release(void *ctx, uint64_t handle) {
    (void)ctx;
    ++release_calls;
    if (handle != 1 || !claimed || reject_release) return false;
    claimed = 0; return true;
}
static bool transact(void *ctx, uint64_t handle, const uint8_t *write, size_t wlen,
                     uint8_t *read, size_t rlen, uint32_t timeout) {
    (void)ctx; (void)timeout;
    if (handle != 1 || !claimed || wlen < 2) return false;
    const uint16_t reg = (uint16_t)((uint16_t)write[0] << 8 | write[1]);
    if (rlen) {
        if (reg == 0x8140 && rlen == 4) { if (reject_probe) return false; memcpy(read, "911\0", 4); return true; }
        if (reg == 0x814e && rlen == 1) { read[0] = status; return true; }
        if (reg == 0x8150 && rlen == 8) { memcpy(read, point, 8); return true; }
        return false;
    }
    if (wlen == 3 && reg == 0x814e && write[2] == 0) { status = 0; return true; }
    return false;
}
int main(void) {
    pins[X4PRO_PIN_PERIPH_EN] = true; // Early board-alive bootstrap, before package acquisition.
    risc_i2c_bus_contract_v1 bus = {
        {RISC_I2C_BUS_API_V1, sizeof(bus), 0, claim, transact, release},
        RISC_I2C_BUS_CONTRACT_TAG, RISC_I2C_BUS_CONTRACT_V1, RISC_I2C_BUS_SAFE_CONTRACT_FLAGS};
    const risc_platform_clock_api_v1 clock = {RISC_PLATFORM_CLOCK_API_V1, sizeof(clock), 0, tick, sleep_ms};
    const risc_provider_dependency_v1 deps[] = {{"i2c.bus", 1, &bus}, {"platform.clock", 1, &clock}};
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver);
    bus.base.struct_size = sizeof(risc_i2c_bus_api_v1);
    assert(!driver->start(deps, 2));
    bus.base.struct_size = sizeof(bus); bus.contract_tag ^= 1;
    assert(!driver->start(deps, 2)); bus.contract_tag ^= 1;
    bus.contract_version = 2; assert(!driver->start(deps, 2)); bus.contract_version = 1;
    for (unsigned bit = 0; bit < 3; ++bit) {
        bus.contract_flags = RISC_I2C_BUS_SAFE_CONTRACT_FLAGS & ~(1u << bit);
        assert(!driver->start(deps, 2));
    }
    assert(!pin_writes && !claim_calls && !release_calls && !now);
    bus.contract_flags = RISC_I2C_BUS_SAFE_CONTRACT_FLAGS;
    assert(driver->start(deps, 2));
    assert(pins[X4PRO_PIN_PERIPH_EN] && !pins[X4PRO_PIN_TOUCH_PWR]);
    const risc_touch_api_v1 *touch = driver->capability;
    assert(touch && touch->api_version == 1);
    const uint64_t sub = touch->subscribe(0);
    assert(sub && !touch->subscribe(0));
    point[0] = 37; point[2] = 140;
    status = 0x81;
    assert(touch->poll(0, 1));
    risc_touch_event_v1 event = {0};
    assert(touch->next(0, sub, &event) == 1 && event.kind == RISC_TOUCH_EVENT_DOWN);
    assert(event.x == 37 && event.y == 140);
    status = 0x80;
    assert(touch->poll(0, 1));
    assert(touch->next(0, sub, &event) == 1 && event.kind == RISC_TOUCH_EVENT_UP);
    assert(touch->next(0, sub, &event) == 0);
    status = 0x90;
    assert(touch->poll(0, 1));
    assert(touch->next(0, sub, &event) == 1 && event.kind == RISC_TOUCH_EVENT_BUTTON_DOWN);
    risc_touch_snapshot_v1 snap = {0};
    assert(touch->snapshot(0, &snap) && snap.width == 480 && snap.height == 800);
    status = 0x82;
    assert(!touch->poll(0, 1) && touch->next(0, sub, &event) == -1);
    assert(touch->snapshot(0, &snap) && snap.contact_count == 0);
    assert(touch->unsubscribe(0, sub) && driver->quiesce());
    assert(pins[X4PRO_PIN_TOUCH_PWR]);
    driver->stop();
    assert(driver->start(deps, 2));
    reject_release = true;
    const unsigned live_pins = pin_writes;
    assert(!driver->quiesce() && claimed == X4PRO_I2C_GT911);
    assert(!pins[X4PRO_PIN_TOUCH_PWR] && pin_writes == live_pins);
    const unsigned rejected_calls = release_calls;
    driver->stop();
    assert(release_calls == rejected_calls && !driver->start(deps, 2));
    reject_release = false;
    assert(driver->quiesce() && !claimed && pins[X4PRO_PIN_TOUCH_PWR]);
    const unsigned retired_calls = release_calls;
    driver->stop(); driver->stop(); assert(release_calls == retired_calls);
    assert(driver->start(deps, 2) && driver->quiesce()); driver->stop();

    reject_probe = reject_release = true;
    const unsigned claims_before = claim_calls;
    assert(!driver->start(deps, 2) && claimed == X4PRO_I2C_GT911);
    assert(claim_calls == claims_before + 1); // No alt-address reset/reprobe after failed release.
    const unsigned probe_pins = pin_writes;
    assert(!driver->quiesce() && pin_writes == probe_pins);
    assert(!driver->start(deps, 2));
    reject_release = false;
    assert(driver->quiesce() && !claimed); driver->stop();
    reject_probe = false;
    assert(driver->start(deps, 2) && driver->quiesce()); driver->stop();
    puts("X4 GT911 rail/touch/Home/gap and failed-release/start retention: PASS");
}
