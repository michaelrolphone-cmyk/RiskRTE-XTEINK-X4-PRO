#include "RiscInputNavigationV1.h"
#include "RiscProviderV2.h"
#include "x4pro_pins.h"
#include <stdio.h>
uint32_t x4_fake_pressed_pins, x4_fake_input_pins;
static int failures;
static void expect(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL %s\n", message); ++failures; }
}
static risc_input_navigation_frame_v1 poll_one(const risc_input_navigation_api_v1 *api) {
    risc_input_navigation_frame_v1 frame = {0};
    expect(api->poll(api->context, &frame), "poll succeeds");
    return frame;
}
int main(void) {
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    expect(driver && driver->abi_version == RISC_PROVIDER_DRIVER_ABI_V2, "driver ABI");
    const risc_input_navigation_api_v1 *api = driver->capability;
    expect(driver->start(0, 0), "start");
    expect((x4_fake_input_pins & ((1u << X4PRO_PIN_BTN_LEFT) | (1u << X4PRO_PIN_BTN_RIGHT) |
                                (1u << X4PRO_PIN_BTN_POWER))) ==
           ((1u << X4PRO_PIN_BTN_LEFT) | (1u << X4PRO_PIN_BTN_RIGHT) |
            (1u << X4PRO_PIN_BTN_POWER)), "only button inputs configured");
    x4_fake_pressed_pins = 1u << X4PRO_PIN_BTN_LEFT;
    for (int i = 0; i < 3; ++i) expect(poll_one(api).pressed == 0, "debounce press");
    risc_input_navigation_frame_v1 f = poll_one(api);
    expect(f.pressed == RISC_NAV_LEFT && f.buttons == RISC_NAV_LEFT, "left edge");
    expect(api->reset(api->context), "reset while held");
    for (int i = 0; i < 4; ++i) {
        f = poll_one(api);
        expect(!f.buttons && !f.pressed && !f.released, "held button suppressed after boundary");
    }
    x4_fake_pressed_pins = 0;
    for (int i = 0; i < 4; ++i) {
        f = poll_one(api);
        expect(!f.buttons && !f.pressed && !f.released, "neutral rearm has no edge");
    }
    x4_fake_pressed_pins = 1u << X4PRO_PIN_BTN_RIGHT;
    for (int i = 0; i < 3; ++i) expect(poll_one(api).pressed == 0, "new press debounce");
    f = poll_one(api);
    expect(f.pressed == RISC_NAV_RIGHT && f.buttons == RISC_NAV_RIGHT, "right edge after rearm");
    driver->stop();
    x4_fake_pressed_pins = 1u << X4PRO_PIN_BTN_POWER;
    expect(driver->start(0, 0), "restart held");
    for (int i = 0; i < 4; ++i) {
        f = poll_one(api);
        expect(!f.buttons && !f.pressed, "boot-held power suppressed");
    }
    x4_fake_pressed_pins = 0;
    for (int i = 0; i < 4; ++i) (void)poll_one(api);
    x4_fake_pressed_pins = 1u << X4PRO_PIN_BTN_POWER;
    for (int i = 0; i < 4; ++i) f = poll_one(api);
    expect(f.pressed == RISC_NAV_CONFIRM, "power maps to confirm after neutral");
    x4_fake_pressed_pins = 0;
    for (int i = 0; i < 3; ++i) expect(poll_one(api).released == 0, "debounce confirm release");
    f = poll_one(api);
    expect(f.released == RISC_NAV_CONFIRM && f.buttons == 0, "confirm release edge");
    if (failures) return 1;
    puts("x4 buttons states: PASS");
    return 0;
}
