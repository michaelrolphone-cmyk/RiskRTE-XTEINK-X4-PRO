/* X4 GT911 raw 480x800, source-0.1.5 single-contact/event semantics.
 * Hardware access is exclusively through scoped typed tables. The board rail
 * dependency remains live while this provider owns its switched touch rail. */
#include <RiscProviderV2.h>
#include <RiscI2cBusV1.h>
#include <RiscPlatformClockV1.h>
#include <RiscTouchV1.h>
#include <RiscTouchPowerV1.h>
#include <RiscTouchI2cV2.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include "../x4pro_board_power/PowerReadyV1.h"
#include <string.h>

#define STATUS 0x814eu
#define POINT 0x8150u
static const risc_i2c_bus_api_v1 *bus;
static const risc_platform_clock_api_v1 *clock_api;
static const garden_gpio_v1 *gpio;
static const risc_provider_sync_api_v1 *sync_api;
static uint64_t mutex, power_pin, reset_pin, irq_pin, claim;
static uint64_t token, token_serial = 1, sequence;
static risc_touch_snapshot_v1 state;
static risc_touch_event_v1 events[RISC_TOUCH_QUEUE_LENGTH];
static uint8_t head, queued;
static bool started, closing, gap, held, retained, neutral_gate;
/* Each fallible operation has its own persisted stage. No returned failure can
 * forget an owned pin/claim or repeat an already completed hold transition. */
enum power_stage {
    POWER_ACTIVE, PREP_RELEASE, PREP_OFF, PREP_HOLD, POWER_PREPARED,
    RESUME_UNHOLD, RESUME_ON, RESUME_POWER_WAIT, RESUME_IRQ_RELEASE,
    RESUME_IRQ_OUTPUT, RESUME_RESET_LOW, RESUME_RESET_WAIT, RESUME_RESET_HIGH,
    RESUME_RESET_RECOVERY, RESUME_IRQ_WRITE, RESUME_IRQ_WAIT,
    RESUME_IRQ_INPUT_RELEASE, RESUME_IRQ_INPUT, RESUME_READY_WAIT,
    RESUME_CLAIM, RESUME_ID, RESUME_ACK, RESUME_REJECT_RELEASE, RESUME_DONE
};
static enum power_stage power_stage;
static bool alternate;
typedef struct { uint64_t first, last; uint32_t limit, charged; } power_budget;
static char error_text[64];

