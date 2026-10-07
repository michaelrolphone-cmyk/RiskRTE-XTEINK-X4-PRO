/* X4 Pro 800x480 panel provider for gated SSD1677 and UC8279 variants.
 * SSD BUSY is active-high; UC BUSY is active-low. No external display PMIC.
 * GPIO1 is the recovered peripheral-enable name and is not driven here.
 * Touch power GPIO2 and SD power GPIO5 stay untouched. */
#include "RiscDisplayOutputV1.h"
#include "RiscPlatformClockV1.h"
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include <RiscFrontlightV1.h>
#include "../x4pro_board_power/PowerReadyV1.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define X4PRO_PANEL_WIDTH 800u
#define X4PRO_PANEL_HEIGHT 480u
/* Role indices are local; physical pads come only from the typed device. */
enum { X4PRO_PIN_EPD_BUSY, X4PRO_PIN_EPD_CS, X4PRO_PIN_EPD_SCLK,
       X4PRO_PIN_EPD_MOSI, X4PRO_PIN_EPD_DC, X4PRO_PIN_EPD_RST, PANEL_PINS };
static const garden_gpio_v1 *gpio;
static const risc_frontlight_api_v1 *frontlight;
static const risc_provider_sync_api_v1 *sync_api;
static const risc_hw_spi_display_v1 *configuration;
static uint64_t pin_tokens[PANEL_PINS], mutex;
static uint8_t physical_pins[PANEL_PINS];
static bool pin_output[PANEL_PINS], pin_pullup[PANEL_PINS];
static bool io_failed, retained, reset_held;
static uint64_t last_sample_ms, operation_deadline;
static void set_reason(const char *text);
static bool enter(void) {
    return !retained && sync_api && mutex && sync_api->is_owner(sync_api->context) &&
        sync_api->try_lock(sync_api->context, mutex);
}
static bool leave(void) {
    if (sync_api->unlock(sync_api->context, mutex)) return true;
    retained = true; set_reason("sync unlock retained"); return false;
}
static void pin_failed(void) { io_failed = true; retained = true; set_reason("gpio operation retained"); }
static void panel_pin_level(unsigned pin, bool level) {
    if (io_failed) return;
    if (!gpio || pin >= PANEL_PINS || !pin_tokens[pin] || !pin_output[pin] ||
        !gpio->write(gpio->context, pin_tokens[pin], level)) pin_failed();
}
static void panel_pin_mode(unsigned pin, bool output, bool level, bool pullup) {
    if (io_failed) return;
    if (pin >= PANEL_PINS) { pin_failed(); return; }
    if (pin_tokens[pin] && pin_output[pin] == output && pin_pullup[pin] == pullup) {
        if (output) panel_pin_level(pin, level);
        return;
    }
    if (pin_tokens[pin]) {
        if (!gpio->release(gpio->context, pin_tokens[pin])) { pin_failed(); return; }
        pin_tokens[pin] = 0;
    }
    uint64_t token = 0;
    if (!gpio->claim(gpio->context, physical_pins[pin], output, level, pullup, &token) || !token) {
        pin_tokens[pin] = token; pin_failed(); return;
    }
    pin_tokens[pin] = token; pin_output[pin] = output; pin_pullup[pin] = pullup;
}
static void panel_pin_output(unsigned pin, bool level) { panel_pin_mode(pin, true, level, false); }
static void panel_pin_input(unsigned pin, bool pullup) { panel_pin_mode(pin, false, false, pullup); }
static bool panel_pin_read(unsigned pin) {
    bool level = false;
    if (!io_failed && (!gpio || pin >= PANEL_PINS || !pin_tokens[pin] || !gpio->read(gpio->context, pin_tokens[pin], &level))) pin_failed();
    return level;
}
static void panel_epd_reset_unhold(void) {
    /* New ordinary claims stage the configured latch before releasing a hold
     * from a previous boot. This instance never clears another owner's hold. */
}

#define FRAME_BYTES ((X4PRO_PANEL_WIDTH / 8u) * X4PRO_PANEL_HEIGHT)
static const risc_platform_clock_api_v1 *clock_api;
static uint8_t frame[FRAME_BYTES], previous_frame[FRAME_BYTES];
static bool previous_seeded, partial_update;
static risc_display_rect_v1 update_area;
static uint64_t transfer_yielded_ms;
static unsigned transfer_work;
static uint8_t present_state;
static bool transfer_started;
static bool started, held, pins_ready;
static uint8_t shutdown_stage;
static int controller;
enum { PRESENT_NONE = 0, PRESENT_QUEUED = 1, PRESENT_ACTIVE = 2, PRESENT_COMPLETE = 3, PRESENT_FAILED = 5 };
static uint64_t frame_serial, token_serial, pending_token;
static char last_error_text[64];

