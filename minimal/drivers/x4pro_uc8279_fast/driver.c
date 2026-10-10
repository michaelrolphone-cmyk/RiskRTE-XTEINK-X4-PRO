/* UC8279 ZHX native-SPI absolute-A2 provider. The separate x4pro-panel
 * provider remains the fallback. Protocol source: X4 lab 0.1.5 at
 * 05d811ae3a75b0711540484ccdbee32464042dd6, 20 MHz modes 4/6/7/9. */
#include "RiscDisplayOutputV1.h"
#include "RiscDisplayOutputPowerV1.h"
#include "../../interfaces/RiscDisplayOutputFrontlightV1.h"
#include "../../interfaces/RiscDisplayOutputSettledV1.h"
#include "RiscPlatformClockV1.h"
#include <GardenPlatformV1.h>
#include <RiscProviderSyncV1.h>
#include "../../interfaces/RiscFrontlightToneV1.h"
#include "../x4pro_board_power/PowerReadyV1.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define X4PRO_PANEL_WIDTH 800u
#define X4PRO_PANEL_HEIGHT 480u
#define X4PRO_FINAL_TARGET_FRAMES 4u
#define X4PRO_ACTIVE_LOCAL_FRAMES 3u
#define X4PRO_ACTIVE_BROAD_FRAMES 2u
#define X4PRO_ACTIVE_LOCAL_MAX_ROWS 160u
/* Role indices are local; physical pads come only from the typed device. */
enum { X4PRO_PIN_EPD_BUSY, X4PRO_PIN_EPD_DC, X4PRO_PIN_EPD_RST, PANEL_PINS };
static const garden_gpio_v1 *gpio;
static const garden_spi_v1 *spi;
static uint64_t spi_token;
static bool spi_held, screen_powered, fast_update, absolute_update, settle_update,
            quality_partial, presentation_fault;
static uint32_t spi_hz = 100000u;
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
static bool previous_seeded, completed_history, partial_update, dtm1_synced, sync_full;
static risc_display_rect_v1 update_area;
static bool transfer_started;
enum { UC_ASYNC_NONE, UC_ASYNC_PRE, UC_ASYNC_PREPARE, UC_ASYNC_WHITE, UC_ASYNC_NEW, UC_ASYNC_OLD,
       UC_ASYNC_SETUP, UC_ASYNC_PON_ASSERT, UC_ASYNC_PON_DONE,
       UC_ASYNC_QUALITY_IN, UC_ASYNC_QUALITY_WINDOW, UC_ASYNC_OTP, UC_ASYNC_REFRESH, UC_ASYNC_ASSERT, UC_ASYNC_DONE, UC_ASYNC_SYNC };
static uint64_t phase_deadline;
static uint8_t async_stage, setup_step, fast_lut_frames;
static uint32_t async_offset, absolute_frames;
static uint64_t absolute_started_ms;
/* The aggregate budget charges provider execution, not unrelated foreground
 * work between polls. BUSY deadlines below remain absolute wall-clock limits. */
static uint64_t async_deadline, async_last_poll_ms;
/* Settling owns only the resident controller image, never a caller's frame.
 * A new submission can queue while its last BUSY pulse is being drained. */
enum { SETTLE_NONE, SETTLE_WAIT, SETTLE_READY, SETTLE_WINDOW, SETTLE_REFRESH,
       SETTLE_ASSERT, SETTLE_DONE, SETTLE_CLOSE, SETTLE_FINAL_NEW,
       SETTLE_FINAL_SETUP, SETTLE_FINAL_REFRESH, SETTLE_FINAL_ASSERT,
       SETTLE_FINAL_DONE, SETTLE_FINAL_CLOSE, SETTLE_SYNC_OLD,
       SETTLE_SYNC_NEW };
static uint8_t settle_stage;
static bool settle_stop, settle_coverage_valid;
static risc_display_rect_v1 settle_area;
static uint64_t settle_until, settle_phase_deadline, settle_refresh_ms;
static uint32_t settle_refreshes, settle_completed, settle_sync_offset;
static uint8_t settle_setup_step;
static uint64_t settle_power_ms;
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
static void spi_failed(const char *text) { io_failed = retained = true; set_reason(text); }
/* An admitted three-wire begin may fail after touching hardware. end is then
 * mandatory and failure retains its token and the mapped provider. */
