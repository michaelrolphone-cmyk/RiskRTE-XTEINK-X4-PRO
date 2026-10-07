/* X4 Pro side buttons. Owns GPIO 0/3/7. Publishes input.navigation. */
#include "RiscInputNavigationV1.h"
#include "RiscProviderV2.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include <stddef.h>

static uint32_t previous, pending;
static uint8_t stable_count;
static bool started, waiting_for_neutral;
static uint32_t sample(void) {
    uint32_t buttons = 0;
    if (!x4pro_pin_read(X4PRO_PIN_BTN_LEFT)) buttons |= RISC_NAV_LEFT;
    if (!x4pro_pin_read(X4PRO_PIN_BTN_RIGHT)) buttons |= RISC_NAV_RIGHT;
    if (!x4pro_pin_read(X4PRO_PIN_BTN_POWER)) buttons |= RISC_NAV_CONFIRM;
    return buttons;
}
static bool poll(void *context, risc_input_navigation_frame_v1 *out) {
    (void)context;
    if (!started || !out) return false;
    uint32_t raw = sample();
    if (waiting_for_neutral) {
        if (raw != 0) stable_count = 0;
        else if (stable_count < 3u) ++stable_count;
        if (stable_count >= 3u) {
            waiting_for_neutral = false;
            stable_count = 0;
        }
        out->buttons = 0;
        out->pressed = 0;
        out->released = 0;
        return true;
    }
    if (raw != pending) { pending = raw; stable_count = 0; }
    else if (stable_count < 3u) ++stable_count;
    uint32_t edges = 0, released = 0;
    if (stable_count >= 3u && pending != previous) {
        edges = pending & ~previous;
        released = previous & ~pending;
        previous = pending;
    }
    out->buttons = previous;
    out->pressed = edges;
    out->released = released;
    return true;
}
static bool foreground(void *context, const risc_input_foreground_v1 *claims, size_t count) {
    (void)context; (void)claims; (void)count;
    return true;
}
static bool reset(void *context) {
    (void)context;
    previous = pending = 0;
    stable_count = 0;
    waiting_for_neutral = sample() != 0;
    return true;
}
static const risc_input_navigation_traits_v1 api = {
    {RISC_INPUT_NAVIGATION_API_V1, sizeof(api), 0, poll, foreground, reset},
    RISC_INPUT_NAVIGATION_TRAITS_TAG, RISC_INPUT_NAVIGATION_TRAITS_VERSION,
    RISC_INPUT_NAVIGATION_PHYSICAL_PAGE_PAIR
};
static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    (void)dependencies; (void)count;
    x4pro_pin_input(X4PRO_PIN_BTN_LEFT, true);
    x4pro_pin_input(X4PRO_PIN_BTN_RIGHT, true);
    x4pro_pin_input(X4PRO_PIN_BTN_POWER, true);
    started = true;
    return reset(0);
}
static void stop(void) { started = false; }
static bool quiesce(void) { stop(); return true; }
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), "x4pro-buttons",
    "input.navigation", 1, &api.base, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver;
}
