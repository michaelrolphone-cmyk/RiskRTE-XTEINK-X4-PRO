/* X4 Pro 800x480 panel provider for gated SSD1677 and UC8279 variants.
 * SSD BUSY is active-high; UC BUSY is active-low. No external display PMIC.
 * GPIO1 is the recovered peripheral-enable name and is not driven here.
 * Touch power GPIO2 and SD power GPIO5 stay untouched. */
#include "RiscDisplayOutputV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include <stddef.h>
#include <stdint.h>

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

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
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
        x4pro_pin_level(X4PRO_PIN_EPD_MOSI, (value >> bit) & 1);
        x4pro_pin_level(X4PRO_PIN_EPD_SCLK, true);
        x4pro_pin_level(X4PRO_PIN_EPD_SCLK, false);
    }
}
static void command(uint8_t cmd) {
    x4pro_pin_level(X4PRO_PIN_EPD_DC, false);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    spi_byte(cmd);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
}
static void data1(uint8_t value) {
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    spi_byte(value);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
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
    uint64_t now = now_ms();
    if (now == UINT64_MAX) { set_reason("clock failure"); return false; }
    if (wait_start_ms && now < wait_start_ms) { set_reason("clock nonmonotonic"); return false; }
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
        saw_busy = x4pro_pin_read(X4PRO_PIN_EPD_BUSY);
        if (saw_busy && !busy_before && !busy_assert_ms) busy_assert_ms = now;
        if (!saw_busy) sleep_ms(10);
    }
    while (x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        uint64_t now = 0;
        if (!sample_now(&now) || now >= deadline_ms) {
            if (reason[0] == 'n') set_reason("busy completion timeout");
            return false;
        }
        sleep_ms(10);
    }
    return sample_now(&busy_done_ms);
}
static char probe_text[96] = "probe=not-run";
static bool append(char *destination, size_t capacity, size_t *used, const char *text);
static bool append_u(char *destination, size_t capacity, size_t *used, uint64_t value);
static void prepare_pins(void);
static void clock_delay(void) {
    for (volatile int i = 0; i < 32; ++i) (void)x4pro_pin_read(X4PRO_PIN_EPD_BUSY);
}
static uint8_t read_byte(void) {
    uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit) {
        clock_delay();
        value = (uint8_t)((value << 1) | (x4pro_pin_read(X4PRO_PIN_EPD_MOSI) ? 1u : 0u));
        x4pro_pin_level(X4PRO_PIN_EPD_SCLK, true);
        clock_delay();
        x4pro_pin_level(X4PRO_PIN_EPD_SCLK, false);
    }
    return value;
}
static void read_cmd(uint8_t cmd, uint8_t *out, size_t len) {
    x4pro_pin_output(X4PRO_PIN_EPD_MOSI, false);
    x4pro_pin_level(X4PRO_PIN_EPD_DC, false);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    clock_delay();
    spi_byte(cmd);
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_input(X4PRO_PIN_EPD_MOSI, true);
    clock_delay();
    for (size_t i = 0; i < len; ++i) out[i] = read_byte();
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    x4pro_pin_output(X4PRO_PIN_EPD_MOSI, false);
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
__attribute__((weak)) int x4_test_probe_mode = 0;
static int probe_controller(void) {
    uint8_t flg = 0;
    uint8_t ver[5] = {0};
    prepare_pins();
    const uint8_t busy_before_reset = x4pro_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    x4pro_epd_reset_unhold();
    x4pro_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(1);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    sleep_ms(30);
    const uint8_t busy = x4pro_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    int verdict = PROBE_AMBIGUOUS;
    if (x4_test_probe_mode == 1) verdict = PROBE_DISABLED;
    else if (x4_test_probe_mode == 2) verdict = PROBE_AMBIGUOUS;
    else if (x4_test_probe_mode == 3) { verdict = PROBE_UC8279; ver[2] = 0x68; flg = 0x13; }
    else if (x4_test_probe_mode == 4) verdict = PROBE_SSD;
    else {
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
    return verdict;
}
static void prepare_pins(void) {
    if (pins_ready) return;
    x4pro_pin_input(X4PRO_PIN_EPD_BUSY, false);
    x4pro_pin_output(X4PRO_PIN_EPD_CS, true);
    x4pro_pin_output(X4PRO_PIN_EPD_SCLK, false);
    x4pro_pin_output(X4PRO_PIN_EPD_MOSI, false);
    x4pro_pin_output(X4PRO_PIN_EPD_DC, false);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    x4pro_epd_reset_unhold();
    pins_ready = true;
}
static const uint8_t x_window[] = {0x00, 0x00, 0x1F, 0x03};
static const uint8_t y_window[] = {0xDF, 0x01, 0x00, 0x00};
static void set_cursor(void) {
    command(0x4E); data1(0x00); data1(0x00);
    command(0x4F); data1(0xDF); data1(0x01);
}
static bool ready_for(const char *failure) {
    if (!x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) return true;
    uint64_t deadline = now_ms() + 500u;
    while (x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        if (now_ms() >= deadline) { set_reason(failure); return false; }
        sleep_ms(10);
    }
    return true;
}
static bool uc_ready_for(const char *failure, uint64_t deadline_ms) {
    while (!x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        uint64_t now = 0;
        if (!sample_now(&now) || now >= deadline_ms) { set_reason(failure); return false; }
        sleep_ms(10);
    }
    return true;
}
static bool uc_init_panel(void) {
    /* FreeInk UC8279 X4 Pro: use panel-programmed voltage and OTP waveform. */
    x4pro_epd_reset_unhold();
    x4pro_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(50);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    sleep_ms(50);
    uint64_t now = 0;
    if (!sample_now(&now) || !uc_ready_for("uc reset busy timeout", now + 500u)) return false;
    command(0x00); data1(0x37); data1(0x4D);
    command(0x61); data1(0x03); data1(0x20); data1(0x02); data1(0x58);
    command(0x65); data1(0); data1(0); data1(0); data1(0);
    command(0x03); data1(0x20);
    command(0x30); data1(0x0E);
    command(0xE1); data1(0x02);
    return true;
}
static bool init_panel(void) {
    static const uint8_t booster[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
    static const uint8_t gate[] = {0xDF, 0x01, 0x02};
    prepare_pins();
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    x4pro_epd_reset_unhold();
    x4pro_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(10);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
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
    return true;
}
static bool transfer_checkpoint(uint64_t deadline_ms, unsigned work) {
    uint64_t now = 0;
    if (!sample_now(&now) || now >= deadline_ms) {
        x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
        transfer_end_ms = now;
        set_reason("transfer deadline");
        return false;
    }
    transfer_work += work;
    if (transfer_work >= 1024u || now - transfer_yielded_ms >= 2u) {
        sleep_ms(1u);
        if (!sample_now(&now) || now >= deadline_ms) {
            x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
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
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
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
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    return true;
}
static bool uc_transfer_plane(uint8_t ram_command, bool white, uint64_t deadline_ms) {
    command(ram_command);
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    const uint8_t *pixels = white && partial_update ? previous_frame : frame;
    for (size_t row = 0; row < 600u; ++row) {
        for (size_t col = 0; col < X4PRO_PANEL_WIDTH / 8u; ++col) {
            uint8_t value = ((white && !partial_update) || row < 120u) ? 0xFFu :
                (uint8_t)~pixels[(row - 120u) * (X4PRO_PANEL_WIDTH / 8u) + col];
            spi_byte(value); ++bytes_sent;
        }
        if (!transfer_checkpoint(deadline_ms, X4PRO_PANEL_WIDTH / 8u)) return false;
    }
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
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
    busy_before = x4pro_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    if (!busy_before) { set_reason("busy already active"); return false; }
    command(0x12);
    while (x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        uint64_t now = 0;
        if (!sample_now(&now) || now >= deadline_ms) { set_reason("busy never asserted"); return false; }
        sleep_ms(1);
    }
    if (!sample_now(&busy_assert_ms)) return false;
    if (!uc_ready_for("busy completion timeout", deadline_ms)) return false;
    if (!sample_now(&busy_done_ms)) return false;
    if (partial_update) command(0x92);
    return true;
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
    busy_before = x4pro_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
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
static bool get_info(void *context, risc_display_info_v1 *out) {
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
static bool acquire(void *context, uint32_t format, risc_display_surface_v1 *out) {
    (void)context;
    if (!started || shutdown_stage || held || !out || format != RISC_DISPLAY_FORMAT_MONO1) return false;
    if (++frame_serial == 0) ++frame_serial;
    held = true;
    *out = (risc_display_surface_v1){frame_serial, frame, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT,
        X4PRO_PANEL_WIDTH / 8u, FRAME_BYTES, RISC_DISPLAY_FORMAT_MONO1};
    return true;
}
static void release(void *context, risc_display_frame_v1 frame_id) {
    (void)context;
    if (present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return;
    if (held && frame_id == frame_serial) { held = false; previous_seeded = false; }
}
static bool submit(void *context, risc_display_frame_v1 frame_id, const risc_display_rect_v1 *damage,
                   size_t count, const risc_display_present_options_v1 *options,
                   risc_display_present_token_v1 *token_out) {
    (void)context;
    if (!started || shutdown_stage || !held || frame_id != frame_serial || present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE)
        return false;
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
    if (++token_serial == 0) ++token_serial;
    pending_token = token_serial;
    previous_seeded = false; // Consumed by this one admitted submission.
    present_state = PRESENT_QUEUED;
    transfer_started = false;
    if (token_out) *token_out = pending_token;
    return true;
}
static bool present_status(void *context, risc_display_present_token_v1 token, risc_display_present_status_v1 *out) {
    (void)context;
    if (!out || token != pending_token) return false;
    out->state = present_state;
    return true;
}
static bool wait_present(void *context, risc_display_present_token_v1 token, uint32_t timeout_ms,
                         risc_display_present_status_v1 *out) {
    if (token != pending_token) return false;
    if (present_state == PRESENT_QUEUED && timeout_ms > 0 && !transfer_started) {
        uint64_t now = 0;
        wait_budget_ms = timeout_ms;
        bytes_sent = 0;
        refresh_ms = busy_assert_ms = busy_done_ms = transfer_start_ms = transfer_end_ms = 0;
        reason = "none";
        if (!sample_now(&now)) {
            present_state = PRESENT_FAILED;
            held = false;
            return present_status(context, token, out);
        }
        wait_start_ms = transfer_yielded_ms = now;
        transfer_work = 0;
        if (timeout_ms > UINT64_MAX - now) {
            set_reason("clock overflow");
            present_state = PRESENT_FAILED;
            held = false;
            return present_status(context, token, out);
        }
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
    return present_status(context, token, out);
}
static bool set_brightness(void *context, uint16_t level, uint16_t maximum) {
    (void)context; (void)level; (void)maximum;
    return false;
}
static bool seed_previous(void *context, risc_display_frame_v1 frame_id) {
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
            if (now == UINT64_MAX || now < began || now - began >= 100u) return false;
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
static const risc_display_output_api_v1_history api = {
    { RISC_DISPLAY_OUTPUT_API_V1, sizeof(api), 0, get_info, acquire, release, submit,
      present_status, wait_present, set_brightness },
    RISC_DISPLAY_HISTORY_TAG, 1u, seed_previous
};
static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    shutdown_stage = 0;
    previous_seeded = partial_update = false;
    clock_api = 0;
    for (size_t i = 0; i < count; ++i)
        if (equal(dependencies[i].capability_id, "platform.clock") && dependencies[i].api_version == 1)
            clock_api = dependencies[i].api;
    if (!clock_api) { fail("platform.clock missing"); return false; }
    prepare_pins();
    const int verdict = probe_controller();
    if (verdict != PROBE_SSD && verdict != PROBE_UC8279) {
        set_reason(verdict == PROBE_DISABLED ? "probe-disabled" : "ambiguous-controller");
        return false;
    }
    controller = verdict;
    for (size_t i = 0; i < sizeof(frame); ++i) frame[i] = 0;
    started = controller == PROBE_UC8279 ? uc_init_panel() : init_panel();
    return started;
}
static void stop(void) { started = false; held = false; }
static bool quiesce(void) {
    if (held || present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return false;
    if (!pins_ready || shutdown_stage == 3u) { started = false; return true; }
    if (!clock_api || (controller != PROBE_SSD && controller != PROBE_UC8279)) return false;
    const uint64_t began = now_ms();
    if (began == UINT64_MAX || began > UINT64_MAX - 1500u) return false;
    const uint64_t deadline = began + 1500u;
    if (shutdown_stage == 0u) {
        const bool busy = controller == PROBE_UC8279 ? !x4pro_pin_read(X4PRO_PIN_EPD_BUSY)
                                                   : x4pro_pin_read(X4PRO_PIN_EPD_BUSY);
        if (busy) return false; // A timed-out physical refresh remains owned.
        /* Pinned FreeInk 111fdcc7 X4 shutdown sequences. No T5 PMIC/GPIO path. */
        if (controller == PROBE_UC8279) command(0x02);
        else { command(0x3C); data1(0x80); command(0x22); data1(0x03); command(0x20); }
        shutdown_stage = 1u; // Retry observes the outstanding POF, never resends.
        sleep_ms(controller == PROBE_UC8279 ? 1u : 200u);
    }
    if (shutdown_stage == 1u) {
        for (unsigned checks = 0; checks < 150u; ++checks) {
            const uint64_t now = now_ms();
            if (now == UINT64_MAX || now < began || now >= deadline) return false;
            const bool busy = controller == PROBE_UC8279 ? !x4pro_pin_read(X4PRO_PIN_EPD_BUSY)
                                                       : x4pro_pin_read(X4PRO_PIN_EPD_BUSY);
            if (!busy) { shutdown_stage = 2u; break; }
            sleep_ms(10u);
        }
        if (shutdown_stage != 2u) return false;
    }
    if (controller == PROBE_UC8279) { command(0x07); data1(0xA5); }
    else { command(0x10); data1(0x03); }
    // X4 has no panel rail switch. Hold RESET high so it cannot leave DSLP.
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    x4pro_pin_hold(X4PRO_PIN_EPD_RST, true);
    shutdown_stage = 3u;
    started = false;
    return true;
}
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
    if (!destination || !capacity) return false;
    uint64_t now = now_ms();
    size_t used = 0;
    destination[0] = 0;
    append(destination, capacity, &used, probe_text);
    append(destination, capacity, &used, " v=0.1.12 token=");
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