static bool end_spi(void) {
    if (!spi_held) return true;
    if (!spi->end(spi->context, spi_token)) { spi_failed("spi end retained"); return false; }
    spi_held = false; return true;
}
static bool begin_spi(uint32_t budget_ms) {
    if (io_failed || spi_held || !spi_token) return false;
    spi_held = true;
    if (!spi->begin(spi->context, spi_token, spi_hz, 0, budget_ms)) {
        spi_failed("spi begin retained"); (void)end_spi(); return false;
    }
    return true;
}
static bool exchange_spi(const uint8_t *tx, uint8_t *rx, size_t length) {
    if (io_failed || !spi_held) return false;
    if (!spi->exchange(spi->context, spi_token, tx, rx, length)) {
        spi_failed("spi exchange retained"); (void)end_spi(); return false;
    }
    return true;
}
static void write_bytes(bool data, const uint8_t *bytes, size_t length) {
    panel_pin_level(X4PRO_PIN_EPD_DC, data);
    if (io_failed || !begin_spi(8u)) return;
    (void)exchange_spi(bytes, NULL, length); (void)end_spi();
}
static void command(uint8_t cmd) { write_bytes(false, &cmd, 1u); }
static void data1(uint8_t value) { write_bytes(true, &value, 1u); }
/* Command/data share one bounded transaction; DC changes while CS is held. */
static void write_register(uint8_t cmd, const uint8_t *bytes, size_t length) {
    panel_pin_level(X4PRO_PIN_EPD_DC, false);
    if (io_failed || !begin_spi(8u)) return;
    if (exchange_spi(&cmd, NULL, 1u)) {
        panel_pin_level(X4PRO_PIN_EPD_DC, true);
        (void)exchange_spi(bytes, NULL, length);
    }
    (void)end_spi();
}
static void reg1(uint8_t cmd, uint8_t value) { write_register(cmd, &value, 1u); }
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
static char probe_text[96] = "probe=not-run";
static bool append(char *destination, size_t capacity, size_t *used, const char *text);
static bool append_u(char *destination, size_t capacity, size_t *used, uint64_t value);
static void prepare_pins(void);
static void read_cmd(uint8_t cmd, uint8_t *out, size_t len) {
    panel_pin_level(X4PRO_PIN_EPD_DC, false);
    if (io_failed || !begin_spi(8u)) return;
    if (exchange_spi(&cmd, NULL, 1u)) {
        panel_pin_level(X4PRO_PIN_EPD_DC, true);
        (void)exchange_spi(NULL, out, len);
    }
    (void)end_spi();
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
    uint8_t flag = 0, confirm_flag = 0, version[5] = {0}, confirm[5] = {0};
    panel_pin_output(X4PRO_PIN_EPD_RST, false); sleep_ms(50);
    panel_pin_output(X4PRO_PIN_EPD_RST, true); sleep_ms(50);
    uint64_t now = 0;
    for (unsigned n = 0; n < 100u; ++n) {
        if (!sample_now(&now) || now >= operation_deadline) return PROBE_AMBIGUOUS;
        if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) break;
        sleep_ms(10);
    }
    if (io_failed || !panel_pin_read(X4PRO_PIN_EPD_BUSY)) return PROBE_AMBIGUOUS;
    read_cmd(0x71, &flag, 1); read_cmd(0x70, version, sizeof(version));
    sleep_ms(20);
    read_cmd(0x71, &confirm_flag, 1); read_cmd(0x70, confirm, sizeof(confirm));
    bool floating = true, matches = flag == confirm_flag;
    for (size_t i = 0; i < sizeof(version); ++i) {
        matches = matches && version[i] == confirm[i];
        floating = floating && version[i] == version[0];
    }
    size_t used = 0; probe_text[0] = 0;
    append(probe_text, sizeof(probe_text), &used, "probe=flg:");
    append_hex(probe_text, sizeof(probe_text), &used, flag);
    append(probe_text, sizeof(probe_text), &used, " ver:");
    for (size_t i = 0; i < sizeof(version); ++i) append_hex(probe_text, sizeof(probe_text), &used, version[i]);
    return !io_failed && matches && !floating && flag != 0xFF && (flag & 1u) &&
        (version[2] == 0x68 || version[2] == 0x69) && panel_pin_read(X4PRO_PIN_EPD_BUSY) ?
        PROBE_UC8279 : PROBE_AMBIGUOUS;
}
static void prepare_pins(void) {
    if (pins_ready) return;
    panel_pin_input(X4PRO_PIN_EPD_BUSY, false);
    panel_pin_output(X4PRO_PIN_EPD_DC, false);
    panel_pin_output(X4PRO_PIN_EPD_RST, true);
    panel_epd_reset_unhold();
    pins_ready = true;
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
/* A RAM plane can span owner polls, but an SPI transaction cannot. CS is
 * released before returning to foreground work; the controller's RAM cursor
 * continues on the next data transaction without reissuing the RAM command.
 * Exchanges stay <=512 bytes and each poll copies at most 16384 bytes. */
static void window_for(const risc_display_rect_v1 *area) {
    const uint16_t top = (uint16_t)area->y + 120u;
    const uint16_t bottom = top + area->height - 1u;
    const uint16_t left = (uint16_t)area->x, right = left + area->width - 1u;
    const uint8_t window[] = {(uint8_t)(left >> 8), (uint8_t)(left & 0xF8u),
        (uint8_t)(right >> 8), (uint8_t)(right | 7u), (uint8_t)(top >> 8), (uint8_t)top,
        (uint8_t)(bottom >> 8), (uint8_t)bottom, 1};
    write_register(0x90, window, sizeof(window));
}
static void window_data(void) { window_for(&update_area); }
static risc_display_rect_v1 tested_window(uint32_t top, uint32_t bottom) {
    const uint32_t span = bottom - top;
    const uint32_t height = span <= 40u ? 40u : (span <= 80u ? 80u : (span <= 160u ? 160u : 480u));
    if (top > 480u - height) top = 480u - height;
    return (risc_display_rect_v1){0, (int32_t)top, X4PRO_PANEL_WIDTH, height};
}
static void write_a2_lut_table(unsigned i, uint8_t frames, bool absolute) {
    uint8_t table[42];
    if (!frames) frames = 1u;
    memset(table, 0, sizeof(table)); table[0] = table[5] = table[6] = 1u;
    /* X4 wire {OLD,NEW}:00->24,01->22,10->23,11->21. Absolute
     * refresh follows NEW only. Differential refresh leaves 00/11 idle and
     * drives only black->white and white->black transitions. */
    if (i == 0u) table[1] = frames;
    else if (absolute) table[1] = (uint8_t)((i <= 2u ? 0x80u : 0x40u) | frames);
    else if (i == 2u) table[1] = (uint8_t)(0x80u | frames);
    else if (i == 3u) table[1] = (uint8_t)(0x40u | frames);
    else table[1] = frames;
    write_register((uint8_t)(0x20u + i), table, sizeof(table));
}
static void absolute_lut_table(unsigned i) {
    write_a2_lut_table(i, fast_lut_frames ? fast_lut_frames : 1u, absolute_update);
}
static bool begin_plane(uint8_t cmd) {
    command(cmd); panel_pin_level(X4PRO_PIN_EPD_DC, true); async_offset = 0;
    return !io_failed;
}
static bool settle_begin_plane(uint8_t cmd) {
    command(cmd); panel_pin_level(X4PRO_PIN_EPD_DC, true); settle_sync_offset = 0;
    return !io_failed;
}
static bool settle_begin_final_target(void) {
    /* The final target is already in previous_frame. Upload it across the
     * complete 800x600 controller RAM before the full-visible endpoint pulse;
     * hidden rows remain explicitly white. No blank or inverse image is ever
     * presented to the glass. */
    if (!screen_powered || !settle_begin_plane(0x13)) return false;
    settle_stage = SETTLE_FINAL_NEW;
    return true;
}
static bool settle_sync_chunk(uint32_t *work) {
    const uint32_t total = 60000u;
    while (settle_sync_offset < total && *work < 16384u) {
        uint32_t count = total - settle_sync_offset;
        if (count > 512u) count = 512u;
        if (count > 16384u - *work) count = 16384u - *work;
        uint8_t buffer[512];
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t offset = settle_sync_offset + i;
            buffer[i] = offset < 12000u ? 0xFFu :
                (uint8_t)~previous_frame[offset - 12000u];
        }
        if (!begin_spi(1000u)) return false;
        if (!exchange_spi(buffer, NULL, count)) return false;
        if (!end_spi()) return false;
        settle_sync_offset += count; *work += count;
    }
    return true;
}
static bool damaged_byte(uint32_t index) {
    if (!metrics.damage_count) return true;
    const uint32_t x = (index % 100u) * 8u, y = index / 100u;
    for (uint32_t i = 0; i < metrics.damage_count; ++i) {
        const risc_display_rect_v1 *r = &metrics.submitted_damage[i];
        if (y >= (uint32_t)r->y && y < (uint32_t)r->y + r->height &&
            x + 8u > (uint32_t)r->x && x < (uint32_t)r->x + r->width) return true;
    }
    return false;
}
static uint8_t frame_byte(uint32_t offset) {
    if (!fast_update && offset < 12000u) return 0xFFu;
    const uint32_t index = fast_update ? (uint32_t)update_area.y * 100u + offset : offset - 12000u;
    if (!fast_update && !quality_partial) return (uint8_t)~frame[index];
    /* Full-width, expanded-height RAM bands preserve all bytes outside the
     * union of submitted damage, including gaps between separate rectangles. */
    return (uint8_t)~(damaged_byte(index) ? frame[index] : previous_frame[index]);
}
static void remember_completed_frame(void) {
    if (fast_update || quality_partial) {
        const uint32_t first = (uint32_t)update_area.y * 100u;
        const uint32_t last = first + update_area.height * 100u;
        for (uint32_t i = first; i < last; ++i) if (damaged_byte(i)) previous_frame[i] = frame[i];
    } else memcpy(previous_frame, frame, FRAME_BYTES);
    completed_history = true;
}
static void present_failed(void) {
    (void)end_spi(); completed_history = previous_seeded = false;
    /* A missed/uncertain waveform can leave PTIN or active panel power.
     * Require restart; never publish a new baseline into unknown RAM mode. */
    presentation_fault = true;
    present_state = PRESENT_FAILED; held = false; async_stage = UC_ASYNC_NONE;
    settle_stage = SETTLE_NONE;
    settle_coverage_valid = false; dtm1_synced = sync_full = false;
    absolute_frames = 0; absolute_started_ms = UINT64_MAX; screen_powered = false;
}
static bool arm_settle(void) {
    if (busy_done_ms > UINT64_MAX - 2300u) { set_reason("settle clock overflow"); return false; }
    uint32_t top = (uint32_t)update_area.y, bottom = top + update_area.height;
    if (settle_coverage_valid) {
        if ((uint32_t)settle_area.y < top) top = (uint32_t)settle_area.y;
        const uint32_t old_bottom = (uint32_t)settle_area.y + settle_area.height;
        if (old_bottom > bottom) bottom = old_bottom;
    }
    settle_area = tested_window(top, bottom); settle_coverage_valid = true;
    settle_until = busy_done_ms + 2300u;
    settle_stop = false; settle_refreshes = settle_completed = 0;
    settle_sync_offset = 0; settle_setup_step = 0; settle_power_ms = 0;
    /* One accepted frame receives one complete differential DRF. Normal
     * quiet time never replays an intermediate animation target. */
    settle_stage = SETTLE_WAIT;
    return true;
}
static void poll_settle_locked(uint32_t budget_ms) {
    uint64_t now = 0;
    uint32_t work = 0;
    if (!sample_now(&now)) goto failed;
    const uint32_t slice = budget_ms > 8u ? 8u : budget_ms;
    const uint64_t slice_end = now > UINT64_MAX - slice ? UINT64_MAX : now + slice;
    for (unsigned steps = 0; steps < 16u && settle_stage; ++steps) {
        if (!sample_now(&now)) goto failed;
        const bool cancel = settle_stop || present_state == PRESENT_QUEUED;
        const bool expired = now >= settle_until;
        if (settle_stage == SETTLE_WAIT) {
            if (cancel) settle_stage = SETTLE_NONE;
            else if (!expired) break;
            else {
                if (!panel_pin_read(X4PRO_PIN_EPD_BUSY)) { set_reason("idle sync busy active"); goto failed; }
                if (!settle_begin_final_target()) goto failed;
            }
        } else if (settle_stage == SETTLE_READY) {
            if (cancel) settle_stage = SETTLE_NONE;
            else if (expired) {
                if (!panel_pin_read(X4PRO_PIN_EPD_BUSY)) { set_reason("idle sync busy active"); goto failed; }
                if (!settle_begin_final_target()) goto failed;
            } else {
                if (!panel_pin_read(X4PRO_PIN_EPD_BUSY)) { set_reason("settle busy already active"); goto failed; }
                command(0x91); settle_stage = SETTLE_WINDOW;
            }
        } else if (settle_stage == SETTLE_WINDOW) {
            if (cancel || expired) settle_stage = SETTLE_CLOSE;
            else { window_for(&settle_area); settle_stage = SETTLE_REFRESH; }
        } else if (settle_stage == SETTLE_REFRESH) {
            if (cancel || expired) settle_stage = SETTLE_CLOSE;
            else {
                if (!panel_pin_read(X4PRO_PIN_EPD_BUSY)) { set_reason("settle busy already active"); goto failed; }
                if (!sample_now(&settle_refresh_ms)) goto failed;
                if (settle_refresh_ms > UINT64_MAX - 3500u) { set_reason("settle clock overflow"); goto failed; }
                command(0x12); ++settle_refreshes;
                if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) {
                    settle_phase_deadline = settle_refresh_ms + 100u; settle_stage = SETTLE_ASSERT;
                } else {
                    settle_phase_deadline = settle_refresh_ms + 3500u; settle_stage = SETTLE_DONE;
                }
            }
        } else if (settle_stage == SETTLE_ASSERT) {
            if (now >= settle_phase_deadline) { set_reason("settle busy never asserted"); goto failed; }
            if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) break;
            settle_phase_deadline = settle_refresh_ms + 3500u; settle_stage = SETTLE_DONE;
        } else if (settle_stage == SETTLE_DONE) {
            const bool complete = panel_pin_read(X4PRO_PIN_EPD_BUSY);
            if (io_failed) goto failed;
            if (!complete) {
                if (now >= settle_phase_deadline) { set_reason("settle busy completion timeout"); goto failed; }
                break;
            }
            ++settle_completed; settle_stage = SETTLE_CLOSE;
        } else if (settle_stage == SETTLE_CLOSE) {
            command(0x92);
            if (cancel) settle_stage = SETTLE_NONE;
            else if (expired) {
                if (!settle_begin_final_target()) goto failed;
            } else settle_stage = SETTLE_READY;
            if (io_failed) goto failed;
            break;
        } else if (settle_stage == SETTLE_FINAL_NEW) {
            if (cancel) settle_stage = SETTLE_NONE;
            else {
                if (!settle_sync_chunk(&work)) goto failed;
                if (settle_sync_offset == 60000u) {
                    settle_setup_step = 0;
                    settle_stage = SETTLE_FINAL_SETUP;
                }
            }
        } else if (settle_stage == SETTLE_FINAL_SETUP) {
            if (cancel) {
                if (settle_setup_step >= 2u) command(0x92);
                settle_stage = SETTLE_NONE;
            } else {
                const unsigned finish = 14u;
                switch (settle_setup_step) {
                case 0: reg1(0x30, 0x0F); break;
                case 1: command(0x91); break;
                case 2: {
                    const risc_display_rect_v1 full = {0, 0, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT};
                    window_for(&full); break;
                }
                case 3: { const uint8_t psr[] = {0x37, 0x4D}; write_register(0x00, psr, sizeof(psr)); break; }
                case 4: reg1(0x03, 0x20); break;
                case 5: reg1(0xE1, 0x02); break;
                case 6: reg1(0x50, 0xD7); break;
                case 7: reg1(0xE0, 0x02); break;
                case 8: reg1(0xE5, 0x5A); break;
                default:
                    if (settle_setup_step < finish)
                        write_a2_lut_table(settle_setup_step - 9u, X4PRO_FINAL_TARGET_FRAMES, true);
                    break;
                }
                if (settle_setup_step++ == finish) settle_stage = SETTLE_FINAL_REFRESH;
            }
        } else if (settle_stage == SETTLE_FINAL_REFRESH) {
            if (cancel) { command(0x92); settle_stage = SETTLE_NONE; }
            else {
                if (!panel_pin_read(X4PRO_PIN_EPD_BUSY)) { set_reason("final target busy already active"); goto failed; }
                if (!sample_now(&settle_refresh_ms)) goto failed;
                if (settle_refresh_ms > UINT64_MAX - 3500u) { set_reason("final target clock overflow"); goto failed; }
                command(0x12); ++settle_refreshes;
                if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) {
                    settle_phase_deadline = settle_refresh_ms + 100u; settle_stage = SETTLE_FINAL_ASSERT;
                } else {
                    settle_phase_deadline = settle_refresh_ms + 3500u; settle_stage = SETTLE_FINAL_DONE;
                }
            }
        } else if (settle_stage == SETTLE_FINAL_ASSERT) {
            if (now >= settle_phase_deadline) { set_reason("final target busy never asserted"); goto failed; }
            if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) break;
            settle_phase_deadline = settle_refresh_ms + 3500u; settle_stage = SETTLE_FINAL_DONE;
        } else if (settle_stage == SETTLE_FINAL_DONE) {
            const bool complete = panel_pin_read(X4PRO_PIN_EPD_BUSY);
            if (io_failed) goto failed;
            if (!complete) {
                if (now >= settle_phase_deadline) { set_reason("final target completion timeout"); goto failed; }
                break;
            }
            ++settle_completed; settle_stage = SETTLE_FINAL_CLOSE;
        } else if (settle_stage == SETTLE_FINAL_CLOSE) {
            command(0x92);
            if (cancel) settle_stage = SETTLE_NONE;
            else {
                if (!settle_begin_plane(0x10)) goto failed;
                settle_stage = SETTLE_SYNC_OLD;
            }
        } else if (settle_stage == SETTLE_SYNC_OLD) {
            if (!settle_sync_chunk(&work)) goto failed;
            if (settle_sync_offset == 60000u) {
                if (!settle_begin_plane(0x13)) goto failed;
                settle_stage = SETTLE_SYNC_NEW;
            }
        } else if (settle_stage == SETTLE_SYNC_NEW) {
            if (!settle_sync_chunk(&work)) goto failed;
            if (settle_sync_offset == 60000u) {
                /* Keep the reconciled panel powered. POF was observed to
                 * trigger rapid relaxation and re-expression of old pixels. */
                dtm1_synced = true; absolute_frames = 0; absolute_started_ms = UINT64_MAX;
                settle_stage = SETTLE_NONE;
            }
        } else { set_reason("invalid settle state"); goto failed; }
        if (io_failed || !sample_now(&now)) goto failed;
        if (now >= slice_end || work >= 16384u) break;
    }
    if (io_failed || !end_spi()) goto failed;
    if (!settle_stage) settle_coverage_valid = false;
    return;