static void fail(const char *text) {
    size_t i = 0;
    while (text[i] && i + 1u < sizeof(last_error_text)) { last_error_text[i] = text[i]; ++i; }
    last_error_text[i] = 0;
}
static void sleep_ms(uint32_t ms) {
    if (clock_api && clock_api->sleep_ms) clock_api->sleep_ms(clock_api->context, ms);
}
static void spi_byte(uint8_t value) {
    for (int bit = 7; bit >= 0; --bit) {
        panel_pin_level(X4PRO_PIN_EPD_MOSI, (value >> bit) & 1);
        panel_pin_level(X4PRO_PIN_EPD_SCLK, true);
        panel_pin_level(X4PRO_PIN_EPD_SCLK, false);
    }
}
static void command(uint8_t cmd) {
    panel_pin_level(X4PRO_PIN_EPD_DC, false);
    panel_pin_level(X4PRO_PIN_EPD_CS, false);
    spi_byte(cmd);
    panel_pin_level(X4PRO_PIN_EPD_CS, true);
}
static void data1(uint8_t value) {
    panel_pin_level(X4PRO_PIN_EPD_DC, true);
    panel_pin_level(X4PRO_PIN_EPD_CS, false);
    spi_byte(value);
    panel_pin_level(X4PRO_PIN_EPD_CS, true);
}
static uint64_t now_ms(void) {
    if (!clock_api || !clock_api->monotonic_ms) return UINT64_MAX;
    return clock_api->monotonic_ms(clock_api->context);
}
static uint64_t transfer_start_ms, transfer_end_ms, refresh_ms, busy_assert_ms, busy_done_ms, wait_start_ms;
static uint32_t bytes_sent, wait_budget_ms;
static uint8_t busy_before;
static const char *reason = "none";
static void set_reason(const char *text) { reason = text; fail(text); }
static bool sample_now(uint64_t *out) {
    if (io_failed) return false;
    uint64_t now = now_ms();
    if (now == UINT64_MAX) { set_reason("clock failure"); return false; }
    if (now < last_sample_ms) { set_reason("clock nonmonotonic"); return false; }
    last_sample_ms = now;
    *out = now;
    return true;
}
static bool wait_idle(uint64_t deadline_ms) {
    bool saw_busy = false;
    while (!saw_busy) {
        uint64_t now = 0;
        if (!sample_now(&now) || now >= deadline_ms) {
            if (reason[0] == 'n') set_reason("busy never asserted");
            return false;
        }
        saw_busy = panel_pin_read(X4PRO_PIN_EPD_BUSY);
        if (saw_busy && !busy_before && !busy_assert_ms) busy_assert_ms = now;
        if (!saw_busy) sleep_ms(10);
    }
    while (panel_pin_read(X4PRO_PIN_EPD_BUSY)) {
        uint64_t now = 0;
        if (!sample_now(&now) || now >= deadline_ms) {
            if (reason[0] == 'n') set_reason("busy completion timeout");
            return false;
        }
        sleep_ms(10);
    }
    return sample_now(&busy_done_ms) && busy_done_ms < deadline_ms;
}
static char probe_text[96] = "probe=not-run";
static bool append(char *destination, size_t capacity, size_t *used, const char *text);
static bool append_u(char *destination, size_t capacity, size_t *used, uint64_t value);
static void prepare_pins(void);
static void clock_delay(void) {
    for (volatile int i = 0; i < 32; ++i) (void)panel_pin_read(X4PRO_PIN_EPD_BUSY);
}
static uint8_t read_byte(void) {
    uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit) {
        clock_delay();
        value = (uint8_t)((value << 1) | (panel_pin_read(X4PRO_PIN_EPD_MOSI) ? 1u : 0u));
        panel_pin_level(X4PRO_PIN_EPD_SCLK, true);
        clock_delay();
        panel_pin_level(X4PRO_PIN_EPD_SCLK, false);
    }
    return value;
}
static void read_cmd(uint8_t cmd, uint8_t *out, size_t len) {
    panel_pin_output(X4PRO_PIN_EPD_MOSI, false);
    panel_pin_level(X4PRO_PIN_EPD_DC, false);
    panel_pin_level(X4PRO_PIN_EPD_CS, false);
    clock_delay();
    spi_byte(cmd);
    panel_pin_level(X4PRO_PIN_EPD_DC, true);
    panel_pin_input(X4PRO_PIN_EPD_MOSI, true);
    clock_delay();
    for (size_t i = 0; i < len; ++i) out[i] = read_byte();
    panel_pin_level(X4PRO_PIN_EPD_CS, true);
    panel_pin_output(X4PRO_PIN_EPD_MOSI, false);
}
static void append_hex(char *destination, size_t capacity, size_t *used, uint8_t value) {
    const char *digits = "0123456789abcdef";
    if (*used + 2u < capacity) {
        destination[(*used)++] = digits[value >> 4];
        destination[(*used)++] = digits[value & 0x0f];
        destination[*used] = 0;
    }
}
enum { PROBE_AMBIGUOUS = 0, PROBE_SSD = 1, PROBE_UC8279 = 2, PROBE_DISABLED = 3 };
static int probe_controller(void) {
    uint8_t flg = 0;
    uint8_t ver[5] = {0};
    prepare_pins();
    const uint8_t busy_before_reset = panel_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    panel_pin_output(X4PRO_PIN_EPD_RST, true);
    panel_epd_reset_unhold();
    panel_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(1);
    panel_pin_output(X4PRO_PIN_EPD_RST, true);
    sleep_ms(30);
    const uint8_t busy = panel_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    int verdict = PROBE_AMBIGUOUS;
    {
        read_cmd(0x71, &flg, 1);
        read_cmd(0x70, ver, 5);
        bool floating = true;
        for (int i = 0; i < 5; ++i) if (ver[i] != ver[0]) floating = false;
        const bool uc = flg != 0 && flg != 0xFF && (flg & 1u) == 1u && !floating;
        const bool ssd = floating && (flg == 0x00 || flg == 0xFF);
        if (uc && ver[2] == 0x68) {
            uint8_t confirm_flg = 0, confirm_ver[5] = {0};
            sleep_ms(50);
            read_cmd(0x71, &confirm_flg, 1);
            read_cmd(0x70, confirm_ver, 5);
            bool matches = confirm_flg == flg;
            for (size_t i = 0; i < sizeof(ver); ++i) matches = matches && confirm_ver[i] == ver[i];
            verdict = matches && busy ? PROBE_UC8279 : PROBE_AMBIGUOUS;
        } else verdict = ssd ? PROBE_SSD : PROBE_AMBIGUOUS;
    }
    size_t used = 0;
    append(probe_text, sizeof(probe_text), &used, "probe flg=");
    append_hex(probe_text, sizeof(probe_text), &used, flg);
    append(probe_text, sizeof(probe_text), &used, " ver=");
    for (int i = 0; i < 5; ++i) append_hex(probe_text, sizeof(probe_text), &used, ver[i]);
    append(probe_text, sizeof(probe_text), &used, " busy0=");
    append_u(probe_text, sizeof(probe_text), &used, busy_before_reset);
    append(probe_text, sizeof(probe_text), &used, " busy=");
    append_u(probe_text, sizeof(probe_text), &used, busy);
    append(probe_text, sizeof(probe_text), &used, verdict == PROBE_SSD ? " verdict=ssd-assumed" : verdict == PROBE_UC8279 ? " verdict=uc8279-confirmed" : verdict == PROBE_DISABLED ? " verdict=probe-disabled" : " verdict=ambiguous");
    return io_failed ? PROBE_AMBIGUOUS : verdict;
}
static void prepare_pins(void) {
    if (pins_ready) return;
    panel_pin_input(X4PRO_PIN_EPD_BUSY, false);
    panel_pin_output(X4PRO_PIN_EPD_CS, true);
    panel_pin_output(X4PRO_PIN_EPD_SCLK, false);
    panel_pin_output(X4PRO_PIN_EPD_MOSI, false);
    panel_pin_output(X4PRO_PIN_EPD_DC, false);
    panel_pin_output(X4PRO_PIN_EPD_RST, true);
    panel_epd_reset_unhold();
    pins_ready = true;
}
static const uint8_t x_window[] = {0x00, 0x00, 0x1F, 0x03};
static const uint8_t y_window[] = {0xDF, 0x01, 0x00, 0x00};
static void set_cursor(void) {
    command(0x4E); data1(0x00); data1(0x00);
    command(0x4F); data1(0xDF); data1(0x01);
}
static bool ready_for(const char *failure) {
    uint64_t now = 0;
    if (!sample_now(&now) || now > UINT64_MAX - 500u) return false;
    uint64_t deadline = now + 500u;
    if (operation_deadline && operation_deadline < deadline) deadline = operation_deadline;
    while (panel_pin_read(X4PRO_PIN_EPD_BUSY)) {
        if (!sample_now(&now) || now >= deadline) { set_reason(failure); return false; }
        sleep_ms(10);
    }
    return sample_now(&now) && now < deadline;
}
static bool uc_ready_for(const char *failure, uint64_t deadline_ms) {
    while (!panel_pin_read(X4PRO_PIN_EPD_BUSY)) {
        uint64_t now = 0;
        if (!sample_now(&now) || now >= deadline_ms) { set_reason(failure); return false; }
        sleep_ms(10);
    }
    uint64_t now = 0;
    return sample_now(&now) && now < deadline_ms;
}
static bool uc_init_panel(void) {
    /* FreeInk UC8279 X4 Pro: use panel-programmed voltage and OTP waveform. */
    panel_epd_reset_unhold();
    panel_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(50);
    panel_pin_output(X4PRO_PIN_EPD_RST, true);
    sleep_ms(50);
    uint64_t now = 0;
    if (!sample_now(&now) || now > UINT64_MAX - 500u || !uc_ready_for("uc reset busy timeout", now + 500u)) return false;
    command(0x00); data1(0x37); data1(0x4D);
    command(0x61); data1(0x03); data1(0x20); data1(0x02); data1(0x58);
    command(0x65); data1(0); data1(0); data1(0); data1(0);
    command(0x03); data1(0x20);
    command(0x30); data1(0x0E);
    command(0xE1); data1(0x02);
    return !io_failed;
}
static bool init_panel(void) {
    static const uint8_t booster[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
    static const uint8_t gate[] = {0xDF, 0x01, 0x02};
    prepare_pins();
    panel_pin_output(X4PRO_PIN_EPD_RST, true);
    panel_epd_reset_unhold();
    panel_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(10);
    panel_pin_output(X4PRO_PIN_EPD_RST, true);
    sleep_ms(10);
    command(0x12);
    sleep_ms(10);
    if (!ready_for("reset busy timeout")) return false;
    command(0x18);
    data1(0x80);
    command(0x0C);
    for (size_t i = 0; i < sizeof(booster); ++i) data1(booster[i]);
    command(0x01);
    for (size_t i = 0; i < sizeof(gate); ++i) data1(gate[i]);
    command(0x3C);
    data1(0x80);
    command(0x11);
    data1(0x01);
    command(0x44);
    for (size_t i = 0; i < sizeof(x_window); ++i) data1(x_window[i]);
    command(0x45);
    for (size_t i = 0; i < sizeof(y_window); ++i) data1(y_window[i]);
    if (!ready_for("pre-cursor readiness")) return false;
    set_cursor();
    command(0x46); data1(0xF7);
    if (!ready_for("first-ram busy timeout")) return false;
    command(0x47); data1(0xF7);
    if (!ready_for("second-ram busy timeout")) return false;
    return !io_failed;
}
static bool transfer_checkpoint(uint64_t deadline_ms, unsigned work) {
    uint64_t now = 0;
    if (!sample_now(&now) || now >= deadline_ms) {
        panel_pin_level(X4PRO_PIN_EPD_CS, true);
        transfer_end_ms = now;
        set_reason("transfer deadline");
        return false;
    }
    transfer_work += work;
    if (transfer_work >= 1024u || now - transfer_yielded_ms >= 2u) {
        sleep_ms(1u);
        if (!sample_now(&now) || now >= deadline_ms) {
            panel_pin_level(X4PRO_PIN_EPD_CS, true);
            set_reason("transfer deadline"); return false;
        }
        transfer_yielded_ms = now; transfer_work = 0;
    }
    return true;
}
static void set_update_window(void) {
    const uint16_t x = partial_update ? (uint16_t)update_area.x : 0u;
    const uint16_t y = partial_update ? (uint16_t)update_area.y : 0u;
    const uint16_t w = partial_update ? (uint16_t)update_area.width : X4PRO_PANEL_WIDTH;
    const uint16_t h = partial_update ? (uint16_t)update_area.height : X4PRO_PANEL_HEIGHT;
    const uint16_t right = x + w - 1u;
    const uint16_t first = X4PRO_PANEL_HEIGHT - 1u - y, last = first + 1u - h;
    command(0x44); data1(x & 255u); data1(x >> 8); data1(right & 255u); data1(right >> 8);
    command(0x45); data1(first & 255u); data1(first >> 8); data1(last & 255u); data1(last >> 8);
    command(0x4E); data1(x & 255u); data1(x >> 8);
    command(0x4F); data1(first & 255u); data1(first >> 8);
}
static bool transfer_plane(uint8_t ram_command, uint64_t deadline_ms) {
    command(ram_command);
    panel_pin_level(X4PRO_PIN_EPD_DC, true);
    panel_pin_level(X4PRO_PIN_EPD_CS, false);
    const uint8_t *pixels = partial_update && ram_command == 0x26 ? previous_frame : frame;
    const unsigned x = partial_update ? (unsigned)update_area.x / 8u : 0u;
    const unsigned y = partial_update ? (unsigned)update_area.y : 0u;
    const unsigned w = partial_update ? update_area.width / 8u : X4PRO_PANEL_WIDTH / 8u;
    const unsigned h = partial_update ? update_area.height : X4PRO_PANEL_HEIGHT;
    for (unsigned row = 0; row < h; ++row) {
        for (unsigned col = 0; col < w; ++col) {
            const size_t i = (size_t)(row + y) * (X4PRO_PANEL_WIDTH / 8u) + x + col;
            spi_byte((uint8_t)~pixels[i]);
            ++bytes_sent;
            if ((col & 63u) == 63u && !transfer_checkpoint(deadline_ms, 64u)) return false;
        }
        if (!transfer_checkpoint(deadline_ms, w & 63u)) return false;
    }
    panel_pin_level(X4PRO_PIN_EPD_CS, true);
    return true;
}
static bool uc_transfer_plane(uint8_t ram_command, bool white, uint64_t deadline_ms) {
    command(ram_command);
    panel_pin_level(X4PRO_PIN_EPD_DC, true);
    panel_pin_level(X4PRO_PIN_EPD_CS, false);
    const uint8_t *pixels = white && partial_update ? previous_frame : frame;
    for (size_t row = 0; row < 600u; ++row) {
        for (size_t col = 0; col < X4PRO_PANEL_WIDTH / 8u; ++col) {
            uint8_t value = ((white && !partial_update) || row < 120u) ? 0xFFu :
                (uint8_t)~pixels[(row - 120u) * (X4PRO_PANEL_WIDTH / 8u) + col];
            spi_byte(value); ++bytes_sent;
        }
        if (!transfer_checkpoint(deadline_ms, X4PRO_PANEL_WIDTH / 8u)) return false;
    }
    panel_pin_level(X4PRO_PIN_EPD_CS, true);
    return true;
}
static bool uc_transfer_frame(uint64_t deadline_ms) {
    if (transfer_started) { set_reason("invalid state"); return false; }
    transfer_started = true;
    if (!sample_now(&transfer_start_ms) || !uc_ready_for("uc pre-transfer busy", deadline_ms)) return false;
    if (!uc_transfer_plane(0x13, false, deadline_ms) || !uc_transfer_plane(0x10, true, deadline_ms)) return false;
    if (!sample_now(&transfer_end_ms) || transfer_end_ms >= deadline_ms) { set_reason("transfer deadline"); return false; }
    command(0x50); data1(partial_update ? 0xD7 : 0x97);
    command(0xE0); data1(0x02);
    command(0xE5); data1(partial_update ? 0x5A : 0x1E);
    if (partial_update) { command(0x03); data1(0x20); command(0xE1); data1(0x02); }
    if (!sample_now(&refresh_ms) || refresh_ms >= deadline_ms) { set_reason("transfer deadline"); return false; }
    command(0x04);
    /* UC8279 PON may reload MTP settings. Select built-in OTP after PON. */
    sleep_ms(1); /* Give BUSY_N one controller tick to assert, as the pinned bus does. */
    if (!uc_ready_for("uc power-on timeout", deadline_ms)) return false;
    if (partial_update) {
        const uint16_t right = (uint16_t)(update_area.x + update_area.width - 1u);
        const uint16_t top = (uint16_t)update_area.y + 120u;
        const uint16_t bottom = top + (uint16_t)update_area.height - 1u;
        command(0x91); command(0x90);
        data1((uint16_t)update_area.x >> 8); data1((uint16_t)update_area.x & 0xF8u);
        data1(right >> 8); data1(right | 7u);
        data1(top >> 8); data1(top & 255u); data1(bottom >> 8); data1(bottom & 255u); data1(0x01);
    }
    command(0x00); data1(0x17); data1(0x4D);
    busy_before = panel_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    if (!busy_before) { set_reason("busy already active"); return false; }
    command(0x12);
    while (panel_pin_read(X4PRO_PIN_EPD_BUSY)) {
        uint64_t now = 0;
        if (!sample_now(&now) || now >= deadline_ms) { set_reason("busy never asserted"); return false; }
        sleep_ms(1);
    }
    if (!sample_now(&busy_assert_ms)) return false;
    if (!uc_ready_for("busy completion timeout", deadline_ms)) return false;
    if (!sample_now(&busy_done_ms) || busy_done_ms >= deadline_ms) return false;
    if (partial_update) command(0x92);
    return !io_failed;
}
static bool transfer_frame(uint64_t deadline_ms) {
    if (transfer_started) { set_reason("invalid state"); return false; }
    transfer_started = true;
    if (!sample_now(&transfer_start_ms)) return false;
    if (!ready_for("pre-transfer readiness")) return false;
    set_update_window();
    if (!ready_for("pre-transfer readiness")) return false;
    /* Full absolute SSD1677 refresh starts with matched BW and RED planes. */
    if (!transfer_plane(0x24, deadline_ms) || !transfer_plane(0x26, deadline_ms)) return false;
    if (!sample_now(&transfer_end_ms) || transfer_end_ms >= deadline_ms) {
        set_reason("transfer deadline");
        return false;
    }
    busy_before = panel_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    if (busy_before) { set_reason("busy already active"); return false; }
    command(0x21); data1(partial_update ? 0x00 : 0x40);
    command(0x3C); data1(partial_update ? 0x80 : 0xC0);
    command(0x22); data1(partial_update ? 0xFC : 0xF7);
    if (!sample_now(&refresh_ms) || refresh_ms >= deadline_ms) {
        set_reason("transfer deadline");
        return false;
    }
    command(0x20);
    return wait_idle(deadline_ms);
}
static bool get_info_impl(void *context, risc_display_info_v1 *out) {
    (void)context;
    if (!out) return false;
    *out = (risc_display_info_v1){0};
    out->api_version = RISC_DISPLAY_OUTPUT_API_V1;
    out->struct_size = sizeof(*out);
    out->width = X4PRO_PANEL_WIDTH;
    out->height = X4PRO_PANEL_HEIGHT;
    out->supported_formats = RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_MONO1);
    out->preferred_format = RISC_DISPLAY_FORMAT_MONO1;
    out->supported_rotations = RISC_DISPLAY_ROTATION_0;
    out->flags = RISC_DISPLAY_INFO_RETAINS_IMAGE | RISC_DISPLAY_INFO_PARTIAL_DAMAGE | RISC_DISPLAY_INFO_QUIESCE_SLEEP;
    out->damage_x_alignment = 8;
    out->damage_width_alignment = 8;
    out->damage_y_alignment = 1;
    out->damage_height_alignment = 1;
    out->nominal_refresh_millihz = 500;
    out->typical_present_latency_us = 1600000;
    return true;
}
static bool acquire_impl(void *context, uint32_t format, risc_display_surface_v1 *out) {
    (void)context;
    if (!started || shutdown_stage || held || !out || format != RISC_DISPLAY_FORMAT_MONO1) return false;
    if (frame_serial == UINT64_MAX) return false;
    ++frame_serial;
    held = true;
    *out = (risc_display_surface_v1){frame_serial, frame, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT,
        X4PRO_PANEL_WIDTH / 8u, FRAME_BYTES, RISC_DISPLAY_FORMAT_MONO1};
    return true;
}
static void release_impl(void *context, risc_display_frame_v1 frame_id) {
    (void)context;
    if (present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return;
    if (held && frame_id == frame_serial) { held = false; previous_seeded = false; }
}
static bool submit_impl(void *context, risc_display_frame_v1 frame_id, const risc_display_rect_v1 *damage,
                   size_t count, const risc_display_present_options_v1 *options,
                   risc_display_present_token_v1 *token_out) {
    (void)context;
    if (!started || shutdown_stage || !held || frame_id != frame_serial || present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE)
        return false;
    if (options && (options->intent > RISC_DISPLAY_PRESENT_CLEAN || options->queue_policy != RISC_DISPLAY_QUEUE_FIFO || options->reserved)) return false;
    if (!token_out || count > RISC_DISPLAY_MAX_DAMAGE_RECTS || (count && !damage)) return false;
    partial_update = count && previous_seeded && (!options || options->intent != RISC_DISPLAY_PRESENT_CLEAN);
    update_area = (risc_display_rect_v1){0, 0, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT};
    if (count) {
        uint32_t left = X4PRO_PANEL_WIDTH, top = X4PRO_PANEL_HEIGHT, right = 0, bottom = 0;
        for (size_t i = 0; i < count; ++i) {
            const risc_display_rect_v1 *r = &damage[i];
            if (r->x < 0 || r->y < 0 || !r->width || !r->height ||
                r->width > X4PRO_PANEL_WIDTH || r->height > X4PRO_PANEL_HEIGHT ||
                (uint32_t)r->x > X4PRO_PANEL_WIDTH - r->width ||
                (uint32_t)r->y > X4PRO_PANEL_HEIGHT - r->height) return false;
            if ((uint32_t)r->x < left) left = (uint32_t)r->x;
            if ((uint32_t)r->y < top) top = (uint32_t)r->y;
            if ((uint32_t)r->x + r->width > right) right = (uint32_t)r->x + r->width;
            if ((uint32_t)r->y + r->height > bottom) bottom = (uint32_t)r->y + r->height;
        }
        left &= ~7u; right = (right + 7u) & ~7u;
        update_area = (risc_display_rect_v1){(int32_t)left, (int32_t)top, right - left, bottom - top};
    }
    if (token_serial == UINT64_MAX) return false;
    ++token_serial;
    pending_token = token_serial;
    previous_seeded = false; // Consumed by this one admitted submission.
    present_state = PRESENT_QUEUED;
    transfer_started = false;
    if (token_out) *token_out = pending_token;
    return true;
}
static bool present_status_impl(void *context, risc_display_present_token_v1 token, risc_display_present_status_v1 *out) {
    (void)context;
    if (!out || !token || token != pending_token) return false;
    *out = (risc_display_present_status_v1){0};
    out->state = present_state;
    return true;
}
static bool wait_present_impl(void *context, risc_display_present_token_v1 token, uint32_t timeout_ms,
                         risc_display_present_status_v1 *out) {
    if (!out || !token || token != pending_token) return false;
    if (present_state == PRESENT_QUEUED && timeout_ms > 0 && !transfer_started) {
        uint64_t now = 0;
        wait_budget_ms = timeout_ms;
        bytes_sent = 0;
        refresh_ms = busy_assert_ms = busy_done_ms = transfer_start_ms = transfer_end_ms = 0;
        reason = "none";
        if (!sample_now(&now)) {
            present_state = PRESENT_FAILED;
            held = false;
            return present_status_impl(context, token, out);
        }
        wait_start_ms = transfer_yielded_ms = now;
        transfer_work = 0;
        if (timeout_ms > UINT64_MAX - now) {
            set_reason("clock overflow");
            present_state = PRESENT_FAILED;
            held = false;
            return present_status_impl(context, token, out);
        }
        operation_deadline = now + timeout_ms;
        present_state = PRESENT_ACTIVE;
        if (!(controller == PROBE_UC8279 ? uc_transfer_frame(now + timeout_ms) : transfer_frame(now + timeout_ms))) {
            present_state = PRESENT_FAILED;
            held = false;
        } else {
            present_state = PRESENT_COMPLETE;
            reason = "complete";
            held = false;
            previous_seeded = false;
        }
    }
    operation_deadline = 0;
    return present_status_impl(context, token, out);
}
static bool set_brightness_impl(void *context, uint16_t level, uint16_t maximum) {
    (void)context;
    return started && !shutdown_stage && maximum && level<=maximum && frontlight &&
        frontlight->set_level(frontlight->context, level, maximum);
}
static bool seed_previous_impl(void *context, risc_display_frame_v1 frame_id) {
    (void)context;
    if (!started || shutdown_stage || !held || frame_id != frame_serial ||
        present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return false;
    previous_seeded = false;
    const uint64_t began = now_ms();
    if (began == UINT64_MAX || began > UINT64_MAX - 100u) return false;
    uint64_t last_yield = began;
    for (size_t i = 0; i < FRAME_BYTES; ++i) {
        previous_frame[i] = frame[i];
        if ((i & 255u) == 255u) {
            uint64_t now = now_ms();
            if (now == UINT64_MAX || now < last_sample_ms || now - began >= 100u) return false;
            last_sample_ms = now;
            if ((i & 4095u) == 4095u || now - last_yield >= 2u) {
                sleep_ms(1u); last_yield = now;
            }
        }
    }
    const uint64_t completed = now_ms();
    if (completed == UINT64_MAX || completed < began || completed - began >= 100u) return false;
    previous_seeded = true;
    return true;
}
/* All mutable provider state is confined to the runtime's owner task. */
static bool get_info(void *c, risc_display_info_v1 *out) {
    if (!enter()) return false;
    const bool ok = get_info_impl(c, out); return leave() && ok;
}
static bool acquire(void *c, uint32_t format, risc_display_surface_v1 *out) {
    if (!enter()) return false;
    const bool ok = acquire_impl(c, format, out); return leave() && ok;
}
static void release(void *c, risc_display_frame_v1 id) {
    if (!enter()) return;
    release_impl(c, id); (void)leave();
}
static bool submit(void *c, risc_display_frame_v1 id, const risc_display_rect_v1 *damage,
                   size_t count, const risc_display_present_options_v1 *options,
                   risc_display_present_token_v1 *out) {
    if (out) *out = 0;
    if (!enter()) return false;
    const bool ok = submit_impl(c, id, damage, count, options, out);
    if (!leave()) { if (out) *out = 0; return false; }
    return ok;
}
static bool present_status(void *c, risc_display_present_token_v1 token, risc_display_present_status_v1 *out) {
    if (!enter()) return false;
    const bool ok = present_status_impl(c, token, out); return leave() && ok;
}
static bool wait_present(void *c, risc_display_present_token_v1 token, uint32_t ms,
                         risc_display_present_status_v1 *out) {
    if (!sync_api || !sync_api->is_owner(sync_api->context) || !clock_api) return false;
    const uint64_t began = now_ms();
    if (!enter()) return false;
    bool ok = false;
    if (!out || !token || token != pending_token) { (void)leave(); return false; }
    const uint64_t admitted = now_ms();
    if (ms && present_state == PRESENT_QUEUED &&
        (began == UINT64_MAX || admitted == UINT64_MAX || admitted < began ||
         began > UINT64_MAX - ms || admitted - began >= ms)) {
        set_reason("admission deadline"); present_state = PRESENT_FAILED; held = false;
        ok = present_status_impl(c, token, out);
    } else {
        const uint32_t remaining = ms ? ms - (uint32_t)(admitted - began) : 0;
        ok = wait_present_impl(c, token, remaining, out);
    }
    return leave() && ok;
}
static bool set_brightness(void *c, uint16_t level, uint16_t maximum) {
    if (!enter()) return false;
    const bool ok = set_brightness_impl(c, level, maximum); return leave() && ok;
}
static bool seed_previous(void *c, risc_display_frame_v1 id) {
    if (!enter()) return false;
    const bool ok = seed_previous_impl(c, id); return leave() && ok;
}
static const risc_display_output_api_v1_history api = {
    { RISC_DISPLAY_OUTPUT_API_V1, sizeof(api), 0, get_info, acquire, release, submit,
      present_status, wait_present, set_brightness },
    RISC_DISPLAY_HISTORY_TAG, 1u, seed_previous
};
/* Typed lifecycle. */
static bool valid_configuration(const risc_hardware_device_v1 *h, int *expected) {
    if (!h || h->api_version != 1 || h->struct_size < sizeof(*h) || !h->instance_id ||
        !h->compatible || !h->revision || strcmp(h->revision, "unspecified") ||
        !h->config_type || strcmp(h->config_type, "display.spi") || h->config_version != 1 ||
        h->config_size != sizeof(risc_hw_spi_display_v1) || !h->config) return false;
    if (!strcmp(h->compatible, "solomon-systech,ssd1677")) *expected = PROBE_SSD;
    else if (!strcmp(h->compatible, "ultrachip,uc8279")) *expected = PROBE_UC8279;
    else return false;
    const risc_hw_spi_display_v1 *c = h->config;
    if (c->struct_size != sizeof(*c) || c->bus.struct_size != sizeof(c->bus) ||
        c->bus.kind != RISC_HW_BUS_SPI || !c->bus.instance_id || c->bus.controller > 2 ||
        !c->bus.frequency_hz || c->bus.frequency_hz > 1000000u || c->bus.mode ||
        c->bus.reserved[0] || c->bus.reserved[1] || c->bus.reserved[2] ||
        c->bus.sclk != 12 || c->bus.mosi != 11 || c->bus.miso != -1 || c->bus.sda != -1 || c->bus.scl != -1 ||
        c->cs != 13 || c->dc != 18 || c->reset != 14 || c->busy != 6 || c->backlight != -1 ||
        c->width != 800 || c->height != 480 || c->offset_x || c->offset_y != (*expected == PROBE_UC8279 ? 120 : 0) ||
        c->rotation || c->reserved[0] || c->reserved[1] || c->reserved[2] || c->reset_active_high ||
        c->busy_active_high != (*expected == PROBE_SSD) || c->backlight_active_high || c->power_count ||
        c->reset_assert_ms != (*expected == PROBE_UC8279 ? 50u : 10u) ||
        c->reset_recovery_ms != (*expected == PROBE_UC8279 ? 50u : 10u)) return false;
    for (size_t i = 0; i < RISC_HW_MAX_POWER_PINS; ++i)
        if (c->power_pins[i] != 0 || c->power_active_high[i]) return false;
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (gpio || sync_api || mutex || started || retained || !deps || count != 6) return false;
    const risc_hardware_device_v1 *hardware = NULL;
    const garden_gpio_v1 *candidate = NULL;
    const risc_platform_clock_api_v1 *clock = NULL;
    const risc_provider_sync_api_v1 *sync = NULL;
    const x4_power_ready_api_v1 *power = NULL;
    const risc_frontlight_api_v1 *light = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!deps[i].capability_id || deps[i].api_version != 1 || !deps[i].api) return false;
        const char *name = deps[i].capability_id;
        if (!strcmp(name, "hardware.device") && !hardware) hardware = deps[i].api;
        else if (!strcmp(name, "platform.gpio") && !candidate) candidate = deps[i].api;
        else if (!strcmp(name, "platform.clock") && !clock) clock = deps[i].api;
        else if (!strcmp(name, RISC_PROVIDER_SYNC_CAPABILITY) && !sync) sync = deps[i].api;
        else if (!strcmp(name, X4_POWER_READY_CAPABILITY) && !power) power = deps[i].api;
        else if (!strcmp(name, "display.frontlight") && !light) light = deps[i].api;
        else return false;
    }
    int expected = 0;
    if (!valid_configuration(hardware, &expected) ||
        !candidate || candidate->api_version != 1 || candidate->struct_size < GARDEN_GPIO_DEEP_SLEEP_HOLD_V1_SIZE ||
        !candidate->claim || !candidate->write || !candidate->read || !candidate->release || !candidate->deep_sleep_hold ||
        !clock || clock->api_version != 1 || clock->struct_size < sizeof(*clock) || !clock->monotonic_ms || !clock->sleep_ms ||
        !sync || sync->api_version != 1 || sync->struct_size < sizeof(*sync) || !sync->is_owner ||
        !sync->create || !sync->try_lock || !sync->unlock || !sync->destroy ||
        !power || power->api_version != 1 || power->struct_size < sizeof(*power) || !power->ready ||
        !light || light->api_version!=1 || light->struct_size<sizeof(*light) || !light->set_level ||
        !sync->is_owner(sync->context) || !power->ready(power->context)) return false;
    const uint64_t now = clock->monotonic_ms(clock->context);
    if (now == UINT64_MAX || now > UINT64_MAX - 2000u) return false;
    frontlight = light; gpio = candidate; clock_api = clock; sync_api = sync; configuration = hardware->config;
    if (!sync_api->create(sync_api->context, &mutex) || !mutex) {
        frontlight = NULL; gpio = NULL; clock_api = NULL; sync_api = NULL; configuration = NULL; return false;
    }
    if (!enter()) return false;
    physical_pins[X4PRO_PIN_EPD_BUSY] = (uint8_t)configuration->busy;
    physical_pins[X4PRO_PIN_EPD_CS] = (uint8_t)configuration->cs;
    physical_pins[X4PRO_PIN_EPD_SCLK] = (uint8_t)configuration->bus.sclk;
    physical_pins[X4PRO_PIN_EPD_MOSI] = (uint8_t)configuration->bus.mosi;
    physical_pins[X4PRO_PIN_EPD_DC] = (uint8_t)configuration->dc;
    physical_pins[X4PRO_PIN_EPD_RST] = (uint8_t)configuration->reset;
    shutdown_stage = 0; previous_seeded = partial_update = false;
    present_state = PRESENT_NONE; pending_token = 0; last_sample_ms = now;
    reason = "none"; operation_deadline = now + 2000u;
    prepare_pins();
    const int verdict = probe_controller();
    if (verdict != expected) {
        set_reason(verdict == PROBE_AMBIGUOUS ? "ambiguous-controller" : "controller mismatch");
        (void)leave(); return false;
    }
    controller = verdict;
    for (size_t i = 0; i < sizeof(frame); ++i) frame[i] = 0;
    started = !io_failed && (controller == PROBE_UC8279 ? uc_init_panel() : init_panel());
    operation_deadline = 0;
    return leave() && started;
}
static bool quiesce_impl(void) {
    if (shutdown_stage == 4u) return true;
    if (held || present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return false;
    if (!pins_ready) return true;
    if (!clock_api || (controller != PROBE_SSD && controller != PROBE_UC8279)) return false;
    const uint64_t began = now_ms();
    if (began == UINT64_MAX || began < last_sample_ms || began > UINT64_MAX - 1500u) return false;
    const uint64_t deadline = began + 1500u;
    if (shutdown_stage == 0u) {
        const bool busy = controller == PROBE_UC8279 ? !panel_pin_read(X4PRO_PIN_EPD_BUSY)
                                                   : panel_pin_read(X4PRO_PIN_EPD_BUSY);
        if (io_failed || busy) return false;
        if (controller == PROBE_UC8279) command(0x02);
        else { command(0x3C); data1(0x80); command(0x22); data1(0x03); command(0x20); }
        if (io_failed) return false;
        shutdown_stage = 1u;
        sleep_ms(controller == PROBE_UC8279 ? 1u : 200u);
    }
    if (shutdown_stage == 1u) {
        for (unsigned checks = 0; checks < 150u; ++checks) {
            uint64_t now = 0;
            if (!sample_now(&now) || now < began || now >= deadline) return false;
            const bool busy = controller == PROBE_UC8279 ? !panel_pin_read(X4PRO_PIN_EPD_BUSY)
                                                       : panel_pin_read(X4PRO_PIN_EPD_BUSY);
            if (io_failed) return false;
            if (!busy) { shutdown_stage = 2u; break; }
            sleep_ms(10u);
        }
        if (shutdown_stage != 2u) return false;
    }
    if (shutdown_stage == 2u) {
        if (controller == PROBE_UC8279) { command(0x07); data1(0xA5); }
        else { command(0x10); data1(0x03); }
        if (io_failed) return false;
        shutdown_stage = 3u;
    }
    if (!reset_held) {
        panel_pin_output(X4PRO_PIN_EPD_RST, true);
        if (io_failed) return false;
        const int32_t result = gpio->deep_sleep_hold(gpio->context, pin_tokens[X4PRO_PIN_EPD_RST], true);
        if (result) {
            if (result == RISC_DEEP_SLEEP_RETAINED) retained = true;
            set_reason("reset hold failed"); return false;
        }
        reset_held = true;
    }
    started = false;
    /* Ordinary release intentionally cannot dispose of a held output. Only
     * an explicit Runtime retirement suffix may take over its safe pad state. */
#ifdef GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE
    if (gpio->struct_size >= GARDEN_GPIO_RETIRE_HELD_OUTPUT_V1_SIZE && gpio->retire_held_output) {
        if (!gpio->retire_held_output(gpio->context, pin_tokens[X4PRO_PIN_EPD_RST])) {
            set_reason("reset retirement retained"); return false;
        }
        pin_tokens[X4PRO_PIN_EPD_RST] = 0;
        shutdown_stage = 4u;
        return true;
    }
#endif
    set_reason("reset hold retains ownership");
    return false;
}
static bool quiesce(void) {
    if (!mutex) return !gpio && !retained;
    if (!enter()) return false;
    if (!quiesce_impl()) { (void)leave(); return false; }
    for (size_t i = 0; i < PANEL_PINS; ++i) {
        if (!pin_tokens[i]) continue;
        if (!gpio->release(gpio->context, pin_tokens[i])) { (void)leave(); return false; }
        pin_tokens[i] = 0;
    }
    if (!leave()) return false;
    if (!sync_api->destroy(sync_api->context, mutex)) return false;
    mutex = 0; frontlight = NULL; gpio = NULL; clock_api = NULL; sync_api = NULL; configuration = NULL;
    started = pins_ready = reset_held = false; controller = 0;
    return true;
}
static void stop(void) { /* Accepted quiescence performs all fallible work. */ }
static bool append(char *destination, size_t capacity, size_t *used, const char *text) {
    while (*text && *used + 1u < capacity) destination[(*used)++] = *text++;
    destination[*used] = 0;
    return *text == 0;
}
static bool append_u(char *destination, size_t capacity, size_t *used, uint64_t value) {
    char digits[20];
    size_t n = 0;
    do { digits[n++] = (char)('0' + (value % 10u)); value /= 10u; } while (value && n < sizeof(digits));
    while (n && *used + 1u < capacity) destination[(*used)++] = digits[--n];
    destination[*used] = 0;
    return n == 0;
}
static bool last_error(char *destination, size_t capacity) {
    if (!destination || !capacity || (sync_api && !sync_api->is_owner(sync_api->context))) return false;
    uint64_t now = now_ms();
    size_t used = 0;
    destination[0] = 0;
    append(destination, capacity, &used, probe_text);
    append(destination, capacity, &used, " v=0.1.15 token=");
    append_u(destination, capacity, &used, pending_token);
    append(destination, capacity, &used, " state=");
    append_u(destination, capacity, &used, present_state);
    append(destination, capacity, &used, " reason=");
    append(destination, capacity, &used, reason);
    append(destination, capacity, &used, " budget=");
    append_u(destination, capacity, &used, wait_budget_ms);
    append(destination, capacity, &used, " elapsed=");
    append_u(destination, capacity, &used, now == UINT64_MAX || now < wait_start_ms ? 0 : now - wait_start_ms);
    append(destination, capacity, &used, " xfer=");
    append_u(destination, capacity, &used, bytes_sent);
    append(destination, capacity, &used, "/");
    append_u(destination, capacity, &used, transfer_end_ms && transfer_end_ms >= transfer_start_ms ? transfer_end_ms - transfer_start_ms : 0);
    append(destination, capacity, &used, "ms refresh=");
    append_u(destination, capacity, &used, refresh_ms);
    append(destination, capacity, &used, " busy0=");
    append_u(destination, capacity, &used, busy_before);
    append(destination, capacity, &used, " assert=");
    append_u(destination, capacity, &used, busy_assert_ms);
    append(destination, capacity, &used, " ");
    append(destination, capacity, &used, probe_text);
    return used > 0;
}
static const risc_driver_diagnostics_v2 driver = {
    { RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2), "x4pro-panel",
      "display.output", 1, &api, start, stop, quiesce },
    last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver.base;
}
