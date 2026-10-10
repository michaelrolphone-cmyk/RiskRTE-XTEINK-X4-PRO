/* X4 Pro 800x480 panel provider for gated SSD1677 and UC8279 variants.
 * SSD BUSY is active-high; UC BUSY is active-low. No external display PMIC.
 * GPIO1 is the recovered peripheral-enable name and is not driven here.
 * Touch power GPIO2 and SD power GPIO5 stay untouched. */
#include "RiscDisplayOutputV1.h"
#include "RiscDisplayOutputPowerV1.h"
#include "../../interfaces/RiscDisplayOutputMetricsV1.h"
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
enum { PRESENT_NONE = 0, PRESENT_QUEUED = 1, PRESENT_ACTIVE = 2, PRESENT_COMPLETE = 3, PRESENT_FAILED = 5 };
static uint8_t present_state;
static risc_display_present_metrics_v1 metrics;
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
    if (!gpio || pin >= PANEL_PINS || !pin_tokens[pin] || !pin_output[pin]) { pin_failed(); return; }
    if (present_state == PRESENT_ACTIVE) ++metrics.gpio_write_calls;
    if (!gpio->write(gpio->context, pin_tokens[pin], level)) pin_failed();
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
static bool previous_seeded, completed_history, partial_update;
static risc_display_rect_v1 update_area;
static uint64_t transfer_yielded_ms;
static unsigned transfer_work;
static bool transfer_started;
enum { UC_ASYNC_NONE, UC_ASYNC_PRE, UC_ASYNC_PLANE13, UC_ASYNC_PLANE10,
       UC_ASYNC_SETUP, UC_ASYNC_PON, UC_ASYNC_REFRESH, UC_ASYNC_ASSERT, UC_ASYNC_DONE,
       SSD_ASYNC_PRE, SSD_ASYNC_WINDOW_X, SSD_ASYNC_WINDOW_Y, SSD_ASYNC_CURSOR_X,
       SSD_ASYNC_CURSOR_Y, SSD_ASYNC_READY, SSD_ASYNC_PLANE24, SSD_ASYNC_OPEN26,
       SSD_ASYNC_PLANE26, SSD_ASYNC_SETUP21, SSD_ASYNC_SETUP3C, SSD_ASYNC_SETUP22,
       SSD_ASYNC_REFRESH, SSD_ASYNC_ASSERT, SSD_ASYNC_DONE };
static uint8_t async_stage;
static uint32_t async_offset;
static uint64_t async_deadline, async_not_before;
static bool started, held, pins_ready;
/* 0 awake, 1 POF sent, 2 POF observed, 3 DSLP sent, 4 retired, 5 resuming. */
static uint8_t shutdown_stage;
static uint64_t shutdown_not_before;
static int controller;
static uint64_t frame_serial, token_serial, pending_token;
static char last_error_text[64];