failed: {
    const bool was_complete = present_state == PRESENT_COMPLETE;
    present_failed();
    if (was_complete) present_state = PRESENT_COMPLETE;
    }
}
static void poll_present_locked(uint32_t budget_ms) {
    uint64_t now = 0;
    if (!sample_now(&now)) goto failed;
    if (now > UINT64_MAX - 10000u) { set_reason("clock overflow"); goto failed; }
    if (present_state == PRESENT_QUEUED) {
        wait_start_ms = transfer_start_ms = now; wait_budget_ms = 10000u;
        metrics.valid_times |= RISC_DISPLAY_METRICS_TRANSFER_START;
        transfer_end_ms = refresh_ms = busy_assert_ms = busy_done_ms = 0;
        bytes_sent = 0; reason = "none"; transfer_started = true;
        async_stage = UC_ASYNC_PRE; async_offset = 0; async_deadline = now + 10000u;
        phase_deadline = now + 10000u; async_last_poll_ms = now;
        present_state = PRESENT_ACTIVE;
    } else {
        /* Storage and application execution outside this callback do not use
         * the display's service budget. Never extend a hardware phase timer. */
        const uint64_t idle_ms = now - async_last_poll_ms;
        if (async_deadline > UINT64_MAX - idle_ms) { set_reason("clock overflow"); goto failed; }
        async_deadline += idle_ms;
    }
    const uint64_t slice_end = now + (budget_ms > 8u ? 8u : budget_ms);
    unsigned work = 0;
    do {
        if (!sample_now(&now)) goto failed;
        if (now >= async_deadline) { set_reason("async present deadline"); goto failed; }
        if (async_stage == UC_ASYNC_PRE) {
            const bool ready = panel_pin_read(X4PRO_PIN_EPD_BUSY);
            if (io_failed) goto failed;
            if (!ready) {
                if (now >= phase_deadline) { set_reason("pre-transfer busy timeout"); goto failed; }
                break;
            }
            setup_step = 0; async_stage = UC_ASYNC_PREPARE;
        } else if (async_stage == UC_ASYNC_PREPARE) {
            if (fast_update && setup_step == 0u) command(0x91);
            else if (fast_update && setup_step == 1u) window_data();
            else if (!fast_update && setup_step == 0u) reg1(0x30, 0x0E);
            else {
                if (!begin_plane(fast_update || quality_partial ? 0x13 : 0x10)) goto failed;
                async_stage = fast_update || quality_partial ? UC_ASYNC_NEW : UC_ASYNC_WHITE;
            }
            ++setup_step;
        } else if (async_stage == UC_ASYNC_WHITE || async_stage == UC_ASYNC_NEW || async_stage == UC_ASYNC_OLD || async_stage == UC_ASYNC_SYNC) {
            const bool full_sync_plane = async_stage == UC_ASYNC_SYNC && sync_full;
            const uint32_t total = full_sync_plane ? 60000u :
                (fast_update ? update_area.height * 100u : 60000u);
            uint32_t count = total - async_offset;
            if (count > 512u) count = 512u;
            if (count > 16384u - work) count = 16384u - work;
            uint8_t buffer[512];
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t offset = async_offset + i;
                buffer[i] = async_stage == UC_ASYNC_WHITE ? 0xFFu :
                    (async_stage == UC_ASYNC_OLD ? (offset < 12000u ? 0xFFu :
                        (uint8_t)~previous_frame[offset - 12000u]) :
                    (full_sync_plane ? (offset < 12000u ? 0xFFu :
                        (uint8_t)~previous_frame[offset - 12000u]) : frame_byte(offset)));
            }
            if (!spi_held && !begin_spi(1000u)) goto failed;
            if (!exchange_spi(buffer, NULL, count)) goto failed;
            async_offset += count; bytes_sent += count; work += count;
            if (async_offset == total) {
                if (!end_spi()) goto failed;
                if (async_stage == UC_ASYNC_WHITE) { if (!begin_plane(0x13)) goto failed; async_stage = UC_ASYNC_NEW; }
                else if (async_stage == UC_ASYNC_NEW && quality_partial) {
                    /* Fast A2 never syncs OLD; deep wake loses panel RAM. Restore
                     * the canonical previous image before every OTP partial. */
                    if (!begin_plane(0x10)) goto failed;
                    async_stage = UC_ASYNC_OLD;
                } else if (async_stage == UC_ASYNC_SYNC) {
                    /* Differential OLD synchronization must use the same PTIN
                     * window and RAM cursor as the NEW upload. Close PTIN only
                     * after DTM1 contains the physically completed target. */
                    if (fast_update) { command(0x92); if (io_failed) goto failed; }
                    settle_coverage_valid = false;
                    if (!fast_update) remember_completed_frame();
                    dtm1_synced = true;
                    if (settle_update) { absolute_frames = 0; absolute_started_ms = UINT64_MAX; }
                    sync_full = false;
                    if (fast_update && !arm_settle()) goto failed;
                    present_state = PRESENT_COMPLETE; held = false;
                    previous_seeded = false; async_stage = UC_ASYNC_NONE; reason = "complete"; break;
                } else {
                    if (fast_update) command(0x92);
                    if (!sample_metric(&transfer_end_ms, RISC_DISPLAY_METRICS_TRANSFER_END)) goto failed;
                    async_stage = UC_ASYNC_SETUP; setup_step = 0;
                }
            }
        } else if (async_stage == UC_ASYNC_SETUP) {
            /* Exactly one bounded control transaction per step. Poll time is
             * sampled after every register/LUT, not just after the whole set. */
            const unsigned finish = fast_update ? 14u : (quality_partial ? 6u : 4u);
            if (fast_update) {
                switch (setup_step) {
                case 0: reg1(0x30, 0x0F); break;
                case 1: command(0x91); break;
                case 2: window_data(); break;
                case 3: { const uint8_t psr[] = {0x37, 0x4D}; write_register(0x00, psr, sizeof(psr)); break; }
                case 4: reg1(0x03, 0x20); break;
                case 5: reg1(0xE1, 0x02); break;
                case 6: reg1(0x50, 0xD7); break;
                case 7: reg1(0xE0, 0x02); break;
                case 8: reg1(0xE5, 0x5A); break;
                default: if (setup_step < finish) absolute_lut_table(setup_step - 9u); break;
                }
            } else {
                /* PON may restore MTP defaults. Replay every clean/quality
                 * register that the following OTP DRF depends on. */
                if (setup_step == 0u) reg1(0x30, 0x0E);
                else if (setup_step == 1u) reg1(0x50, quality_partial ? 0xD7 : 0x97);
                else if (setup_step == 2u) reg1(0xE0, 0x02);
                else if (setup_step == 3u) reg1(0xE5, quality_partial ? 0x5A : 0x1E);
                else if (quality_partial && setup_step == 4u) reg1(0x03, 0x20);
                else if (quality_partial && setup_step == 5u) reg1(0xE1, 0x02);
            }
            if (setup_step++ == finish) {
                if (!screen_powered) {
                    if (!panel_pin_read(X4PRO_PIN_EPD_BUSY)) { set_reason("power busy already active"); goto failed; }
                    command(0x04);
                    if (!sample_now(&now)) goto failed;
                    if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) {
                        phase_deadline = now + 100u; async_stage = UC_ASYNC_PON_ASSERT;
                    } else {
                        phase_deadline = now + 1500u; async_stage = UC_ASYNC_PON_DONE;
                    }
                } else async_stage = fast_update ? UC_ASYNC_REFRESH : (quality_partial ? UC_ASYNC_QUALITY_IN : UC_ASYNC_OTP);
            }
        } else if (async_stage == UC_ASYNC_PON_ASSERT) {
            if (now >= phase_deadline) { set_reason("power busy never asserted"); goto failed; }
            if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) break;
            phase_deadline = now + 1500u; async_stage = UC_ASYNC_PON_DONE;
        } else if (async_stage == UC_ASYNC_PON_DONE) {
            const bool complete = panel_pin_read(X4PRO_PIN_EPD_BUSY);
            if (io_failed) goto failed;
            if (!complete) {
                if (now >= phase_deadline) { set_reason("power busy completion timeout"); goto failed; }
                break;
            }
            screen_powered = true;
            /* PON may restore MTP defaults. Close any pre-PON partial
             * window and replay the selected profile before DRF. */
            if (fast_update) { command(0x92); if (io_failed) goto failed; }
            setup_step = 0; async_stage = UC_ASYNC_SETUP;
        } else if (async_stage == UC_ASYNC_QUALITY_IN) {
            command(0x91); async_stage = UC_ASYNC_QUALITY_WINDOW;
        } else if (async_stage == UC_ASYNC_QUALITY_WINDOW) {
            window_data(); async_stage = UC_ASYNC_OTP;
        } else if (async_stage == UC_ASYNC_OTP) {
            const uint8_t psr[] = {0x17, 0x4D}; write_register(0x00, psr, sizeof(psr));
            async_stage = UC_ASYNC_REFRESH;
        } else if (async_stage == UC_ASYNC_REFRESH) {
            busy_before = panel_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
            if (!busy_before) { set_reason("busy already active"); goto failed; }
            if (!sample_metric(&metrics.refresh_ms, RISC_DISPLAY_METRICS_REFRESH)) goto failed;
            refresh_ms = metrics.refresh_ms; command(0x12);
            /* Observe an immediate assertion before yielding at a slice edge;
             * a short valid BUSY pulse may be over by the next owner poll. */
            if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) {
                phase_deadline = refresh_ms + 100u; async_stage = UC_ASYNC_ASSERT;
            } else {
                if (!sample_metric(&busy_assert_ms, RISC_DISPLAY_METRICS_BUSY_ASSERT)) goto failed;
                phase_deadline = refresh_ms + 3500u; async_stage = UC_ASYNC_DONE;
            }
        } else if (async_stage == UC_ASYNC_ASSERT) {
            if (now >= phase_deadline) { set_reason("busy never asserted"); goto failed; }
            if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) break;
            if (!sample_metric(&busy_assert_ms, RISC_DISPLAY_METRICS_BUSY_ASSERT)) goto failed;
            phase_deadline = refresh_ms + 3500u; async_stage = UC_ASYNC_DONE;
        } else if (async_stage == UC_ASYNC_DONE) {
            /* Assertion was observed on entry to DONE. A late owner poll is
             * not evidence of a stuck panel when BUSY has already deasserted. */
            const bool complete = panel_pin_read(X4PRO_PIN_EPD_BUSY);
            if (io_failed) goto failed;
            if (!complete) {
                if (now >= phase_deadline) { set_reason("busy completion timeout"); goto failed; }
                break;
            }
            if (!sample_metric(&busy_done_ms, RISC_DISPLAY_METRICS_BUSY_DONE)) goto failed;
            if (fast_update) {
                /* Absolute updates end PTIN here because they intentionally do
                 * not synchronize OLD. Differential updates retain PTIN until
                 * the matching DTM1 window has been copied. */
                if (absolute_update) { command(0x92); if (io_failed) goto failed; }
                remember_completed_frame();
                if (absolute_update && !settle_update) {
                    dtm1_synced = false;
                    if (!absolute_frames) absolute_started_ms = busy_done_ms;
                    ++absolute_frames;
                    if (!arm_settle()) goto failed;
                    present_state = PRESENT_COMPLETE; held = false;
                    previous_seeded = false; async_stage = UC_ASYNC_NONE; reason = "complete"; break;
                }
                sync_full = settle_update;
                if (!begin_plane(0x10)) goto failed;
                async_stage = UC_ASYNC_SYNC;
            } else {
                /* Full-stride OLD sync must run outside the partial RAM window. */
                if (quality_partial) command(0x92);
                if (!begin_plane(0x10)) goto failed;
                async_stage = UC_ASYNC_SYNC;
            }
        } else { set_reason("invalid async state"); goto failed; }
        if (io_failed) goto failed;
        if (!sample_now(&now)) goto failed;
    } while (now < slice_end && work < 16384u);
    if (io_failed || !end_spi() || !sample_now(&async_last_poll_ms)) goto failed;
    return;
