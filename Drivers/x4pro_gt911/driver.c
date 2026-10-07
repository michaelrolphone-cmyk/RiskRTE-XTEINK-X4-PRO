/* X4 GT911: boot-owned rail/I2C touch provider, single-contact Reader input. */
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscTouchV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include "x4pro_proto.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STATUS 0x814eu
#define POINT 0x8150u
static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t claim, token, token_serial = 1, sequence;
static risc_touch_snapshot_v1 state;
static risc_touch_event_v1 events[RISC_TOUCH_QUEUE_LENGTH];
static uint8_t head, queued;
static bool started, gap, powered;
static char error_text[64];

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void fail(const char *s) {
    size_t i = 0;
    while (s[i] && i + 1u < sizeof(error_text)) { error_text[i] = s[i]; ++i; }
    error_text[i] = 0;
}
static void sleep_ms(uint32_t ms) { clock_api->sleep_ms(clock_api->context, ms); }
static uint64_t now_ms(void) { return clock_api->monotonic_ms(clock_api->context); }
static bool read_reg(uint16_t reg, uint8_t *out, size_t length) {
    uint8_t addr[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return bus && claim && out && length &&
        bus->transact(bus->context, claim, addr, 2, out, length, 20);
}
static bool write_reg(uint16_t reg, uint8_t value) {
    uint8_t data[3] = {(uint8_t)(reg >> 8), (uint8_t)reg, value};
    return bus && claim && bus->transact(bus->context, claim, data, 3, 0, 0, 20);
}
static void reset_select(bool high) {
    x4pro_pin_output(X4PRO_PIN_TOUCH_INT, high);
    x4pro_pin_output(X4PRO_PIN_TOUCH_RST, false);
    sleep_ms(10);
    x4pro_pin_level(X4PRO_PIN_TOUCH_RST, true);
    sleep_ms(10);
    x4pro_pin_level(X4PRO_PIN_TOUCH_INT, high);
    sleep_ms(50);
    x4pro_pin_input(X4PRO_PIN_TOUCH_INT, false);
    sleep_ms(50);
}
static bool probe(uint8_t address) {
    uint64_t candidate = 0;
    if (!bus->claim_device(bus->context, address, &candidate) || !candidate) return false;
    claim = candidate;
    uint8_t id[4] = {0};
    if (read_reg(0x8140u, id, sizeof(id)) && id[0] == '9' &&
        id[1] == '1' && id[2] == '1' && write_reg(STATUS, 0)) return true;
    if (!bus->release_device(bus->context, claim)) { fail("gt911 probe release pending"); return false; }
    claim = 0;
    return false;
}
static void invalidate(void) {
    gap = true;
    head = queued = 0;
    if (sequence != UINT64_MAX) ++sequence;
    state.sequence = sequence;
    state.contact_count = 0;
    state.buttons = 0;
    state.timestamp_ms = now_ms();
}
static void emit(uint8_t kind, uint8_t id, uint16_t x, uint16_t y, uint64_t when) {
    if (sequence == UINT64_MAX) { invalidate(); return; }
    risc_touch_event_v1 e = {0};
    e.sequence = ++sequence;
    e.timestamp_ms = when;
    e.kind = kind; e.id = id; e.x = x; e.y = y;
    if (!token || gap) return;
    if (queued == RISC_TOUCH_QUEUE_LENGTH) { gap = true; head = queued = 0; return; }
    events[(head + queued) % RISC_TOUCH_QUEUE_LENGTH] = e;
    ++queued;
}
static uint64_t subscribe(void *context) {
    (void)context;
    if (!started || token || token_serial == UINT64_MAX) return 0;
    token = token_serial++;
    head = queued = 0; gap = false;
    return token;
}
static bool unsubscribe(void *context, uint64_t sub) {
    (void)context;
    if (!token || sub != token) return false;
    token = 0; head = queued = 0; gap = false;
    return true;
}
static bool poll(void *context, size_t max_reports) {
    (void)context;
    if (!started || !max_reports || max_reports > 16u) return false;
    uint8_t status = 0;
    if (!read_reg(STATUS, &status, 1)) { fail("gt911 status"); return false; }
    if (!(status & 0x80u)) return true;
    const uint8_t contacts = status & 0x0fu;
    /* This X4 revision has no stable track IDs. Multi-contact reports fence
     * the stream instead of fabricating a tap from reordered fingers. */
    if (contacts > 1u) {
        invalidate(); (void)write_reg(STATUS, 0); return false;
    }
    risc_touch_contact_v1 next_contact = {0};
    if (contacts) {
        uint8_t raw[8] = {0};
        if (!read_reg(POINT, raw, sizeof(raw))) { fail("gt911 point"); return false; }
        if (!x4pro_gt911_map(raw, &next_contact.x, &next_contact.y, &next_contact.id)) {
            invalidate(); (void)write_reg(STATUS, 0); return false;
        }
    }
    const uint64_t when = now_ms();
    if (state.contact_count && !contacts)
        emit(RISC_TOUCH_EVENT_UP, 1, state.contacts[0].x, state.contacts[0].y, when);
    else if (!state.contact_count && contacts)
        emit(RISC_TOUCH_EVENT_DOWN, 1, next_contact.x, next_contact.y, when);
    else if (state.contact_count && contacts &&
             (state.contacts[0].x != next_contact.x || state.contacts[0].y != next_contact.y))
        emit(RISC_TOUCH_EVENT_MOVE, 1, next_contact.x, next_contact.y, when);
    const bool home = (status & 0x10u) != 0;
    const bool was_home = (state.buttons & RISC_TOUCH_BUTTON_PRIMARY) != 0;
    if (home != was_home)
        emit(home ? RISC_TOUCH_EVENT_BUTTON_DOWN : RISC_TOUCH_EVENT_BUTTON_UP, 0, 0, 0, when);
    state.contact_count = contacts;
    state.contacts[0] = contacts ? next_contact : (risc_touch_contact_v1){0};
    state.buttons = home ? RISC_TOUCH_BUTTON_PRIMARY : 0;
    state.sequence = sequence;
    state.timestamp_ms = when;
    /* Commit before ACK: an ambiguous ACK failure cannot duplicate an edge. */
    if (!write_reg(STATUS, 0)) { fail("gt911 acknowledge"); return false; }
    return true;
}
static int32_t next(void *context, uint64_t sub, risc_touch_event_v1 *out) {
    (void)context;
    if (!started || !token || sub != token || !out) return -1;
    if (gap) { gap = false; head = queued = 0; return -1; }
    if (!queued) return 0;
    *out = events[head];
    head = (uint8_t)((head + 1u) % RISC_TOUCH_QUEUE_LENGTH);
    --queued;
    return 1;
}
static bool snapshot(void *context, risc_touch_snapshot_v1 *out) {
    (void)context;
    if (!started || !out) return false;
    *out = state;
    return true;
}
static const risc_touch_api_v1 api = {
    RISC_TOUCH_API_V1, sizeof(api), 0, subscribe, unsubscribe, poll, next, snapshot
};
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || bus || clock_api || claim || token || powered || !deps || count != 2u) return false;
    const risc_i2c_bus_api_v1 *candidate_bus = 0;
    const risc_platform_clock_api_v1 *candidate_clock = 0;
    error_text[0] = 0;
    for (size_t i = 0; i < count; ++i) {
        if (equal(deps[i].capability_id, "i2c.bus") && deps[i].api_version == 1)
            candidate_bus = (const risc_i2c_bus_api_v1 *)deps[i].api;
        if (equal(deps[i].capability_id, "platform.clock") && deps[i].api_version == 1)
            candidate_clock = (const risc_platform_clock_api_v1 *)deps[i].api;
    }
    if (!risc_i2c_bus_has_safe_contract(candidate_bus) || !candidate_clock ||
        candidate_clock->api_version != RISC_PLATFORM_CLOCK_API_V1 ||
        candidate_clock->struct_size < sizeof(*candidate_clock) || !candidate_clock->sleep_ms ||
        !candidate_clock->monotonic_ms) { fail("gt911 dependencies"); return false; }
    bus = candidate_bus; clock_api = candidate_clock;
    /* The isolated board-alive bootstrap owns and retains GPIO1 before SD
     * package loading. This ELF owns only the independently switched GPIO2
     * touch rail; unloading touch must never release the board keep-alive. */
    x4pro_pin_hold(X4PRO_PIN_TOUCH_PWR, false);
    x4pro_pin_output(X4PRO_PIN_TOUCH_PWR, false);
    powered = true;
    sleep_ms(50);
    reset_select(false);
    if (!probe(X4PRO_I2C_GT911)) {
        if (claim) return false; // Retain rejected release; do not reset/reprobe.
        reset_select(true);
        if (!probe(0x14u)) {
            fail("gt911 probe");
            return false; // Graph invokes checked quiescence before unload.
        }
    }
    memset(&state, 0, sizeof(state));
    state.width = 480; state.height = 800; state.timestamp_ms = now_ms();
    sequence = 0; token = 0; head = queued = 0; gap = false; started = true;
    return true;
}
static bool quiesce(void) {
    if (token) return false; // A live subscription remains authoritative.
    started = false;
    if (claim) {
        if (!bus || !bus->release_device(bus->context, claim)) {
            fail("gt911 release pending"); return false;
        }
        claim = 0;
    }
    if (powered) {
        x4pro_pin_output(X4PRO_PIN_TOUCH_PWR, true);
        x4pro_pin_hold(X4PRO_PIN_TOUCH_PWR, true);
    }
    powered = false;
    bus = 0; clock_api = 0;
    return true;
}
static void stop(void) {
    /* ModuleV2 calls stop only after quiesce=true; no new fallible release. */
    if (claim || token || powered || bus || clock_api) return;
    started = false;
}
static bool last_error(char *dst, size_t cap) {
    if (!dst || !cap || !error_text[0]) return false;
    size_t i = 0;
    while (error_text[i] && i + 1u < cap) { dst[i] = error_text[i]; ++i; }
    dst[i] = 0; return true;
}
static const risc_driver_diagnostics_v2 driver = {
    {RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2), "x4pro-gt911",
     "input.touch.raw", 1, &api, start, stop, quiesce}, last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.base : 0;
}