static void fail(const char *s) {
    size_t i = 0;
    while (s[i] && i + 1u < sizeof(error_text)) { error_text[i] = s[i]; ++i; }
    error_text[i] = 0;
}
static bool enter(void) {
    return !retained && sync_api && mutex && sync_api->is_owner(sync_api->context) &&
        sync_api->try_lock(sync_api->context, mutex);
}
static bool leave(void) {
    if (!sync_api->unlock(sync_api->context, mutex)) {
        fail("gt911 sync retained"); retained = true; return false;
    }
    return true;
}
static void sleep_ms(uint32_t ms) { clock_api->sleep_ms(clock_api->context, ms); }
static uint64_t now_ms(void) { return clock_api->monotonic_ms(clock_api->context); }
static bool read_reg(uint16_t reg, uint8_t *out, size_t length) {
    const uint8_t addr[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return bus && claim && out && length &&
        bus->transact(bus->context, claim, addr, 2, out, length, 20);
}
static bool write_reg(uint16_t reg, uint8_t value) {
    const uint8_t data[3] = {(uint8_t)(reg >> 8), (uint8_t)reg, value};
    return bus && claim && bus->transact(bus->context, claim, data, 3, NULL, 0, 20);
}
static bool claim_pin(uint8_t pin, bool output, bool initial, uint64_t *out) {
    if (gpio->claim(gpio->context, pin, output, initial, false, out) && *out) return true;
    /* Native claim cleanup may be uncertain even without a returned token.
     * Fence reset/reprobe and keep all dependencies resident in that case. */
    fail("gt911 gpio claim retained"); retained = true; return false;
}
static bool release_pin(uint64_t *pin) {
    if (!*pin) return true;
    if (!gpio->release(gpio->context, *pin)) { fail("gt911 gpio release pending"); return false; }
    *pin = 0; return true;
}
static bool reset_select(bool high) {
    /* A GPIO claim has a fixed direction. Relinquish/reclaim the scoped IRQ
     * pad explicitly; a failed transition never proceeds to reset or probe. */
    if (!release_pin(&irq_pin) || !claim_pin(10, true, high, &irq_pin)) return false;
    if (reset_pin) {
        if (!gpio->write(gpio->context, reset_pin, false)) return false;
    } else if (!claim_pin(4, true, false, &reset_pin)) return false;
    sleep_ms(10);
    if (!gpio->write(gpio->context, reset_pin, true)) return false;
    sleep_ms(10);
    if (!gpio->write(gpio->context, irq_pin, high)) return false;
    sleep_ms(50);
    if (!release_pin(&irq_pin) || !claim_pin(10, false, false, &irq_pin)) return false;
    sleep_ms(50);
    return true;
}
static bool probe(uint8_t address) {
    uint64_t candidate = 0;
    const bool acquired = bus->claim_device(bus->context, address, &candidate);
    if (!acquired || !candidate) {
        /* The checked I2C contract guarantees rejected claims have no effect.
         * A nonzero token on false violates it: retain instead of reprobe. */
        if (candidate || acquired) { claim = candidate; retained = true; fail("gt911 i2c claim retained"); }
        return false;
    }
    claim = candidate;
    uint8_t id[4] = {0};
    if (read_reg(0x8140u, id, sizeof(id)) && id[0] == '9' && id[1] == '1' &&
        id[2] == '1' && write_reg(STATUS, 0)) return true;
    if (!bus->release_device(bus->context, claim)) { fail("gt911 probe release pending"); return false; }
    claim = 0; return false;
}
static void invalidate(void) {
    gap = true; head = queued = 0;
    if (sequence != UINT64_MAX) ++sequence;
    state.sequence = sequence; state.contact_count = 0; state.buttons = 0;
    state.timestamp_ms = now_ms();
}
static void emit(uint8_t kind, uint8_t id, uint16_t x, uint16_t y, uint64_t when) {
    if (sequence == UINT64_MAX) { invalidate(); return; }
    risc_touch_event_v1 event = {0};
    event.sequence = ++sequence; event.timestamp_ms = when;
    event.kind = kind; event.id = id; event.x = x; event.y = y;
    if (!token || gap) return;
    if (queued == RISC_TOUCH_QUEUE_LENGTH) { gap = true; head = queued = 0; return; }
    events[(head + queued) % RISC_TOUCH_QUEUE_LENGTH] = event; ++queued;
}
static uint64_t subscribe(void *context) {
    (void)context;
    if (!enter()) return 0;
    uint64_t result = 0;
    if (started && !closing && power_stage == POWER_ACTIVE && !token && token_serial != UINT64_MAX) {
        result = token = token_serial++; head = queued = 0; gap = false;
    }
    return leave() ? result : 0;
}
static bool unsubscribe(void *context, uint64_t sub) {
    (void)context;
    if (!enter()) return false;
    const bool okay = token && sub == token;
    if (okay) { token = 0; head = queued = 0; gap = false; }
    return leave() && okay;
}
static bool poll_locked(void) {
    uint8_t status = 0;
    if (!read_reg(STATUS, &status, 1)) { fail("gt911 status"); return false; }
    if (!(status & 0x80u)) return true;
    const uint8_t contacts = status & 0x0fu;
    /* Source X4 coordinates have no stable track IDs. Multi-contact and
     * out-of-bounds packets fence the stream instead of fabricating taps. */
    if (contacts > 1u) { invalidate(); (void)write_reg(STATUS, 0); return false; }
    risc_touch_contact_v1 contact = {0};
    if (contacts) {
        uint8_t raw[8] = {0};
        if (!read_reg(POINT, raw, sizeof(raw))) { fail("gt911 point"); return false; }
        contact.x = (uint16_t)(raw[0] | ((uint16_t)raw[1] << 8));
        contact.y = (uint16_t)(raw[2] | ((uint16_t)raw[3] << 8));
        contact.id = 1;
        if (contact.x >= 480u || contact.y >= 800u) {
            invalidate(); (void)write_reg(STATUS, 0); return false;
        }
    }
    const uint64_t when = now_ms();
    if (neutral_gate) {
        /* No stale DOWN, MOVE, UP or Home edge, including an ambiguous ACK.
         * Only an acknowledged READY all-neutral report rearms input. */
        state.timestamp_ms = when;
        if (!write_reg(STATUS, 0)) { fail("gt911 neutral acknowledge"); return false; }
        if (!contacts && !(status & 0x10u)) neutral_gate = false;
        return true;
    }
    if (state.contact_count && !contacts)
        emit(RISC_TOUCH_EVENT_UP, 1, state.contacts[0].x, state.contacts[0].y, when);
    else if (!state.contact_count && contacts)
        emit(RISC_TOUCH_EVENT_DOWN, 1, contact.x, contact.y, when);
    else if (state.contact_count && contacts &&
             (state.contacts[0].x != contact.x || state.contacts[0].y != contact.y))
        emit(RISC_TOUCH_EVENT_MOVE, 1, contact.x, contact.y, when);
    const bool home = (status & 0x10u) != 0;
    const bool was_home = (state.buttons & RISC_TOUCH_BUTTON_PRIMARY) != 0;
    if (home != was_home)
        emit(home ? RISC_TOUCH_EVENT_BUTTON_DOWN : RISC_TOUCH_EVENT_BUTTON_UP, 0, 0, 0, when);
    state.contact_count = contacts;
    state.contacts[0] = contacts ? contact : (risc_touch_contact_v1){0};
    state.buttons = home ? RISC_TOUCH_BUTTON_PRIMARY : 0;
    state.sequence = sequence; state.timestamp_ms = when;
    /* Commit before ACK: retrying an ambiguous ACK cannot duplicate an edge. */
    if (!write_reg(STATUS, 0)) { fail("gt911 acknowledge"); return false; }
    return true;
}
static bool poll(void *context, size_t max_reports) {
    (void)context;
    if (!max_reports || max_reports > 16u || !enter()) return false;
    const bool okay = started && !closing && power_stage == POWER_ACTIVE && poll_locked();
    return leave() && okay;
}
static int32_t next(void *context, uint64_t sub, risc_touch_event_v1 *out) {
    (void)context;
    if (!out || !enter()) return -1;
    int32_t result = -1;
    risc_touch_event_v1 event = {0};
    if (started && !closing && power_stage == POWER_ACTIVE && token && sub == token) {
        if (gap) { gap = false; head = queued = 0; }
        else if (!queued) result = 0;
        else {
            event = events[head]; head = (uint8_t)((head + 1u) % RISC_TOUCH_QUEUE_LENGTH);
            --queued; result = 1;
        }
    }
    if (!leave()) return -1;
    if (result == 1) *out = event;
    return result;
}
static bool snapshot(void *context, risc_touch_snapshot_v1 *out) {
    (void)context;
    if (!out || !enter()) return false;
    const bool okay = started && !closing && power_stage == POWER_ACTIVE;
    const risc_touch_snapshot_v1 copy = state;
    if (!leave() || !okay) return false;
    *out = copy; return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || retained || bus || clock_api || gpio || sync_api || mutex || claim ||
        token || power_pin || reset_pin || irq_pin || !deps || count != 6u) return false;
    const risc_hardware_device_v1 *hardware = NULL;
    const risc_i2c_bus_api_v1 *candidate_bus = NULL;
    const risc_platform_clock_api_v1 *clock = NULL;
    const garden_gpio_v1 *pins = NULL;
    const risc_provider_sync_api_v1 *sync = NULL;
    const x4_power_ready_api_v1 *power = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!deps[i].capability_id || deps[i].api_version != 1 || !deps[i].api) return false;
        const char *name = deps[i].capability_id;
        if (!strcmp(name, "hardware.device") && !hardware) hardware = deps[i].api;
        else if (!strcmp(name, "i2c.bus") && !candidate_bus) candidate_bus = deps[i].api;
        else if (!strcmp(name, "platform.clock") && !clock) clock = deps[i].api;
        else if (!strcmp(name, "platform.gpio") && !pins) pins = deps[i].api;
        else if (!strcmp(name, RISC_PROVIDER_SYNC_CAPABILITY) && !sync) sync = deps[i].api;
        else if (!strcmp(name, X4_POWER_READY_CAPABILITY) && !power) power = deps[i].api;
        else return false;
    }
    if (!hardware || hardware->api_version != 1 || hardware->struct_size < sizeof(*hardware) ||
        !hardware->instance_id || !hardware->compatible || strcmp(hardware->compatible, "goodix,gt911") ||
        !hardware->revision || strcmp(hardware->revision, "unspecified") || !hardware->config_type ||
        strcmp(hardware->config_type, "touch.i2c") || hardware->config_version != 2 ||
        hardware->config_size != sizeof(risc_hw_i2c_touch_v2) || !hardware->config ||
        !risc_i2c_bus_has_safe_contract(candidate_bus) ||
        !clock || clock->api_version != 1 || clock->struct_size < sizeof(*clock) || !clock->sleep_ms || !clock->monotonic_ms ||
        !pins || pins->api_version != 1 || pins->struct_size < GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE ||
        !pins->claim || !pins->write || !pins->release || !pins->deep_sleep_hold || !pins->retire_held_output ||
        !sync || sync->api_version != 1 || sync->struct_size < sizeof(*sync) || !sync->is_owner ||
        !sync->create || !sync->try_lock || !sync->unlock || !sync->destroy ||
        !power || power->api_version != 1 || power->struct_size < sizeof(*power) || !power->ready) return false;
    const risc_hw_i2c_touch_v2 *config = hardware->config;
    const risc_hw_i2c_touch_v1 *base = &config->base;
    const risc_hw_bus_v1 *wire = &base->bus;
    if (base->struct_size != sizeof(*config) || wire->struct_size != sizeof(*wire) ||
        wire->kind != RISC_HW_BUS_I2C || !wire->instance_id || wire->controller > 1 ||
        !wire->frequency_hz || wire->frequency_hz > 400000 || wire->mode ||
        wire->reserved[0] || wire->reserved[1] || wire->reserved[2] ||
        wire->sda != 39 || wire->scl != 38 || wire->sclk != -1 || wire->mosi != -1 || wire->miso != -1 ||
        base->width != 480 || base->height != 800 || base->address != 0x5d ||
        base->reset != 4 || base->reset_active_high || base->irq != 10 || base->irq_active_high || base->irq_pull_up ||
        base->reset_assert_ms != 10 || base->reset_recovery_ms != 10 ||
        config->power != 2 || config->power_active_high || config->irq_output != 1 || config->alternate_address != 0x14 ||
        config->reserved[0] || config->reserved[1] || config->reserved[2] ||
        !sync->is_owner(sync->context) || !power->ready(power->context)) return false;
    bus = candidate_bus; clock_api = clock; gpio = pins; sync_api = sync;
    closing = false; power_stage = POWER_ACTIVE; neutral_gate = false; error_text[0] = 0;
    if (!sync_api->create(sync_api->context, &mutex) || !mutex) {
        if (mutex) retained = true;
        else { bus = NULL; clock_api = NULL; gpio = NULL; sync_api = NULL; }
        return false;
    }
    if (!enter()) return false;
    bool okay = claim_pin(2, true, false, &power_pin);
    if (okay) { held = false; sleep_ms(50); okay = reset_select(false); }
    if (okay && !probe(0x5d)) {
        okay = !claim && !retained && reset_select(true) && probe(0x14);
        if (!okay && !error_text[0]) fail("gt911 probe");
    }
    if (okay) {
        memset(&state, 0, sizeof(state)); state.width = 480; state.height = 800;
        state.timestamp_ms = now_ms(); sequence = 0; head = queued = 0; gap = false; started = true;
    }
    return leave() && okay;
}
/* Budget accounting also charges the maximum issued waits/transfers, so a
 * stalled clock cannot authorize unbounded work. No dynamic retry loop exists.
 * Individual scoped GPIO/claim/release calls are nonblocking. Scheduler latency
 * and the one mandatory unlock may overrun; expiry never reports success. */