failed:
    present_failed();
}
static void poll_work_locked(uint32_t budget_ms) {
    if (settle_stage) poll_settle_locked(budget_ms);
    else if (present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE)
        poll_present_locked(budget_ms);
}
static void poll_present(uint32_t budget_ms) {
    if (!budget_ms || !started || shutdown_stage ||
        (!settle_stage && present_state != PRESENT_QUEUED && present_state != PRESENT_ACTIVE) || !enter()) return;
    poll_work_locked(budget_ms); (void)leave();
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
    /* This diagnostic profile deliberately retains panel power and refuses
     * normal sleep preparation after the first physical frame. */
    out->flags = RISC_DISPLAY_INFO_RETAINS_IMAGE | RISC_DISPLAY_INFO_PARTIAL_DAMAGE;
    if (frontlight && frontlight->set_level) out->flags |= RISC_DISPLAY_INFO_BRIGHTNESS;
    if (controller == PROBE_UC8279) out->flags |= RISC_DISPLAY_INFO_ASYNC_PRESENT;
    out->damage_x_alignment = 8;
    out->damage_width_alignment = 8;
    out->damage_y_alignment = 1;
    out->damage_height_alignment = 1;
    out->nominal_refresh_millihz = 11000; /* Three-frame differential reference. */
    out->typical_present_latency_us = 90000; /* Approximately 89 ms at 160 rows. */
    return true;
}
static bool acquire_impl(void *context, uint32_t format, risc_display_surface_v1 *out) {
    (void)context;
    if (!started || shutdown_stage || presentation_fault || held || !out || format != RISC_DISPLAY_FORMAT_MONO1) return false;
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
    if (!started || shutdown_stage || presentation_fault || !held || frame_id != frame_serial || present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE)
        return false;
    if (options && (options->intent > RISC_DISPLAY_PRESENT_CLEAN || options->queue_policy != RISC_DISPLAY_QUEUE_FIFO || options->reserved)) return false;
    if (!token_out || count > RISC_DISPLAY_MAX_DAMAGE_RECTS || (count && !damage)) return false;
    const uint8_t intent = options ? options->intent : RISC_DISPLAY_PRESENT_DEFAULT;
    quality_partial = intent == RISC_DISPLAY_PRESENT_QUALITY && count && (completed_history || previous_seeded);
    fast_update = completed_history && (intent == RISC_DISPLAY_PRESENT_DEFAULT || intent == RISC_DISPLAY_PRESENT_LOW_LATENCY);
    absolute_update = settle_update = false; fast_lut_frames = 1u; sync_full = false;
    if (fast_update) {
        /* Truthful DTM1 OLD plus DTM2 NEW makes unchanged 00/11 pixels idle.
         * Only actual W->B and B->W transitions receive the bounded pulse. */
        absolute_update = false;
    }
    partial_update = count && (fast_update || quality_partial);
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
    if (fast_update) {
        /* Keep exact 0.1.12 full-width 40/80/160/480-row geometry while
         * making unchanged pixels electrically idle. */
        update_area = tested_window((uint32_t)update_area.y, (uint32_t)update_area.y + update_area.height);
        fast_lut_frames = update_area.height <= X4PRO_ACTIVE_LOCAL_MAX_ROWS ?
            X4PRO_ACTIVE_LOCAL_FRAMES : X4PRO_ACTIVE_BROAD_FRAMES;
    } else if (!quality_partial) update_area = (risc_display_rect_v1){0, 0, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT};
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
    settle_stop = true;
    if (settle_stage == SETTLE_WAIT || settle_stage == SETTLE_READY) settle_stage = SETTLE_NONE;
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
static bool set_brightness_impl(void *context, uint16_t level, uint16_t maximum) {
    (void)context;
    return started && !shutdown_stage && maximum && level<=maximum && frontlight &&
        frontlight->set_level(frontlight->context, level, maximum);
}
static bool seed_previous_impl(void *context, risc_display_frame_v1 frame_id) {
    (void)context;
    if (!started || shutdown_stage || !held || frame_id != frame_serial ||
        present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return false;
    previous_seeded = completed_history = false; dtm1_synced = false;
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
    if (!sync_api || !sync_api->is_owner(sync_api->context) || !clock_api || !out) return false;
    const uint64_t began = now_ms();
    if (began == UINT64_MAX || began > UINT64_MAX - ms) return false;
    const uint64_t deadline = began + ms;
    for (unsigned polls = 0; polls < 10001u; ++polls) {
        if (!enter()) return false;
        if (shutdown_stage || !token || token != pending_token) { (void)leave(); return false; }
        if (ms && (present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE)) {
            uint64_t now = 0;
            if (!sample_now(&now) || now >= deadline || polls == 10000u) {
                set_reason("wait present deadline"); present_failed();
            } else {
                const uint64_t remaining = deadline - now;
                poll_work_locked(remaining > 8u ? 8u : (uint32_t)remaining);
            }
        }
        const bool ok = present_status_impl(c, token, out);
        if (!leave() || !ok) return false;
        if (!ms || out->state == PRESENT_COMPLETE || out->state == PRESENT_FAILED) return true;
        sleep_ms(1);
    }
    return false;
}
static bool set_brightness(void *c, uint16_t level, uint16_t maximum) {
    if (!enter()) return false;
    const bool ok = set_brightness_impl(c, level, maximum); return leave() && ok;
}
/* Forward only to the admitted frontlight dependency; tone never acquires a
 * frame or advances display work. A failed callback may have changed output,
 * so do not retry it or issue rollback/cleanup writes. */
static bool set_tone(void *context, uint16_t warm, uint16_t maximum) {
    (void)context;
    if (!maximum || warm > maximum || !enter()) return false;
    const risc_frontlight_api_v1_tone *tone = risc_frontlight_tone(frontlight);
    const bool ok = started && !shutdown_stage && tone &&
        tone->set_tone(frontlight->context, warm, maximum);
    return leave() && ok;
}
static int32_t get_tone(void *context, uint16_t *warm, uint16_t *maximum) {
    (void)context;
    if (!warm || !maximum || warm == maximum || !enter()) return RISC_DISPLAY_TONE_FAILED;
    int32_t result = RISC_DISPLAY_TONE_FAILED;
    uint16_t value = 0, limit = 0;
    if (started && !shutdown_stage) {
        const risc_frontlight_api_v1_tone *tone = risc_frontlight_tone(frontlight);
        if (!tone) result = RISC_DISPLAY_TONE_UNAVAILABLE;
        else if (tone->get_tone(frontlight->context, &value, &limit) && limit && value <= limit)
            result = RISC_DISPLAY_TONE_OK;
    }
    if (!leave()) return RISC_DISPLAY_TONE_FAILED;
    if (result == RISC_DISPLAY_TONE_OK) { *warm = value; *maximum = limit; }
    return result;
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
/* Explicit final-image preparation only. Normal rendering never calls this
 * and retains its exact shipped cadence. No panel I/O occurs in this callback. */
static bool request_settle(void *context, risc_display_present_token_v1 token) {
    (void)context;
    if (!enter()) return false;
    const bool valid = started && !shutdown_stage && !presentation_fault && !held &&
        token && token == pending_token && present_state == PRESENT_COMPLETE &&
        completed_history && !previous_seeded;
    if (valid && settle_stage == SETTLE_WAIT) settle_stage = SETTLE_READY;
    return leave() && valid;
}
static int32_t settled_status(void *context, risc_display_present_token_v1 token) {
    (void)context;
    if (!enter()) return RISC_DISPLAY_SETTLED_FAILED;
    int32_t result = RISC_DISPLAY_SETTLED_FAILED;
    if (started && !shutdown_stage && !presentation_fault && token && token == pending_token) {
        if (present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE)
            result = RISC_DISPLAY_SETTLED_PENDING;
        else if (present_state == PRESENT_COMPLETE && completed_history && !previous_seeded)
            result = held || settle_stage ? RISC_DISPLAY_SETTLED_PENDING : RISC_DISPLAY_SETTLED_COMPLETE;
    }
    return leave() ? result : RISC_DISPLAY_SETTLED_FAILED;
}
static const risc_display_output_api_v1_settled api;
static bool overlaps(uintptr_t first, uintptr_t end, const void *storage, size_t size) {
    const uintptr_t address = (uintptr_t)storage;
    return first < address + size && address < end;
}
static bool copy_completed(void *context, uint32_t format, void *pixels,
                           size_t size_bytes, uint32_t stride_bytes) {
    (void)context;
    if (!pixels || format != RISC_DISPLAY_FORMAT_MONO1 || stride_bytes < 100u ||
        X4PRO_PANEL_HEIGHT > SIZE_MAX / stride_bytes ||
        size_bytes < (size_t)stride_bytes * X4PRO_PANEL_HEIGHT ||
        !started || shutdown_stage || retained || presentation_fault || held ||
        !completed_history || previous_seeded || present_state == PRESENT_QUEUED ||
        present_state == PRESENT_ACTIVE || !sync_api || !mutex ||
        !sync_api->is_owner(sync_api->context)) return false;
    const uintptr_t first = (uintptr_t)pixels;
    if (size_bytes > UINTPTR_MAX - first) return false;
    const uintptr_t end = first + size_bytes;
    /* Reject stale frame leases and all exported/state buffers before writes.
     * This callback never returns or retains a provider storage pointer. */
    if (overlaps(first, end, frame, sizeof(frame)) ||
        overlaps(first, end, previous_frame, sizeof(previous_frame)) ||
        overlaps(first, end, &api, sizeof(api)) ||
        overlaps(first, end, &metrics, sizeof(metrics)) ||
        overlaps(first, end, probe_text, sizeof(probe_text)) ||
        overlaps(first, end, last_error_text, sizeof(last_error_text)) ||
        overlaps(first, end, pin_tokens, sizeof(pin_tokens))) return false;
    for (size_t y = 0; y < X4PRO_PANEL_HEIGHT; ++y)
        memcpy((uint8_t *)pixels + y * stride_bytes, previous_frame + y * 100u, 100u);
    return true;
}
static const risc_display_output_api_v1_settled api = {
    {{{{{{ RISC_DISPLAY_OUTPUT_API_V1, sizeof(api), 0, get_info, acquire, release, submit,
       present_status, wait_present, set_brightness },
     RISC_DISPLAY_HISTORY_TAG, 1u, seed_previous},
    RISC_DISPLAY_POWER_TAG, 1u, power_prepare, power_resume},
    RISC_DISPLAY_METRICS_TAG, RISC_DISPLAY_METRICS_VERSION, present_metrics},
    RISC_DISPLAY_SNAPSHOT_TAG, RISC_DISPLAY_SNAPSHOT_VERSION, copy_completed},
    RISC_DISPLAY_FRONTLIGHT_TAG, RISC_DISPLAY_FRONTLIGHT_VERSION, set_tone, get_tone},
    RISC_DISPLAY_SETTLED_TAG, RISC_DISPLAY_SETTLED_VERSION, settled_status, request_settle
};
/* Typed lifecycle. */
static bool valid_configuration(const risc_hardware_device_v1 *h, int *expected) {
    if (!h || h->api_version != 1 || h->struct_size < sizeof(*h) || !h->instance_id ||
        !h->compatible || !h->revision || strcmp(h->revision, "unspecified") ||
        !h->config_type || strcmp(h->config_type, "display.spi") || h->config_version != 1 ||
        h->config_size != sizeof(risc_hw_spi_display_v1) || !h->config) return false;
    if (!strcmp(h->compatible, "ultrachip,uc8279")) *expected = PROBE_UC8279;
    else return false;
    const risc_hw_spi_display_v1 *c = h->config;
    if (c->struct_size != sizeof(*c) || c->bus.struct_size != sizeof(c->bus) ||
        c->bus.kind != RISC_HW_BUS_SPI || !c->bus.instance_id || c->bus.controller > 2 ||
        c->bus.frequency_hz != 20000000u || c->bus.mode ||
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
    if (gpio || sync_api || mutex || started || retained || !deps || count != 7) return false;
    last_error_text[0]=0;
    const risc_hardware_device_v1 *hardware = NULL;
    const garden_gpio_v1 *candidate = NULL;
    const garden_spi_v1 *bus = NULL;
    const risc_platform_clock_api_v1 *clock = NULL;
    const risc_provider_sync_api_v1 *sync = NULL;
    const x4_power_ready_api_v1 *power = NULL;
    const risc_frontlight_api_v1 *light = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!deps[i].capability_id || deps[i].api_version != 1 || !deps[i].api) return false;
        const char *name = deps[i].capability_id;
        if (!strcmp(name, "hardware.device") && !hardware) hardware = deps[i].api;
        else if (!strcmp(name, "platform.gpio") && !candidate) candidate = deps[i].api;
        else if (!strcmp(name, "spi.bus") && !bus) bus = deps[i].api;
        else if (!strcmp(name, "platform.clock") && !clock) clock = deps[i].api;
        else if (!strcmp(name, RISC_PROVIDER_SYNC_CAPABILITY) && !sync) sync = deps[i].api;
        else if (!strcmp(name, X4_POWER_READY_CAPABILITY) && !power) power = deps[i].api;
        else if (!strcmp(name, "display.frontlight") && !light) light = deps[i].api;
        else return false;
    }
    int expected = 0;
    if (!valid_configuration(hardware, &expected) ||
        !bus || bus->api_version != 1 || bus->struct_size < GARDEN_SPI_THREE_WIRE_V1_SIZE ||
        !bus->claim_three_wire || !bus->begin || !bus->exchange || !bus->end || !bus->release ||
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
    frontlight = light; gpio = candidate; spi = bus; spi_hz = 100000u; clock_api = clock; sync_api = sync; configuration = hardware->config;
    if (!sync_api->create(sync_api->context, &mutex) || !mutex) {
        frontlight = NULL; gpio = NULL; spi = NULL; clock_api = NULL; sync_api = NULL; configuration = NULL; return false;
    }
    if (!enter()) return false;
    physical_pins[X4PRO_PIN_EPD_BUSY] = (uint8_t)configuration->busy;
    physical_pins[X4PRO_PIN_EPD_DC] = (uint8_t)configuration->dc;
    physical_pins[X4PRO_PIN_EPD_RST] = (uint8_t)configuration->reset;
    shutdown_stage = 0; previous_seeded = completed_history = partial_update = false;
    fast_update = absolute_update = settle_update = quality_partial = false;
    dtm1_synced = sync_full = false; fast_lut_frames = 1u;
    absolute_frames = 0; absolute_started_ms = UINT64_MAX;
    settle_stage = SETTLE_NONE; settle_stop = settle_coverage_valid = false;
    settle_until = settle_phase_deadline = settle_refresh_ms = 0;
    settle_refreshes = settle_completed = settle_sync_offset = 0; settle_setup_step = 0; settle_power_ms = 0;
    present_state = PRESENT_NONE; pending_token = 0; last_sample_ms = now;
    memset(&metrics, 0, sizeof(metrics));
    bytes_sent = 0; transfer_start_ms = transfer_end_ms = refresh_ms = busy_assert_ms = busy_done_ms = 0;
    reason = "none"; operation_deadline = now + 2000u;
    prepare_pins();
    if (io_failed || !spi->claim_three_wire(spi->context, configuration->bus.sclk, configuration->bus.mosi, configuration->cs, &spi_token) || !spi_token) {
        spi_failed("spi claim retained"); (void)leave(); return false;
    }
    const int verdict = probe_controller();
    if (verdict != expected) {
        set_reason(verdict == PROBE_AMBIGUOUS ? "ambiguous-controller" : "controller mismatch");
        (void)leave(); return false;
    }
    controller = verdict; spi_hz = configuration->bus.frequency_hz; screen_powered = false;
    for (size_t i = 0; i < sizeof(frame); ++i) frame[i] = 0;
    started = !io_failed && uc_init_panel();
    operation_deadline = 0;
    return leave() && started;
}
/* Each operation has a total owner-admission deadline, at most 1500 ms and
 * 150 ten-ms readiness polls. Single commands are finite (at most 6 bytes),
 * completed before deadline sampling. Once the panel has displayed an image,
 * this diagnostic profile refuses sleep preparation rather than issuing POF
 * or DSLP and allowing inactive-region relaxation. */
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
        const bool busy = !panel_pin_read(X4PRO_PIN_EPD_BUSY);
        if (io_failed) return RISC_DISPLAY_POWER_RETAINED;
        if (!busy && last_sample_ms >= shutdown_not_before) return power_checkpoint(deadline);
        result = power_delay(deadline, 10u);
        if (result) return result;
    }
    set_reason("panel power readiness bound");
    return RISC_DISPLAY_POWER_TIMEOUT;
}
static int32_t drain_settle_for_power(uint64_t deadline) {
    /* Never expose a weak fast frame by cancelling directly into POF. Force the
     * quiet interval expired, drain any already-started resident pulse, then run
     * the same full-frame absolute endpoint redraw and dual-plane reconciliation
     * used by normal idle finalization. */
    settle_stop = false;
    settle_until = 0;
    for (unsigned checks = 0; settle_stage; ++checks) {
        int32_t result = power_checkpoint(deadline);
        if (result) return result;
        if (checks >= RISC_DISPLAY_POWER_MAX_BUDGET_MS) {
            set_reason("settle power readiness bound"); return RISC_DISPLAY_POWER_TIMEOUT;
        }
        const uint64_t remaining = deadline - last_sample_ms;
        poll_settle_locked(remaining > 8u ? 8u : (uint32_t)remaining);
        if (presentation_fault || retained) return RISC_DISPLAY_POWER_RETAINED;
        if (settle_stage) {
            result = power_delay(deadline, 1u);
            if (result) return result;
        }
    }
    return RISC_DISPLAY_POWER_OK;
}
static int32_t prepare_power_impl(uint64_t deadline) {
    if (presentation_fault) return RISC_DISPLAY_POWER_RETAINED;
    if (held || present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE || shutdown_stage == 5u)
        return RISC_DISPLAY_POWER_BUSY;
    if (settle_stage) {
        const int32_t drained = drain_settle_for_power(deadline);
        if (drained) return drained;
    }
    if (!pins_ready || (!started && !shutdown_stage) || shutdown_stage == 4u ||
        (controller != PROBE_SSD && controller != PROBE_UC8279)) return RISC_DISPLAY_POWER_UNAVAILABLE;
    if (shutdown_stage == 3u && reset_held) return RISC_DISPLAY_POWER_OK;
    int32_t result = power_checkpoint(deadline);
    if (result) return result;
    if (shutdown_stage == 0u) {
        const bool busy = !panel_pin_read(X4PRO_PIN_EPD_BUSY);
        if (io_failed) return RISC_DISPLAY_POWER_RETAINED;
        if (busy) return RISC_DISPLAY_POWER_BUSY;
        /* Refuse sleep before invalidating any live state. A BUSY result must
         * leave started/history/planes intact so normal rendering can continue. */
        if (screen_powered) return RISC_DISPLAY_POWER_BUSY;
        started = false; previous_seeded = completed_history = dtm1_synced = false;
        absolute_frames = 0; absolute_started_ms = UINT64_MAX;
        shutdown_stage = 2u;
    }
    if (shutdown_stage == 1u) {
        result = power_ready(deadline);
        if (result) return result;
        shutdown_stage = 2u;
    }
    if (shutdown_stage == 2u) {
        result = power_checkpoint(deadline);
        if (result) return result;
        command(0x07); data1(0xA5);
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
     * frame clear, PON or refresh. RESET recovers pre-display DSLP/refusals. */
    panel_pin_output(X4PRO_PIN_EPD_RST, false);
    result = power_delay(deadline, controller == PROBE_UC8279 ? 50u : 10u);
    if (result) return result;
    panel_pin_output(X4PRO_PIN_EPD_RST, true);
    result = power_delay(deadline, controller == PROBE_UC8279 ? 50u : 10u);
    if (result) return result;
    result = power_ready(deadline); if (result) return result;
    RESUME_SEND(0x00, 0x37, 0x4D);
    RESUME_SEND(0x61, 0x03, 0x20, 0x02, 0x58);
    RESUME_SEND(0x65, 0, 0, 0, 0);
    RESUME_SEND(0x03, 0x20); RESUME_SEND(0x30, 0x0E); RESUME_SEND(0xE1, 0x02);
    result = power_ready(deadline);
    if (result) return result;
    started = true; screen_powered = false; shutdown_stage = 0; pending_token = 0; present_state = PRESENT_NONE;
    memset(&metrics, 0, sizeof(metrics));
    bytes_sent = 0; transfer_start_ms = transfer_end_ms = refresh_ms = busy_assert_ms = busy_done_ms = 0;
    partial_update = fast_update = absolute_update = settle_update = quality_partial = false;
    dtm1_synced = sync_full = false; fast_lut_frames = 1u;
    absolute_frames = 0; absolute_started_ms = UINT64_MAX;
    async_stage = UC_ASYNC_NONE; transfer_started = false;
    settle_stage = SETTLE_NONE; settle_stop = settle_coverage_valid = false;
    settle_sync_offset = 0; settle_setup_step = 0; settle_power_ms = 0;
    return RISC_DISPLAY_POWER_OK;
}
#undef RESUME_SEND
static int32_t power_call(bool resume, uint32_t timeout_ms) {
    if (!sync_api || !clock_api || !mutex || !sync_api->is_owner(sync_api->context))
        return RISC_DISPLAY_POWER_UNAVAILABLE;
    if (retained || presentation_fault) return RISC_DISPLAY_POWER_RETAINED;
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
    if (spi_token) {
        if (!end_spi() || !spi->release(spi->context, spi_token)) { (void)leave(); return false; }
        spi_token = 0;
    }
    for (size_t i = 0; i < PANEL_PINS; ++i) {
        if (!pin_tokens[i]) continue;
        if (!gpio->release(gpio->context, pin_tokens[i])) { (void)leave(); return false; }
        pin_tokens[i] = 0;
    }
    if (!leave()) return false;
    if (!sync_api->destroy(sync_api->context, mutex)) return false;
    mutex = 0; frontlight = NULL; gpio = NULL; spi = NULL; clock_api = NULL; sync_api = NULL; configuration = NULL;
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
    append(destination, capacity, &used, "v=0.1.18 cause=");
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
    append(destination, capacity, &used, " settle=");
    append_u(destination, capacity, &used, settle_stage);
    append(destination, capacity, &used, "/");
    append_u(destination, capacity, &used, settle_completed);
    append(destination, capacity, &used, "/");
    append_u(destination, capacity, &used, settle_refreshes);
    append(destination, capacity, &used, " old=");
    append_u(destination, capacity, &used, dtm1_synced);
    append(destination, capacity, &used, " abs=");
    append_u(destination, capacity, &used, absolute_update);
    append(destination, capacity, &used, " settle2=");
    append_u(destination, capacity, &used, settle_update);
    append(destination, capacity, &used, " burst=");
    append_u(destination, capacity, &used, absolute_frames);
    append(destination, capacity, &used, " off=");
    append_u(destination, capacity, &used, !screen_powered);
    append(destination, capacity, &used, " ");
    append(destination, capacity, &used, probe_text);
    return used > 0;
}
static const risc_driver_poll_v2 driver = {
    {{ RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_poll_v2), "x4pro-uc8279-fast",
      "display.output", 1, &api, start, stop, quiesce },
    last_error, NULL}, poll_present
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver.streams.driver;
}
