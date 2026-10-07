/* X4 physical side buttons: active-low GPIO0 LEFT, GPIO7 RIGHT, GPIO3
 * CONFIRM in legacy profiles. An explicit long_press_us selects a completed
 * short power-key HOME pulse, like the product crown-button root action.
 * Preserve source 0.1.5 page-button debounce/neutral reset and page-pair traits.
 * All GPIO authority and synchronization arrive as scoped typed tables. */
#include <RiscInputNavigationV1.h>
#include <RiscProviderV2.h>
#include <RiscHardwareConfigV1.h>
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include <RiscPlatformClockV1.h>
#include <stddef.h>
#include <string.h>
static const uint8_t button_pins[3] = {0, 7, 3};
static const uint32_t button_bits[3] = {RISC_NAV_LEFT, RISC_NAV_RIGHT, RISC_NAV_CONFIRM};
static const garden_gpio_v1 *gpio;
static const risc_provider_sync_api_v1 *sync_api;
static const risc_platform_clock_api_v1 *clock_api;
static uint32_t crown_limit_ms;
static uint64_t crown_began, last_sample;
static bool crown_armed, crown_cancelled;
static uint64_t mutex, tokens[3];
static uint32_t previous, pending;
static uint8_t stable_count;
static bool started, waiting_for_neutral, retained, closing;
static bool enter(void) {
    return !retained && sync_api && mutex && sync_api->is_owner(sync_api->context) &&
        sync_api->try_lock(sync_api->context, mutex);
}
static bool leave(void) {
    if (!sync_api->unlock(sync_api->context, mutex)) { retained = true; return false; }
    return true;
}
static bool sample(uint32_t *out) {
    uint32_t buttons = 0;
    for (unsigned i = 0; i < 3; ++i) {
        bool high;
        if (!gpio->read(gpio->context, tokens[i], &high)) return false;
        if (!high) buttons |= button_bits[i];
    }
    *out = buttons; return true;
}
static bool poll(void *context, risc_input_navigation_frame_v1 *out) {
    (void)context;
    if (!out || !enter()) return false;
    uint32_t raw;
    if (!started || closing || !sample(&raw)) {
        if(crown_limit_ms){crown_armed=false;previous=pending=0;stable_count=0;waiting_for_neutral=true;}
        (void)leave(); return false;
    }
    uint64_t now = 0;
    if (crown_limit_ms) {
        now = clock_api->monotonic_ms(clock_api->context);
        if (now == UINT64_MAX || now < last_sample) {
            crown_armed=false;previous=pending=0;stable_count=0;waiting_for_neutral=true;
            (void)leave();return false;
        }
        last_sample=now;
        if (crown_armed && (raw & (RISC_NAV_LEFT|RISC_NAV_RIGHT))) crown_cancelled=true;
    }
    risc_input_navigation_frame_v1 frame = {0, 0, 0};
    if (waiting_for_neutral) {
        if (raw != 0) stable_count = 0;
        else if (stable_count < 3u) ++stable_count;
        if (stable_count >= 3u) { waiting_for_neutral = false; stable_count = 0; }
    } else {
        if (raw != pending) { pending = raw; stable_count = 0; }
        else if (stable_count < 3u) ++stable_count;
        if (stable_count >= 3u && pending != previous) {
            frame.pressed = pending & ~previous;
            frame.released = previous & ~pending;
            previous = pending;
        }
        frame.buttons = previous;
    }
    if (crown_limit_ms) {
        if (frame.pressed & RISC_NAV_CONFIRM) {
            crown_armed=true;crown_began=now;
            crown_cancelled=(raw & (RISC_NAV_LEFT|RISC_NAV_RIGHT))!=0;
        }
        if (frame.released & RISC_NAV_CONFIRM) {
            if (crown_armed && !crown_cancelled && now>=crown_began && now-crown_began<crown_limit_ms)
                frame.pressed|=RISC_NAV_HOME,frame.released|=RISC_NAV_HOME;
            crown_armed=false;
        }
        frame.buttons&=~RISC_NAV_CONFIRM;
        frame.pressed&=~RISC_NAV_CONFIRM;
        frame.released&=~RISC_NAV_CONFIRM;
    }
    if (!leave()) return false;
    *out = frame; return true;
}
static bool foreground(void *context, const risc_input_foreground_v1 *claims, size_t count) {
    (void)context; (void)claims; (void)count;
    /* This source never overlaps a foreground-owned transport. */
    if (!enter()) return false;
    const bool okay = started && !closing;
    return leave() && okay;
}
static bool reset(void *context) {
    (void)context;
    if (!enter()) return false;
    uint32_t raw;
    if (!started || closing || !sample(&raw)) { (void)leave(); return false; }
    previous = pending = 0; stable_count = 0; waiting_for_neutral = raw != 0;
    crown_armed=crown_cancelled=false;
    return leave();
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (gpio || sync_api || mutex || tokens[0] || tokens[1] || tokens[2] || started || retained || !deps || (count != 3 && count != 4)) return false;
    const risc_hardware_device_v1 *hardware = NULL;
    const garden_gpio_v1 *candidate = NULL;
    const risc_provider_sync_api_v1 *sync = NULL;
    const risc_platform_clock_api_v1 *clock = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!deps[i].capability_id || deps[i].api_version != 1 || !deps[i].api) return false;
        const char *name = deps[i].capability_id;
        if (!strcmp(name, "hardware.device") && !hardware) hardware = deps[i].api;
        else if (!strcmp(name, "platform.gpio") && !candidate) candidate = deps[i].api;
        else if (!strcmp(name, RISC_PROVIDER_SYNC_CAPABILITY) && !sync) sync = deps[i].api;
        else if (!strcmp(name, "platform.clock") && !clock) clock = deps[i].api;
        else return false;
    }
    if (!hardware || hardware->api_version != 1 || hardware->struct_size < sizeof(*hardware) || !hardware->instance_id ||
        !hardware->compatible || strcmp(hardware->compatible, "xteink,x4-pro-buttons") ||
        !hardware->revision || strcmp(hardware->revision, "unspecified") ||
        !hardware->config_type || strcmp(hardware->config_type, "gpio.bank") || hardware->config_version != 1 ||
        hardware->config_size != sizeof(risc_hw_gpio_bank_v1) || !hardware->config ||
        !candidate || candidate->api_version != 1 || candidate->struct_size < offsetof(garden_gpio_v1, release) + sizeof(candidate->release) ||
        !candidate->claim || !candidate->read || !candidate->release ||
        !sync || sync->api_version != 1 || sync->struct_size < sizeof(*sync) ||
        !sync->is_owner || !sync->create || !sync->try_lock || !sync->unlock || !sync->destroy) return false;
    const risc_hw_gpio_bank_v1 *config = hardware->config;
    if (config->struct_size != sizeof(*config) || config->count != 3 || config->active_high || config->pull_up != 1 ||
        config->pins[0] != 0 || config->pins[1] != 7 || config->pins[2] != 3 || config->reserved ||
        config->debounce_us || config->click_min_us || !sync->is_owner(sync->context)) return false;
    if (config->long_press_us && (config->long_press_us<100000u || config->long_press_us>10000000u || config->long_press_us%1000u ||
        !clock || clock->api_version!=1 || clock->struct_size<sizeof(*clock) || !clock->monotonic_ms)) return false;
    gpio = candidate; sync_api = sync; clock_api=clock; closing = false;
    crown_limit_ms=config->long_press_us/1000u;last_sample=0;crown_armed=crown_cancelled=false;
    if (!sync_api->create(sync_api->context, &mutex) || !mutex) { gpio = NULL; sync_api = NULL; return false; }
    if (!enter()) return false;
    bool okay = true;
    for (unsigned i = 0; i < 3; ++i) {
        if (!gpio->claim(gpio->context, button_pins[i], false, false, true, &tokens[i]) || !tokens[i]) {
            retained = true; okay = false; break;
        }
    }
    uint32_t raw = 0;
    if (okay) okay = sample(&raw);
    if (okay) { previous = pending = 0; stable_count = 0; waiting_for_neutral = raw != 0; }
    started = okay;
    return leave() && okay;
}
static bool quiesce(void) {
    if (retained) return false;
    if (!mutex) return !tokens[0] && !tokens[1] && !tokens[2];
    if (!enter()) return false;
    closing = true; started = false;
    bool okay = true;
    for (unsigned i = 0; i < 3; ++i) {
        if (!tokens[i]) continue;
        if (!gpio->release(gpio->context, tokens[i])) { okay = false; continue; }
        tokens[i] = 0;
    }
    if (!leave() || !okay) return false;
    if (!sync_api->destroy(sync_api->context, mutex)) return false;
    mutex = 0; gpio = NULL; sync_api = NULL; clock_api=NULL;crown_armed=false;
    return true;
}
static void stop(void) { /* No fallible cleanup after accepted quiescence. */ }
static const risc_input_navigation_traits_v1 api = {
    {1, sizeof(api), NULL, poll, foreground, reset}, RISC_INPUT_NAVIGATION_TRAITS_TAG,
    RISC_INPUT_NAVIGATION_TRAITS_VERSION, RISC_INPUT_NAVIGATION_PHYSICAL_PAGE_PAIR
};
static const risc_driver_v2 driver = {2, sizeof(driver), "x4pro-buttons", "input.navigation", 1, &api.base, start, stop, quiesce};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi == 2 ? &driver : NULL; }