static void fail(const char *text) {
    if(last_error_text[0])return; // Preserve the first failure through cleanup.
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
static bool sample_metric(uint64_t *out, uint32_t flag) {
    if (!sample_now(out)) return false;
    metrics.valid_times |= flag; return true;
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
        if (saw_busy && !busy_before && !busy_assert_ms) {
            busy_assert_ms = now; metrics.valid_times |= RISC_DISPLAY_METRICS_BUSY_ASSERT;
        }
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
    return sample_metric(&busy_done_ms, RISC_DISPLAY_METRICS_BUSY_DONE) && busy_done_ms < deadline_ms;
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
    if (!sample_metric(&transfer_start_ms, RISC_DISPLAY_METRICS_TRANSFER_START) || !uc_ready_for("uc pre-transfer busy", deadline_ms)) return false;
    if (!uc_transfer_plane(0x13, false, deadline_ms) || !uc_transfer_plane(0x10, true, deadline_ms)) return false;
    if (!sample_metric(&transfer_end_ms, RISC_DISPLAY_METRICS_TRANSFER_END) || transfer_end_ms >= deadline_ms) { set_reason("transfer deadline"); return false; }
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
    if (!sample_metric(&metrics.refresh_ms, RISC_DISPLAY_METRICS_REFRESH)) return false;
    command(0x12);
    while (panel_pin_read(X4PRO_PIN_EPD_BUSY)) {
        uint64_t now = 0;
        if (!sample_now(&now) || now >= deadline_ms) { set_reason("busy never asserted"); return false; }
        sleep_ms(1);
    }
    if (!sample_metric(&busy_assert_ms, RISC_DISPLAY_METRICS_BUSY_ASSERT)) return false;
    if (!uc_ready_for("busy completion timeout", deadline_ms)) return false;
    if (!sample_metric(&busy_done_ms, RISC_DISPLAY_METRICS_BUSY_DONE) || busy_done_ms >= deadline_ms) return false;
    if (partial_update) command(0x92);
    return !io_failed;
}
static bool transfer_frame(uint64_t deadline_ms) {
    if (transfer_started) { set_reason("invalid state"); return false; }
    transfer_started = true;
    if (!sample_metric(&transfer_start_ms, RISC_DISPLAY_METRICS_TRANSFER_START)) return false;
    if (!ready_for("pre-transfer readiness")) return false;
    set_update_window();
    if (!ready_for("pre-transfer readiness")) return false;
    /* Full absolute SSD1677 refresh starts with matched BW and RED planes. */
    if (!transfer_plane(0x24, deadline_ms) || !transfer_plane(0x26, deadline_ms)) return false;
    if (!sample_metric(&transfer_end_ms, RISC_DISPLAY_METRICS_TRANSFER_END) || transfer_end_ms >= deadline_ms) {
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
    metrics.refresh_ms = refresh_ms; metrics.valid_times |= RISC_DISPLAY_METRICS_REFRESH;
    command(0x20);
    return wait_idle(deadline_ms);
}
/* Record only pixels whose visible update completed. Unsubmitted caller
 * edits and a partial refresh's untouched pixels never become history. */
static void remember_completed_frame(void) {
    if (controller != PROBE_UC8279) return;
    if (partial_update) {
        const size_t row_bytes = X4PRO_PANEL_WIDTH / 8u;
        const size_t left = (size_t)update_area.x / 8u;
        const size_t length = update_area.width / 8u;
        for (size_t y = (size_t)update_area.y; y < (size_t)update_area.y + update_area.height; ++y)
            memcpy(previous_frame + y * row_bytes + left, frame + y * row_bytes + left, length);
    } else memcpy(previous_frame, frame, FRAME_BYTES);
    completed_history = true;
}
/* SSD1677 preserves the synchronous wire sequence, including the full/partial
 * RAM source and update-control bytes. Only owner scheduling changes: no sleep
 * or readiness spin occurs here. Each poll sends at most 512 pixel bytes and
 * samples time after eight bytes or one <=5-byte register command. A native
 * GPIO callback itself must remain bounded; it cannot be preempted by us. */
static uint64_t ssd_ready_deadline;
static void ssd_begin_readiness(uint64_t now) {
    ssd_ready_deadline = now >= async_deadline || async_deadline - now < 500u ? async_deadline : now + 500u;
}
static bool ssd_async_ready(uint64_t now) {
    if (now >= ssd_ready_deadline) { set_reason("pre-transfer readiness"); return false; }
    return !panel_pin_read(X4PRO_PIN_EPD_BUSY) && !io_failed;
}
static void ssd_poll_present(uint32_t budget_ms) {
    if (!budget_ms || !started || shutdown_stage ||
        (present_state != PRESENT_QUEUED && present_state != PRESENT_ACTIVE) || !enter()) return;
    uint64_t now = 0;
    if (!sample_now(&now)) goto failed;
    if (present_state == PRESENT_QUEUED) {
        if (now > UINT64_MAX - 10000u) { set_reason("clock overflow"); goto failed; }
        wait_start_ms = transfer_start_ms = now; wait_budget_ms = 10000u;
        metrics.valid_times |= RISC_DISPLAY_METRICS_TRANSFER_START;
        transfer_end_ms = refresh_ms = busy_assert_ms = busy_done_ms = 0;
        bytes_sent = 0; reason = "none"; transfer_started = true;
        async_stage = SSD_ASYNC_PRE; async_offset = 0; async_deadline = now + 10000u;
        ssd_begin_readiness(now); present_state = PRESENT_ACTIVE;
    }
    if (now >= async_deadline) { set_reason("async present deadline"); goto failed; }
    const uint32_t slice_ms = budget_ms > 8u ? 8u : budget_ms;
    const uint64_t slice_end = async_deadline - now < slice_ms ? async_deadline : now + slice_ms;
    const unsigned x = partial_update ? (unsigned)update_area.x / 8u : 0u;
    const unsigned y = partial_update ? (unsigned)update_area.y : 0u;
    const unsigned w = partial_update ? update_area.width / 8u : X4PRO_PANEL_WIDTH / 8u;
    const unsigned h = partial_update ? update_area.height : X4PRO_PANEL_HEIGHT;
    const uint16_t left = (uint16_t)(x * 8u), right = (uint16_t)(left + w * 8u - 1u);
    const uint16_t first = (uint16_t)(X4PRO_PANEL_HEIGHT - 1u - y), last = (uint16_t)(first + 1u - h);
    unsigned work = 0;
    do {
        if (!sample_now(&now)) goto failed;
        if (now >= async_deadline) { set_reason("async present deadline"); goto failed; }
        if (async_stage == SSD_ASYNC_PRE || async_stage == SSD_ASYNC_READY) {
            if (!ssd_async_ready(now)) {
                if (io_failed || now >= ssd_ready_deadline) goto failed;
                break;
            }
            if (async_stage == SSD_ASYNC_PRE) async_stage = SSD_ASYNC_WINDOW_X;
            else {
                command(0x24); panel_pin_level(X4PRO_PIN_EPD_DC, true);
                panel_pin_level(X4PRO_PIN_EPD_CS, false); async_stage = SSD_ASYNC_PLANE24;
            }
        } else if (async_stage == SSD_ASYNC_WINDOW_X) {
            command(0x44); data1(left & 255u); data1(left >> 8); data1(right & 255u); data1(right >> 8);
            async_stage = SSD_ASYNC_WINDOW_Y;
        } else if (async_stage == SSD_ASYNC_WINDOW_Y) {
            command(0x45); data1(first & 255u); data1(first >> 8); data1(last & 255u); data1(last >> 8);
            async_stage = SSD_ASYNC_CURSOR_X;
        } else if (async_stage == SSD_ASYNC_CURSOR_X) {
            command(0x4E); data1(left & 255u); data1(left >> 8); async_stage = SSD_ASYNC_CURSOR_Y;
        } else if (async_stage == SSD_ASYNC_CURSOR_Y) {
            command(0x4F); data1(first & 255u); data1(first >> 8);
            if (!sample_now(&now)) goto failed;
            ssd_begin_readiness(now); async_stage = SSD_ASYNC_READY;
        } else if (async_stage == SSD_ASYNC_PLANE24 || async_stage == SSD_ASYNC_PLANE26) {
            const uint8_t *pixels = partial_update && async_stage == SSD_ASYNC_PLANE26 ? previous_frame : frame;
            for (unsigned n = 0; n < 8u && async_offset < w * h && work < 512u; ++n) {
                const size_t i = (size_t)(async_offset / w + y) * (X4PRO_PANEL_WIDTH / 8u) + x + async_offset % w;
                spi_byte((uint8_t)~pixels[i]); ++async_offset; ++bytes_sent; ++work;
            }
            if (async_offset == w * h) {
                panel_pin_level(X4PRO_PIN_EPD_CS, true);
                if (async_stage == SSD_ASYNC_PLANE24) {
                    async_stage = SSD_ASYNC_OPEN26; async_offset = 0;
                } else {
                    if (!sample_metric(&transfer_end_ms, RISC_DISPLAY_METRICS_TRANSFER_END)) goto failed;
                    async_stage = SSD_ASYNC_SETUP21;
                }
            }
        } else if (async_stage == SSD_ASYNC_OPEN26) {
            command(0x26); panel_pin_level(X4PRO_PIN_EPD_DC, true);
            panel_pin_level(X4PRO_PIN_EPD_CS, false); async_stage = SSD_ASYNC_PLANE26;
        } else if (async_stage == SSD_ASYNC_SETUP21) {
            busy_before = panel_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
            if (io_failed) goto failed;
            if (busy_before) { set_reason("busy already active"); goto failed; }
            command(0x21); data1(partial_update ? 0x00 : 0x40); async_stage = SSD_ASYNC_SETUP3C;
        } else if (async_stage == SSD_ASYNC_SETUP3C) {
            command(0x3C); data1(partial_update ? 0x80 : 0xC0); async_stage = SSD_ASYNC_SETUP22;
        } else if (async_stage == SSD_ASYNC_SETUP22) {
            command(0x22); data1(partial_update ? 0xFC : 0xF7); async_stage = SSD_ASYNC_REFRESH;
        } else if (async_stage == SSD_ASYNC_REFRESH) {
            if (!sample_metric(&refresh_ms, RISC_DISPLAY_METRICS_REFRESH)) goto failed;
            metrics.refresh_ms = refresh_ms; command(0x20); async_stage = SSD_ASYNC_ASSERT;
        } else if (async_stage == SSD_ASYNC_ASSERT) {
            const bool busy = panel_pin_read(X4PRO_PIN_EPD_BUSY);
            if (io_failed) goto failed;
            if (!busy) break;
            if (!sample_metric(&busy_assert_ms, RISC_DISPLAY_METRICS_BUSY_ASSERT)) goto failed;
            async_stage = SSD_ASYNC_DONE;
        } else if (async_stage == SSD_ASYNC_DONE) {
            const bool busy = panel_pin_read(X4PRO_PIN_EPD_BUSY);
            if (io_failed) goto failed;
            if (busy) break;
            if (!sample_metric(&busy_done_ms, RISC_DISPLAY_METRICS_BUSY_DONE) || busy_done_ms >= async_deadline) goto failed;
            present_state = PRESENT_COMPLETE; held = false; previous_seeded = false;
            async_stage = UC_ASYNC_NONE; reason = "complete"; break;
        } else { set_reason("invalid async state"); goto failed; }
        if (io_failed) goto failed;
        if (!sample_now(&now)) goto failed;
    } while (now < slice_end && work < 512u);
    if (io_failed) goto failed;
    (void)leave(); return;
failed:
    panel_pin_level(X4PRO_PIN_EPD_CS, true);
    present_state = PRESENT_FAILED; held = false; async_stage = UC_ASYNC_NONE;
    (void)leave();
}

/* Runtime calls this ordinary poll suffix on the existing serialized owner.
 * Keep every GPIO edge/command from the synchronous UC path, but return between
 * bounded chunks so app input can be sampled while a frame is in flight. */
static void poll_present(uint32_t budget_ms) {
    if (controller == PROBE_SSD) { ssd_poll_present(budget_ms); return; }
    if (!budget_ms || controller != PROBE_UC8279 || !started || shutdown_stage ||
        (present_state != PRESENT_QUEUED && present_state != PRESENT_ACTIVE) || !enter()) return;
    uint64_t now = 0;
    if (!sample_now(&now)) goto failed;
    if (present_state == PRESENT_QUEUED) {
        if (now > UINT64_MAX - 10000u) { set_reason("clock overflow"); goto failed; }
        wait_start_ms = transfer_start_ms = now; wait_budget_ms = 10000u;
        metrics.valid_times |= RISC_DISPLAY_METRICS_TRANSFER_START;
        transfer_end_ms = refresh_ms = busy_assert_ms = busy_done_ms = 0;
        bytes_sent = 0; reason = "none"; transfer_started = true;
        async_stage = UC_ASYNC_PRE; async_offset = 0; async_deadline = now + 10000u;
        present_state = PRESENT_ACTIVE;
    }
    if (now >= async_deadline) { set_reason("async present deadline"); goto failed; }
    const uint64_t slice_end = now + (budget_ms > 8u ? 8u : budget_ms);
    unsigned work = 0;
    do {
        if (!sample_now(&now)) goto failed;
        if (now >= async_deadline) { set_reason("async present deadline"); goto failed; }
        if (async_stage == UC_ASYNC_PRE) {
            if (!panel_pin_read(X4PRO_PIN_EPD_BUSY)) break;
            command(0x13); panel_pin_level(X4PRO_PIN_EPD_DC, true);
            panel_pin_level(X4PRO_PIN_EPD_CS, false); async_stage = UC_ASYNC_PLANE13;
        } else if (async_stage == UC_ASYNC_PLANE13 || async_stage == UC_ASYNC_PLANE10) {
            /* Check time every eight bytes, with an independent 512-byte cap
             * even when the monotonic clock has coarse resolution. */
            for (unsigned n = 0; n < 8u && async_offset < 60000u; ++n) {
                const bool white = async_stage == UC_ASYNC_PLANE10;
                const uint8_t *pixels = white && partial_update ? previous_frame : frame;
                const uint8_t value = ((white && !partial_update) || async_offset < 12000u) ?
                    0xFFu : (uint8_t)~pixels[async_offset - 12000u];
                spi_byte(value); ++async_offset; ++bytes_sent; ++work;
            }
            if (async_offset == 60000u) {
                panel_pin_level(X4PRO_PIN_EPD_CS, true);
                if (async_stage == UC_ASYNC_PLANE13) {
                    command(0x10); panel_pin_level(X4PRO_PIN_EPD_DC, true);
                    panel_pin_level(X4PRO_PIN_EPD_CS, false);
                    async_stage = UC_ASYNC_PLANE10; async_offset = 0;
                } else {
                    if (!sample_metric(&transfer_end_ms, RISC_DISPLAY_METRICS_TRANSFER_END)) goto failed;
                    async_stage = UC_ASYNC_SETUP;
                }
            }
        } else if (async_stage == UC_ASYNC_SETUP) {
            command(0x50); data1(partial_update ? 0xD7 : 0x97);
            command(0xE0); data1(0x02); command(0xE5); data1(partial_update ? 0x5A : 0x1E);
            if (partial_update) { command(0x03); data1(0x20); command(0xE1); data1(0x02); }
            if (!sample_now(&refresh_ms)) goto failed;
            command(0x04); async_not_before = refresh_ms + 1u; async_stage = UC_ASYNC_PON;
            break;
        } else if (async_stage == UC_ASYNC_PON) {
            if (now < async_not_before || !panel_pin_read(X4PRO_PIN_EPD_BUSY)) break;
            async_stage = UC_ASYNC_REFRESH;
        } else if (async_stage == UC_ASYNC_REFRESH) {
            if (partial_update) {
                const uint16_t right = (uint16_t)(update_area.x + update_area.width - 1u);
                const uint16_t top = (uint16_t)update_area.y + 120u;
                const uint16_t bottom = top + (uint16_t)update_area.height - 1u;
                command(0x91); command(0x90);
                data1((uint16_t)update_area.x >> 8); data1((uint16_t)update_area.x & 0xF8u);
                data1(right >> 8); data1(right | 7u); data1(top >> 8); data1(top & 255u);
                data1(bottom >> 8); data1(bottom & 255u); data1(0x01);
            }
            command(0x00); data1(0x17); data1(0x4D);
            busy_before = panel_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
            if (!busy_before) { set_reason("busy already active"); goto failed; }
            if (!sample_metric(&metrics.refresh_ms, RISC_DISPLAY_METRICS_REFRESH)) goto failed;
            command(0x12); async_stage = UC_ASYNC_ASSERT;
        } else if (async_stage == UC_ASYNC_ASSERT) {
            if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) break;
            if (!sample_metric(&busy_assert_ms, RISC_DISPLAY_METRICS_BUSY_ASSERT)) goto failed;
            async_stage = UC_ASYNC_DONE;
        } else if (async_stage == UC_ASYNC_DONE) {
            if (!panel_pin_read(X4PRO_PIN_EPD_BUSY)) break;
            if (!sample_metric(&busy_done_ms, RISC_DISPLAY_METRICS_BUSY_DONE)) goto failed;
            if (partial_update) command(0x92);
            if (io_failed) goto failed;
            remember_completed_frame();
            present_state = PRESENT_COMPLETE; held = false; previous_seeded = false;
            async_stage = UC_ASYNC_NONE; reason = "complete"; break;
        } else { set_reason("invalid async state"); goto failed; }
        if (io_failed) goto failed;
        if (!sample_now(&now)) goto failed;
    } while (now < slice_end && work < 512u);
    if (io_failed) goto failed;
    (void)leave(); return;
failed:
    panel_pin_level(X4PRO_PIN_EPD_CS, true);
    present_state = PRESENT_FAILED; held = false; async_stage = UC_ASYNC_NONE;
    (void)leave();
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
    if (frontlight && frontlight->set_level) out->flags |= RISC_DISPLAY_INFO_BRIGHTNESS;
    if (controller == PROBE_UC8279 || controller == PROBE_SSD) out->flags |= RISC_DISPLAY_INFO_ASYNC_PRESENT;
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
    partial_update = count && (previous_seeded || completed_history) && (!options || options->intent != RISC_DISPLAY_PRESENT_CLEAN);
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
    memset(&metrics, 0, sizeof(metrics));
    metrics.token = pending_token;
    metrics.mode = partial_update ? RISC_DISPLAY_METRICS_PARTIAL : RISC_DISPLAY_METRICS_FULL;
    metrics.damage_count = (uint32_t)count;
    for (size_t i = 0; i < count; ++i) metrics.submitted_damage[i] = damage[i];
    metrics.effective_update = partial_update ? update_area :
        (risc_display_rect_v1){0, 0, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT};
    const uint64_t queued = now_ms();
    if (queued != UINT64_MAX && queued >= last_sample_ms) {
        metrics.queued_ms = queued; metrics.valid_times |= RISC_DISPLAY_METRICS_QUEUED;
    }
    bytes_sent = 0;
    transfer_start_ms = transfer_end_ms = refresh_ms = busy_assert_ms = busy_done_ms = 0;
    previous_seeded = completed_history = false; // Invalid until this submission completes.
    present_state = PRESENT_QUEUED;
    transfer_started = false;
    async_stage = UC_ASYNC_NONE;
    if (token_out) *token_out = pending_token;
    return true;
}
static bool present_status_impl(void *context, risc_display_present_token_v1 token, risc_display_present_status_v1 *out) {
    (void)context;
    if (shutdown_stage || !out || !token || token != pending_token) return false;
    *out = (risc_display_present_status_v1){0};
    out->state = present_state;
    return true;
}
static bool wait_present_impl(void *context, risc_display_present_token_v1 token, uint32_t timeout_ms,
                         risc_display_present_status_v1 *out) {
    if (shutdown_stage || !out || !token || token != pending_token) return false;
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
            remember_completed_frame();
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
    previous_seeded = completed_history = false;
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
    if (shutdown_stage || !out || !token || token != pending_token) { (void)leave(); return false; }
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
static int32_t power_prepare(void *context, uint32_t timeout_ms);
static int32_t power_resume(void *context, uint32_t timeout_ms);
static bool present_metrics(void *context, risc_display_present_metrics_v1 *out) {
    (void)context;
    if (!out || out->api_version != 1u || out->struct_size < sizeof(*out) ||
        !started || shutdown_stage || retained || !sync_api || !mutex ||
        !sync_api->is_owner(sync_api->context)) return false;
    risc_display_present_metrics_v1 copy = metrics;
    copy.api_version = 1u; copy.struct_size = sizeof(copy);
    copy.state = present_state; copy.bytes_sent = bytes_sent;
    copy.transfer_start_ms = transfer_start_ms; copy.transfer_end_ms = transfer_end_ms;
    copy.busy_assert_ms = busy_assert_ms; copy.busy_done_ms = busy_done_ms;
    *out = copy; return true;
}
static const risc_display_output_api_v1_metrics api = {
    {{{ RISC_DISPLAY_OUTPUT_API_V1, sizeof(api), 0, get_info, acquire, release, submit,
       present_status, wait_present, set_brightness },
     RISC_DISPLAY_HISTORY_TAG, 1u, seed_previous},
    RISC_DISPLAY_POWER_TAG, 1u, power_prepare, power_resume},
    RISC_DISPLAY_METRICS_TAG, RISC_DISPLAY_METRICS_VERSION, present_metrics
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
    last_error_text[0]=0;
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
    shutdown_stage = 0; previous_seeded = completed_history = partial_update = false;
    present_state = PRESENT_NONE; pending_token = 0; last_sample_ms = now;
    memset(&metrics, 0, sizeof(metrics));
    bytes_sent = 0; transfer_start_ms = transfer_end_ms = refresh_ms = busy_assert_ms = busy_done_ms = 0;
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
/* Each operation has a total owner-admission deadline, at most 1500 ms and
 * 150 ten-ms readiness polls. Single commands are finite (at most 6 bytes),
 * completed before deadline sampling so retries never replay partial POF/DSLP.
 * No pixels are sent and no display refresh is triggered by this lifecycle. */
static int32_t power_checkpoint(uint64_t deadline) {
    uint64_t now = 0;
    if (retained || io_failed) return RISC_DISPLAY_POWER_RETAINED;
    if (!sample_now(&now)) return RISC_DISPLAY_POWER_PLATFORM;
    if (now >= deadline) { set_reason("panel power deadline"); return RISC_DISPLAY_POWER_TIMEOUT; }
    return RISC_DISPLAY_POWER_OK;
}
static int32_t power_delay(uint64_t deadline, uint32_t ms) {
    int32_t result = power_checkpoint(deadline);
    if (result) return result;
    const uint64_t remaining = deadline - last_sample_ms;
    if (remaining <= ms) {
        sleep_ms((uint32_t)remaining);
        set_reason("panel power deadline");
        return retained ? RISC_DISPLAY_POWER_RETAINED : RISC_DISPLAY_POWER_TIMEOUT;
    }
    sleep_ms(ms);
    return power_checkpoint(deadline);
}
static int32_t power_ready(uint64_t deadline) {
    for (unsigned checks = 0; checks < 150u; ++checks) {
        int32_t result = power_checkpoint(deadline);
        if (result) return result;
        const bool busy = controller == PROBE_UC8279 ? !panel_pin_read(X4PRO_PIN_EPD_BUSY)
                                                   : panel_pin_read(X4PRO_PIN_EPD_BUSY);
        if (io_failed) return RISC_DISPLAY_POWER_RETAINED;
        if (!busy && last_sample_ms >= shutdown_not_before) return power_checkpoint(deadline);
        result = power_delay(deadline, 10u);
        if (result) return result;
    }
    set_reason("panel power readiness bound");
    return RISC_DISPLAY_POWER_TIMEOUT;
}
static int32_t prepare_power_impl(uint64_t deadline) {
    if (held || present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE || shutdown_stage == 5u)
        return RISC_DISPLAY_POWER_BUSY;
    if (!pins_ready || (!started && !shutdown_stage) || shutdown_stage == 4u ||
        (controller != PROBE_SSD && controller != PROBE_UC8279)) return RISC_DISPLAY_POWER_UNAVAILABLE;
    if (shutdown_stage == 3u && reset_held) return RISC_DISPLAY_POWER_OK;
    int32_t result = power_checkpoint(deadline);
    if (result) return result;
    if (shutdown_stage == 0u) {
        const bool busy = controller == PROBE_UC8279 ? !panel_pin_read(X4PRO_PIN_EPD_BUSY)
                                                   : panel_pin_read(X4PRO_PIN_EPD_BUSY);
        if (io_failed) return RISC_DISPLAY_POWER_RETAINED;
        if (busy) return RISC_DISPLAY_POWER_BUSY;
        if (controller == PROBE_UC8279) command(0x02);
        else { command(0x3C); data1(0x80); command(0x22); data1(0x03); command(0x20); }
        if (io_failed) return RISC_DISPLAY_POWER_RETAINED;
        shutdown_stage = 1u; started = false; previous_seeded = completed_history = false;
        /* Start settling after the completed command, not before its GPIO I/O. */
        result = power_checkpoint(deadline);
        shutdown_not_before = last_sample_ms + (controller == PROBE_UC8279 ? 1u : 200u);
        if (result) return result;
    }
    if (shutdown_stage == 1u) {
        result = power_ready(deadline);
        if (result) return result;
        shutdown_stage = 2u;
    }
    if (shutdown_stage == 2u) {
        result = power_checkpoint(deadline);
        if (result) return result;
        if (controller == PROBE_UC8279) { command(0x07); data1(0xA5); }
        else { command(0x10); data1(0x03); }
        if (io_failed) return RISC_DISPLAY_POWER_RETAINED;
        shutdown_stage = 3u;
    }
    result = power_checkpoint(deadline);
    if (result) return result;
    if (!reset_held) {
        panel_pin_output(X4PRO_PIN_EPD_RST, true);
        if (io_failed) return RISC_DISPLAY_POWER_RETAINED;
        const int32_t held_result = gpio->deep_sleep_hold(gpio->context, pin_tokens[X4PRO_PIN_EPD_RST], true);
        if (held_result) {
            if (held_result == RISC_DEEP_SLEEP_RETAINED) retained = true;
            set_reason("reset hold failed");
            return retained ? RISC_DISPLAY_POWER_RETAINED : RISC_DISPLAY_POWER_PLATFORM;
        }
        reset_held = true;
    }
    return power_checkpoint(deadline);
}
static int32_t power_command(uint64_t deadline, uint8_t cmd, const uint8_t *data, size_t count) {
    int32_t result = power_checkpoint(deadline);
    if (result) return result;
    command(cmd);
    for (size_t i = 0; i < count; ++i) data1(data[i]);
    return power_checkpoint(deadline);
}
/* All uses below are compile-time bounded to five data bytes per command. */
#define RESUME_SEND(cmd, ...) do { \
    const uint8_t data[] = {__VA_ARGS__}; \
    result = power_command(deadline, cmd, data, sizeof(data)); \
    if (result) return result; \
} while (0)
static int32_t resume_power_impl(uint64_t deadline) {
    if (!pins_ready || shutdown_stage == 4u) return RISC_DISPLAY_POWER_UNAVAILABLE;
    if (!shutdown_stage) return started ? RISC_DISPLAY_POWER_OK : RISC_DISPLAY_POWER_UNAVAILABLE;
    int32_t result = power_checkpoint(deadline);
    if (result) return result;
    if (reset_held) {
        /* A failed disable is uncertain even if a platform returns a weaker
         * error than RETAINED. Never write/release a possibly held output. */
        if (gpio->deep_sleep_hold(gpio->context, pin_tokens[X4PRO_PIN_EPD_RST], false)) {
            retained = true; set_reason("reset unhold retained"); return RISC_DISPLAY_POWER_RETAINED;
        }
        reset_held = false;
    }
    shutdown_stage = 5u; started = false; previous_seeded = completed_history = false;
    shutdown_not_before = 0;
    /* The same controller register setup as initial start, but without probe,
     * frame clear, PON or refresh. RESET recovers partial POF/DSLP/refusals. */
    panel_pin_output(X4PRO_PIN_EPD_RST, false);
    result = power_delay(deadline, controller == PROBE_UC8279 ? 50u : 10u);
    if (result) return result;
    panel_pin_output(X4PRO_PIN_EPD_RST, true);
    result = power_delay(deadline, controller == PROBE_UC8279 ? 50u : 10u);
    if (result) return result;
    if (controller == PROBE_UC8279) {
        result = power_ready(deadline); if (result) return result;
        RESUME_SEND(0x00, 0x37, 0x4D);
        RESUME_SEND(0x61, 0x03, 0x20, 0x02, 0x58);
        RESUME_SEND(0x65, 0, 0, 0, 0);
        RESUME_SEND(0x03, 0x20); RESUME_SEND(0x30, 0x0E); RESUME_SEND(0xE1, 0x02);
    } else {
        result = power_command(deadline, 0x12, NULL, 0); if (result) return result;
        result = power_delay(deadline, 10u); if (result) return result;
        result = power_ready(deadline); if (result) return result;
        RESUME_SEND(0x18, 0x80);
        RESUME_SEND(0x0C, 0xAE, 0xC7, 0xC3, 0xC0, 0x80);
        RESUME_SEND(0x01, 0xDF, 0x01, 0x02);
        RESUME_SEND(0x3C, 0x80); RESUME_SEND(0x11, 0x01);
        RESUME_SEND(0x44, 0x00, 0x00, 0x1F, 0x03);
        RESUME_SEND(0x45, 0xDF, 0x01, 0x00, 0x00);
        result = power_ready(deadline); if (result) return result;
        RESUME_SEND(0x4E, 0, 0); RESUME_SEND(0x4F, 0xDF, 0x01);
        RESUME_SEND(0x46, 0xF7);
        result = power_ready(deadline); if (result) return result;
        RESUME_SEND(0x47, 0xF7);
    }
    result = power_ready(deadline);
    if (result) return result;
    started = true; shutdown_stage = 0; pending_token = 0; present_state = PRESENT_NONE;
    memset(&metrics, 0, sizeof(metrics));
    bytes_sent = 0; transfer_start_ms = transfer_end_ms = refresh_ms = busy_assert_ms = busy_done_ms = 0;
    partial_update = false; async_stage = UC_ASYNC_NONE; transfer_started = false;
    return RISC_DISPLAY_POWER_OK;
}
#undef RESUME_SEND
static int32_t power_call(bool resume, uint32_t timeout_ms) {
    if (!sync_api || !clock_api || !mutex || !sync_api->is_owner(sync_api->context))
        return RISC_DISPLAY_POWER_UNAVAILABLE;
    if (retained) return RISC_DISPLAY_POWER_RETAINED;
    const uint64_t began = now_ms();
    if (began == UINT64_MAX || began < last_sample_ms) return RISC_DISPLAY_POWER_PLATFORM;
    const uint32_t budget = timeout_ms > RISC_DISPLAY_POWER_MAX_BUDGET_MS ? RISC_DISPLAY_POWER_MAX_BUDGET_MS : timeout_ms;
    if (began > UINT64_MAX - RISC_DISPLAY_POWER_MAX_BUDGET_MS) return RISC_DISPLAY_POWER_PLATFORM;
    if (!enter()) return RISC_DISPLAY_POWER_BUSY;
    int32_t result;
    if (!timeout_ms) {
        result = (resume ? started && !shutdown_stage : shutdown_stage == 3u && reset_held) ?
            RISC_DISPLAY_POWER_OK : RISC_DISPLAY_POWER_BUSY;
    } else {
        result = power_checkpoint(began + budget);
        if (!result) result = resume ? resume_power_impl(began + budget) : prepare_power_impl(began + budget);
    }
    return leave() ? result : RISC_DISPLAY_POWER_RETAINED;
}
static int32_t power_prepare(void *context, uint32_t timeout_ms) {
    (void)context; return power_call(false, timeout_ms);
}
static int32_t power_resume(void *context, uint32_t timeout_ms) {
    (void)context; return power_call(true, timeout_ms);
}
static bool quiesce_impl(void) {
    if (shutdown_stage == 4u) return true;
    if (held || present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return false;
    if (!pins_ready) return true;
    const uint64_t began = now_ms();
    if (began == UINT64_MAX || began < last_sample_ms || began > UINT64_MAX - RISC_DISPLAY_POWER_MAX_BUDGET_MS) return false;
    if (prepare_power_impl(began + RISC_DISPLAY_POWER_MAX_BUDGET_MS)) return false;
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
    append(destination, capacity, &used, "v=0.1.23 cause=");
    append(destination, capacity, &used, last_error_text[0]?last_error_text:reason);
    append(destination, capacity, &used, " ");
    append(destination, capacity, &used, probe_text);
    append(destination, capacity, &used, " token=");
    append_u(destination, capacity, &used, pending_token);
    append(destination, capacity, &used, " state=");
    append_u(destination, capacity, &used, present_state);
    append(destination, capacity, &used, " power=");
    append_u(destination, capacity, &used, shutdown_stage);
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
static const risc_driver_poll_v2 driver = {
    {{ RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_poll_v2), "x4pro-panel",
      "display.output", 1, &api, start, stop, quiesce },
    last_error, NULL}, poll_present
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver.streams.driver;
}