static uint32_t remaining(power_budget *budget) {
    const uint64_t current = now_ms();
    if (current < budget->last) { fail("gt911 power clock"); return 0; }
    budget->last = current;
    const uint64_t elapsed = current - budget->first;
    const uint64_t used = elapsed > budget->charged ? elapsed : budget->charged;
    return used < budget->limit ? budget->limit - (uint32_t)used : 0;
}
static bool power_write(uint64_t pin, bool level) {
    if (gpio->write(gpio->context, pin, level)) return true;
    fail("gt911 power write retained"); retained = true; return false;
}
static int32_t power_finish(int32_t result) {
    if (!leave() || retained) return RISC_TOUCH_POWER_RETAINED;
    return result;
}
static int32_t prepare_locked(power_budget *budget) {
    if (power_stage == POWER_ACTIVE) {
        power_stage = PREP_RELEASE;
        invalidate();
        memset(state.contacts, 0, sizeof(state.contacts));
        neutral_gate = true;
    } else if (power_stage > POWER_PREPARED) {
        /* Cancellation of a partially resumed provider closes the same exact
         * candidate claim before touching GPIO. No resource is abandoned. */
        power_stage = PREP_RELEASE;
    }
    for (;;) {
        if (!remaining(budget)) return RISC_TOUCH_POWER_TIMEOUT;
        switch (power_stage) {
        case PREP_RELEASE:
            if (claim) {
                if (!bus->release_device(bus->context, claim)) {
                    fail("gt911 prepare release pending"); return RISC_TOUCH_POWER_PLATFORM;
                }
                claim = 0;
            }
            power_stage = PREP_OFF;
            break;
        case PREP_OFF:
            if (!held && !power_write(power_pin, true)) return RISC_TOUCH_POWER_RETAINED;
            power_stage = PREP_HOLD;
            break;
        case PREP_HOLD:
            if (!held) {
                const int32_t result = gpio->deep_sleep_hold(gpio->context, power_pin, true);
                if (result == RISC_DEEP_SLEEP_RETAINED) {
                    fail("gt911 prepare hold retained"); retained = true;
                    return RISC_TOUCH_POWER_RETAINED;
                }
                if (result) { fail("gt911 prepare hold"); return RISC_TOUCH_POWER_PLATFORM; }
                held = true;
            }
            power_stage = POWER_PREPARED;
            break;
        case POWER_PREPARED: return RISC_TOUCH_POWER_OK;
        default: return RISC_TOUCH_POWER_UNAVAILABLE;
        }
    }
}
static int32_t power_prepare(void *context, uint32_t timeout_ms) {
    (void)context;
    if (retained) return RISC_TOUCH_POWER_RETAINED;
    if (timeout_ms > RISC_TOUCH_POWER_MAX_BUDGET_MS) return RISC_TOUCH_POWER_INVALID;
    if (!mutex) return RISC_TOUCH_POWER_UNAVAILABLE;
    if (!enter()) return RISC_TOUCH_POWER_BUSY;
    if (!started || closing) return power_finish(RISC_TOUCH_POWER_UNAVAILABLE);
    /* A rejected prepare cannot invalidate events or retire a subscriber. */
    if (token) return power_finish(RISC_TOUCH_POWER_BUSY);
    if (!timeout_ms) return power_finish(power_stage == POWER_PREPARED ?
        RISC_TOUCH_POWER_OK : RISC_TOUCH_POWER_BUSY);
    const uint64_t current = now_ms();
    power_budget budget = {current, current, timeout_ms, 0};
    return power_finish(prepare_locked(&budget));
}
static int32_t resume_locked(power_budget *budget) {
    if (power_stage < POWER_PREPARED) {
        const int32_t result = prepare_locked(budget);
        if (result) return result;
    }
    if (power_stage == POWER_PREPARED) { power_stage = RESUME_UNHOLD; alternate = false; }
    for (;;) {
        const uint32_t left = remaining(budget);
        if (!left) return RISC_TOUCH_POWER_TIMEOUT;
        uint32_t delay = 0;
        switch (power_stage) {
        case RESUME_UNHOLD:
            /* Any failed disable is uncertain, even if a backend incorrectly
             * supplies an ordinary refusal. Never power a potentially held pad. */
            if (gpio->deep_sleep_hold(gpio->context, power_pin, false)) {
                fail("gt911 unhold retained"); retained = true; return RISC_TOUCH_POWER_RETAINED;
            }
            held = false; power_stage = RESUME_ON;
            break;
        case RESUME_ON:
            if (!power_write(power_pin, false)) return RISC_TOUCH_POWER_RETAINED;
            power_stage = RESUME_POWER_WAIT;
            break;
        case RESUME_POWER_WAIT: delay = 50; break;
        case RESUME_IRQ_RELEASE:
            if (!release_pin(&irq_pin)) return RISC_TOUCH_POWER_PLATFORM;
            power_stage = RESUME_IRQ_OUTPUT;
            break;
        case RESUME_IRQ_OUTPUT:
            if (!claim_pin(10, true, alternate, &irq_pin)) return RISC_TOUCH_POWER_RETAINED;
            power_stage = RESUME_RESET_LOW;
            break;
        case RESUME_RESET_LOW:
            if (!power_write(reset_pin, false)) return RISC_TOUCH_POWER_RETAINED;
            power_stage = RESUME_RESET_WAIT;
            break;
        case RESUME_RESET_WAIT: delay = 10; break;
        case RESUME_RESET_HIGH:
            if (!power_write(reset_pin, true)) return RISC_TOUCH_POWER_RETAINED;
            power_stage = RESUME_RESET_RECOVERY;
            break;
        case RESUME_RESET_RECOVERY: delay = 10; break;
        case RESUME_IRQ_WRITE:
            if (!power_write(irq_pin, alternate)) return RISC_TOUCH_POWER_RETAINED;
            power_stage = RESUME_IRQ_WAIT;
            break;
        case RESUME_IRQ_WAIT: delay = 50; break;
        case RESUME_IRQ_INPUT_RELEASE:
            if (!release_pin(&irq_pin)) return RISC_TOUCH_POWER_PLATFORM;
            power_stage = RESUME_IRQ_INPUT;
            break;
        case RESUME_IRQ_INPUT:
            if (!claim_pin(10, false, false, &irq_pin)) return RISC_TOUCH_POWER_RETAINED;
            power_stage = RESUME_READY_WAIT;
            break;
        case RESUME_READY_WAIT: delay = 50; break;
        case RESUME_CLAIM: {
            uint64_t candidate = 0;
            const bool acquired = bus->claim_device(bus->context, alternate ? 0x14 : 0x5d, &candidate);
            if (!acquired || !candidate) {
                if (candidate || acquired) {
                    claim = candidate; retained = true; fail("gt911 resume claim retained");
                    return RISC_TOUCH_POWER_RETAINED;
                }
                power_stage = RESUME_REJECT_RELEASE;
            } else { claim = candidate; power_stage = RESUME_ID; }
            break;
        }
        case RESUME_ID: {
            uint8_t id[4] = {0};
            const uint8_t reg[2] = {0x81, 0x40};
            const uint32_t transfer_ms = left < 20u ? left : 20u;
            const bool okay = bus->transact(bus->context, claim, reg, 2, id, sizeof(id), transfer_ms);
            budget->charged += transfer_ms;
            power_stage = okay && id[0] == '9' && id[1] == '1' && id[2] == '1' ?
                RESUME_ACK : RESUME_REJECT_RELEASE;
            break;
        }
        case RESUME_ACK: {
            const uint8_t ack[3] = {0x81, 0x4e, 0};
            const uint32_t transfer_ms = left < 20u ? left : 20u;
            const bool okay = bus->transact(bus->context, claim, ack, 3, NULL, 0, transfer_ms);
            budget->charged += transfer_ms;
            power_stage = okay ? RESUME_DONE : RESUME_REJECT_RELEASE;
            break;
        }
        case RESUME_REJECT_RELEASE:
            if (claim) {
                if (!bus->release_device(bus->context, claim)) {
                    fail("gt911 resume release pending"); return RISC_TOUCH_POWER_PLATFORM;
                }
                claim = 0;
            }
            power_stage = RESUME_IRQ_RELEASE;
            if (alternate) {
                alternate = false; fail("gt911 resume probe"); return RISC_TOUCH_POWER_PLATFORM;
            }
            alternate = true;
            break;
        case RESUME_DONE:
            power_stage = POWER_ACTIVE;
            error_text[0] = 0;
            return RISC_TOUCH_POWER_OK;
        default: return RISC_TOUCH_POWER_UNAVAILABLE;
        }
        if (delay) {
            if (delay > left) return RISC_TOUCH_POWER_TIMEOUT;
            sleep_ms(delay); budget->charged += delay;
            power_stage = (enum power_stage)(power_stage + 1);
        }
    }
}
static int32_t power_resume(void *context, uint32_t timeout_ms) {
    (void)context;
    if (retained) return RISC_TOUCH_POWER_RETAINED;
    if (timeout_ms > RISC_TOUCH_POWER_MAX_BUDGET_MS) return RISC_TOUCH_POWER_INVALID;
    if (!mutex) return RISC_TOUCH_POWER_UNAVAILABLE;
    if (!enter()) return RISC_TOUCH_POWER_BUSY;
    if (!started || closing) return power_finish(RISC_TOUCH_POWER_UNAVAILABLE);
    if (power_stage == POWER_ACTIVE) return power_finish(RISC_TOUCH_POWER_OK);
    if (!timeout_ms) return power_finish(RISC_TOUCH_POWER_BUSY);
    const uint64_t current = now_ms();
    power_budget budget = {current, current, timeout_ms, 0};
    return power_finish(resume_locked(&budget));
}
static bool quiesce(void) {
    if (retained) return false;
    if (!mutex) return !claim && !token && !power_pin && !reset_pin && !irq_pin;
    if (!enter()) return false;
    /* Rejected unload must leave an existing subscription fully usable. */
    if (token) { (void)leave(); return false; }
    closing = true; started = false;
    if (claim) {
        if (!bus->release_device(bus->context, claim)) {
            fail("gt911 release pending"); (void)leave(); return false;
        }
        claim = 0;
    }
    bool okay = true;
    if (power_pin && !held) {
        okay = gpio->write(gpio->context, power_pin, true);
        if (okay) {
            const int32_t result = gpio->deep_sleep_hold(gpio->context, power_pin, true);
            if (result == RISC_DEEP_SLEEP_RETAINED) retained = true;
            okay = result == 0; held = okay;
        }
    }
    /* Power is off and held before releasing control pins. Retire only after
     * successful hold and checked releases; false preserves exact custody. */
    if (okay) okay = release_pin(&irq_pin);
    if (okay) okay = release_pin(&reset_pin);
    if (okay && power_pin) {
        okay = gpio->retire_held_output(gpio->context, power_pin);
        if (okay) { power_pin = 0; held = false; }
    }
    if (!leave() || !okay) return false;
    if (!sync_api->destroy(sync_api->context, mutex)) return false;
    mutex = 0; bus = NULL; clock_api = NULL; gpio = NULL; sync_api = NULL;
    return true;
}
static void stop(void) { /* No fallible work after accepted quiescence. */ }
static bool last_error(char *dst, size_t cap) {
    if (!dst || !cap || !error_text[0]) return false;
    size_t i = 0;
    while (error_text[i] && i + 1u < cap) { dst[i] = error_text[i]; ++i; }
    dst[i] = 0; return true;
}
static const risc_touch_power_api_v1 api = {
    {1, sizeof(api), NULL, subscribe, unsubscribe, poll, next, snapshot},
    RISC_TOUCH_POWER_TAG, 1, power_prepare, power_resume
};
static const risc_driver_diagnostics_v2 driver = {
    {2, sizeof(driver), "x4pro-gt911", "input.touch.raw", 1, &api, start, stop, quiesce}, last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi == 2 ? &driver.base : NULL; }
