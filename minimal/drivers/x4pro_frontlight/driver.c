/* X4 dual frontlight: GPIO8/9, active-high, 25 kHz, 10-bit levels.
 * Ordinary scoped GPIO and owner-task synchronization; no MMIO/RTOS imports.
 * Only static LOW pads are held, including accepted provider quiescence. */
#include <RiscFrontlightToneV1.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include <stddef.h>
#include <string.h>

#define DUTY_FULL 1024u
#define PWM_HZ 25000u
static const uint8_t light_pins[2] = {8, 9};
static const garden_gpio_v1 *gpio;
static const risc_provider_sync_api_v1 *sync_api;
static uint64_t mutex, tokens[2];
static bool held[2], started, retained, closing;
static uint16_t duty;
/* Exact selected ratio. Neutral retains the legacy equal per-channel duty. */
static uint16_t tone_warm = 1, tone_maximum = 2;

static bool enter(void) {
    return !retained && sync_api && mutex && sync_api->is_owner(sync_api->context) &&
        sync_api->try_lock(sync_api->context, mutex);
}
static bool leave(void) {
    if (!sync_api->unlock(sync_api->context, mutex)) { retained = true; return false; }
    return true;
}
static bool claim(unsigned channel) {
    if (tokens[channel]) return true;
    /* CPU stages LOW before enabling output/unholding its retained pad. A
     * fresh same-scope token is required after every successful retirement. */
    if (!gpio->claim(gpio->context, light_pins[channel], true, false, false, &tokens[channel]) || !tokens[channel]) {
        retained = true;
        return false;
    }
    held[channel] = false;
    return true;
}
static bool hold(unsigned channel, bool enable) {
    if (held[channel] == enable) return true;
    const int32_t result = gpio->deep_sleep_hold(gpio->context, tokens[channel], enable);
    if (result == RISC_DEEP_SLEEP_RETAINED) retained = true;
    if (result) return false;
    held[channel] = enable;
    return true;
}
static bool pin_off(unsigned i) {
    if (!tokens[i]) return true; /* Already in CPU boot custody. */
    /* Successful write cancels PWM before enabling static sleep retention.
     * Never unhold a pad to turn it off, nor retire a failed safe write. */
    if (!held[i] && (!gpio->write(gpio->context, tokens[i], false) || !hold(i, true))) {
        return false;
    }
    /* Every established hold is LOW. Normal off/start must transfer it to
     * CPU custody too: a live held token fences Runtime app handoff. A
     * refusal retains the exact claim/hold for a later cleanup retry. */
    if (!gpio->retire_held_output(gpio->context, tokens[i])) return false;
    tokens[i] = 0;
    held[i] = false;
    return true;
}
static bool pins_off(void) {
    bool okay = true;
    for (unsigned i = 0; i < 2; ++i) {
        if (!pin_off(i)) okay = false;
    }
    if (okay) duty = 0;
    return okay;
}
static uint16_t ratio_to_duty(uint16_t requested, uint16_t maximum) {
    if (!requested) return 0;
    if (requested == maximum) return DUTY_FULL;
    uint32_t value = ((uint32_t)requested * DUTY_FULL + maximum / 2u) / maximum;
    if (!value) value = 1;
    if (value >= DUTY_FULL) value = DUTY_FULL - 1u;
    return (uint16_t)value;
}
static uint16_t channel_duty(unsigned channel, uint16_t base, uint16_t warm, uint16_t maximum) {
    const uint32_t ratio = 2u * (channel ? warm : (uint32_t)maximum - warm);
    if (ratio >= maximum) return base;
    /* Only reduce the opposite channel. This preserves logical brightness,
     * not measured luminance, and never raises a channel above legacy duty. */
    return (uint16_t)(((uint32_t)base * ratio + maximum / 2u) / maximum);
}
static bool apply_level(uint16_t base, uint16_t warm, uint16_t maximum) {
    bool okay = true;
    for (unsigned i = 0; i < 2 && okay; ++i) {
        const uint16_t next = channel_duty(i, base, warm, maximum);
        if (!next) { okay = pin_off(i); continue; }
        /* A held pad's underlying latch is already LOW. Release that hold
         * before the requested output, never before staging a safe LOW. */
        okay = claim(i) && hold(i, false);
        if (okay) okay = next == DUTY_FULL ? gpio->write(gpio->context, tokens[i], true) :
            gpio->pwm(gpio->context, tokens[i], PWM_HZ, next, DUTY_FULL);
    }
    return okay;
}
static void fail_dark(void) {
    /* A failed output may have changed native state. Attempt both LOWs, then
     * fence normal calls; only cleanup retries may follow an I/O failure. */
    started = false;
    (void)pins_off();
}
static bool set_level(void *context, uint16_t requested, uint16_t maximum) {
    (void)context;
    if (!maximum || requested > maximum || !enter()) return false;
    if (!started || closing) { (void)leave(); return false; }
    const uint16_t next = ratio_to_duty(requested, maximum);
    bool okay = true;
    if (!next) okay = pins_off();
    else okay = apply_level(next, tone_warm, tone_maximum);
    if (okay) duty = next;
    else fail_dark();
    return leave() && okay;
}
static bool get_level(void *context, uint16_t *out, uint16_t *maximum) {
    (void)context;
    if (!out || !maximum || !enter()) return false;
    const bool valid = started && !closing;
    const uint16_t current = duty;
    if (!leave() || !valid) return false;
    *out = current; *maximum = DUTY_FULL;
    return true;
}
static bool set_tone(void *context, uint16_t warm, uint16_t maximum) {
    (void)context;
    if (!maximum || warm > maximum || !enter()) return false;
    if (!started || closing) { (void)leave(); return false; }
    /* OFF is already LOW and retired. Selection while off performs no GPIO
     * operations, and the selected ratio is used by the next nonzero level. */
    const bool okay = !duty || apply_level(duty, warm, maximum);
    if (okay) { tone_warm = warm; tone_maximum = maximum; }
    else fail_dark();
    return leave() && okay;
}
static bool get_tone(void *context, uint16_t *warm, uint16_t *maximum) {
    (void)context;
    if (!warm || !maximum || warm == maximum || !enter()) return false;
    const bool valid = started && !closing;
    const uint16_t current = tone_warm, scale = tone_maximum;
    if (!leave() || !valid) return false;
    *warm = current; *maximum = scale;
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (gpio || sync_api || mutex || tokens[0] || tokens[1] || started || retained || !deps || count != 3) return false;
    const risc_hardware_device_v1 *hardware = NULL;
    const garden_gpio_v1 *candidate = NULL;
    const risc_provider_sync_api_v1 *sync = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!deps[i].capability_id || deps[i].api_version != 1 || !deps[i].api) return false;
        const char *name = deps[i].capability_id;
        if (!strcmp(name, "hardware.device") && !hardware) hardware = deps[i].api;
        else if (!strcmp(name, "platform.gpio") && !candidate) candidate = deps[i].api;
        else if (!strcmp(name, RISC_PROVIDER_SYNC_CAPABILITY) && !sync) sync = deps[i].api;
        else return false;
    }
    if (!hardware || hardware->api_version != 1 || hardware->struct_size < sizeof(*hardware) || !hardware->instance_id ||
        !hardware->compatible || strcmp(hardware->compatible, "xteink,x4-pro-frontlight") ||
        !hardware->revision || strcmp(hardware->revision, "unspecified") ||
        !hardware->config_type || strcmp(hardware->config_type, "gpio.bank") || hardware->config_version != 1 ||
        hardware->config_size != sizeof(risc_hw_gpio_bank_v1) || !hardware->config ||
        !candidate || candidate->api_version != 1 || candidate->struct_size < GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE ||
        !candidate->claim || !candidate->write || !candidate->pwm || !candidate->deep_sleep_hold || !candidate->retire_held_output ||
        !sync || sync->api_version != 1 || sync->struct_size < sizeof(*sync) ||
        !sync->is_owner || !sync->create || !sync->try_lock || !sync->unlock || !sync->destroy) return false;
    const risc_hw_gpio_bank_v1 *config = hardware->config;
    if (config->struct_size != sizeof(*config) || config->count != 2 || config->active_high != 1 || config->pull_up ||
        config->pins[0] != 8 || config->pins[1] != 9 || config->reserved || config->debounce_us ||
        config->long_press_us || config->click_min_us || !sync->is_owner(sync->context)) return false;
    gpio = candidate; sync_api = sync; closing = false;
    if (!sync_api->create(sync_api->context, &mutex) || !mutex) { gpio = NULL; sync_api = NULL; return false; }
    if (!enter()) return false;
    bool okay = true;
    for (unsigned i = 0; i < 2; ++i) {
        if (!claim(i)) { okay = false; break; }
    }
    if (!pins_off()) okay = false;
    started = okay;
    return leave() && okay;
}
static bool quiesce(void) {
    if (retained) return false;
    if (!mutex) return !tokens[0] && !tokens[1];
    if (!enter()) return false;
    closing = true; started = false;
    bool okay = pins_off();
    if (!leave() || !okay) return false;
    if (!sync_api->destroy(sync_api->context, mutex)) return false;
    mutex = 0; gpio = NULL; sync_api = NULL; duty = 0; tone_warm = 1; tone_maximum = 2;
    return true;
}
static void stop(void) { /* All fallible cleanup precedes accepted quiescence. */ }
static const risc_frontlight_api_v1_tone api = {
    {1, sizeof(api), NULL, set_level, get_level},
    RISC_FRONTLIGHT_TONE_TAG, RISC_FRONTLIGHT_TONE_VERSION, set_tone, get_tone
};
static const risc_driver_v2 driver = {2, sizeof(driver), "x4pro-frontlight", "display.frontlight", 1, &api, start, stop, quiesce};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi == 2 ? &driver : NULL; }
