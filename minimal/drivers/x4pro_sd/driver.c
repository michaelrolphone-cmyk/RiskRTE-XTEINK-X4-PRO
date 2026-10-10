/* X4 hardware one-bit SDMMC transport, provider ABI2.
 * Legacy GPIO is available only in explicitly selected compatibility tests.
 * Protocol derived from Drivers/x4pro_sd/driver.c at Reader 34d8e694.
 * Filesystem implementation is shared by all consumers in RiscRTE-Drivers StorageFatFs.
 * GPIO authority and synchronization are scoped to this hardware.device. */
#ifndef X4PRO_SD_ALLOW_LEGACY_GPIO
#define X4PRO_SD_ALLOW_LEGACY_GPIO 0
#endif
#include <RiscPlatformClockV1.h>
#include <RiscProviderV2.h>
#include <RiscProviderSyncV1.h>
#include <RiscDiagnosticSourceV1.h>
#include <RiscStorageExportV1.h>
#include <RiscStorageVolumeFsV1.h>
#include <GardenPlatformV1.h>
#include <RiscGpioSdmmcV1.h>
#include "../x4pro_board_power/PowerReadyV1.h"
#include "../../../Drivers/x4pro_board/x4pro_pins.h"
#include <sd_protocol.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define x4pro_sd_command risc_sd_command
#define x4pro_sd_crc16 risc_sd_crc16
static const risc_platform_clock_api_v1 *clock_api;
static const garden_gpio_v1 *gpio_api;
static const risc_sdmmc_host_api_v1 *sdmmc_api;
static uint64_t sdmmc_token;
static bool sdmmc_fault;
static const risc_provider_sync_api_v1 *sync_api;
static const risc_diagnostic_source_api_v1 *diagnostic_source;
static void bootlog_drain(void);
static bool bootlog_mount_pending, bootlog_paused;
enum { EXPORT_LOCAL, EXPORT_PREPARING, EXPORT_HOST, EXPORT_RETAINED };
static unsigned export_state;
static risc_storage_export_token_t export_generation, export_token;
static uint64_t card_block_count;
static bool bootlog_custody_safe(void);
static uint64_t operation_mutex;
static bool mutex_poisoned, quiescing, quiesced;
static bool gpio_fault, gpio_retained;
typedef struct { uint64_t token; bool output, pullup, held; } sd_pin;
static sd_pin pins[4];
static const uint8_t pin_numbers[4] = {5, 41, 42, 40};
static sd_pin *pin_for(uint32_t pin) {
    for (unsigned i=0; i<4; ++i) if (pin_numbers[i] == pin) return &pins[i];
    return NULL;
}
static void x4pro_pin_input(uint32_t pin, bool pullup);
static bool configure_pin(uint32_t pin, bool output, bool initial, bool pullup) {
    sd_pin *state = pin_for(pin);
    if (!state || !gpio_api || gpio_fault) return false;
    if (state->token && state->output == output && state->pullup == pullup)
        return !output || gpio_api->write(gpio_api->context, state->token, initial);
    if (state->token) {
        if (!gpio_api->release(gpio_api->context, state->token)) return false;
        state->token = 0;
    }
    if (!gpio_api->claim(gpio_api->context, (uint8_t)pin, output, initial, pullup, &state->token) || !state->token) {
        /* A failed claim cannot justify discarding possible native ownership. */
        gpio_retained = true;
        return false;
    }
    state->output = output; state->pullup = pullup; state->held = false;
    return true;
}
static void x4pro_pin_output(uint32_t pin, bool high) {
    if (!configure_pin(pin, true, high, false)) gpio_fault = true;
}
static void x4pro_pin_level(uint32_t pin, bool high) { x4pro_pin_output(pin, high); }
static void x4pro_pin_input(uint32_t pin, bool pullup) {
    if (!configure_pin(pin, false, false, pullup)) gpio_fault = true;
}
static void x4pro_pin_release(uint32_t pin) { x4pro_pin_input(pin, true); }
static bool x4pro_pin_read(uint32_t pin) {
    sd_pin *state = pin_for(pin); bool high = true;
    if (gpio_fault || !state || !state->token || !gpio_api->read(gpio_api->context, state->token, &high)) {
        gpio_fault = true; return true;
    }
    return high;
}
static void x4pro_pin_hold(uint32_t pin, bool hold) {
    sd_pin *state = pin_for(pin);
    /* A fresh claim stages HIGH before releasing a bootstrap/deep-sleep hold. */
    if (!hold && state && !state->token) { x4pro_pin_output(pin, true); return; }
    if (gpio_fault || !state || !state->token || !state->output) { gpio_fault = true; return; }
    const int32_t result = gpio_api->deep_sleep_hold(gpio_api->context, state->token, hold);
    if (result) { gpio_fault = true; if (result == RISC_DEEP_SLEEP_RETAINED) gpio_retained = true; }
    else state->held = hold;
}
static bool valid_task(void) { return sync_api && sync_api->is_owner(sync_api->context); }
static bool guard_enter(void) {
    return valid_task() && operation_mutex && !mutex_poisoned && !quiescing && !gpio_retained &&
        sync_api->try_lock(sync_api->context, operation_mutex);
}
static bool guard_leave(void) {
    if (valid_task() && !mutex_poisoned) bootlog_drain();
    if (!valid_task() || !sync_api->unlock(sync_api->context, operation_mutex)) {
        mutex_poisoned = true; return false;
    }
    return bootlog_custody_safe();
}
static bool started, high_capacity;
static char error[80];
static bool bootlog_capture_error;
static char bootlog_media_error[sizeof(error)];
static bool mounted, card_ready, io_failed;
static uint32_t card_rca;
static bool mount_filesystem(void);
static uint64_t last_cooperate;
static unsigned cooperate_bytes;
/* usleep(1000) can consume a scheduler tick. Sleeping every 64 bytes
 * imposed 16384 waits/MiB before protocol work. Check elapsed time every
 * 64 bytes, but yield only after 4 KiB or 4 ms, whichever arrives first.
 * State spans API calls so small metadata reads also cooperate. */
static void cooperate(unsigned bytes) {
    cooperate_bytes += bytes;
    const uint64_t now = clock_api->monotonic_ms(clock_api->context);
    if (cooperate_bytes >= 4096u || now - last_cooperate >= 4u) {
        clock_api->sleep_ms(clock_api->context, 1);
        last_cooperate = clock_api->monotonic_ms(clock_api->context);
        cooperate_bytes = 0;
    }
}

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void fail(const char *text) {
    size_t i = 0;
    while (text[i] && i + 1u < sizeof(error)) { error[i] = text[i]; ++i; }
    error[i] = 0;
    if(bootlog_capture_error && !bootlog_media_error[0])memcpy(bootlog_media_error,error,sizeof(error));
}
/* ESP32-S3 TRM 7.2.4.1 caps CPU_CLK at 240 MHz. After the GPIO input
 * read-back observes each output level, 24 CPU cycles hold that phase for at
 * least 100 ns: comfortably above default-SD's 10 ns HIGH/LOW minimum and
 * below its 25 MHz maximum (at most 5 MHz before software/bus overhead).
 * Do not infer a timing minimum from back-to-back APB stores. */
#define SELECTED_PHASE_CYCLES 24u
/* 1.25 us at the ESP32-S3 maximum 240 MHz: identification <= 400 kHz. */
#define IDENTIFICATION_PHASE_CYCLES 300u
#define CLOCK_GUARD_POLLS 128u
static uint32_t cycle_count(void) {
#if defined(__XTENSA__)
    uint32_t value;
    __asm__ __volatile__("rsr.ccount %0" : "=a"(value) :: "memory");
    return value;
#elif defined(X4PRO_SD_CYCLE_COUNT)
    return X4PRO_SD_CYCLE_COUNT();
#else
#error "SD clock guard requires a native cycle counter or explicit test clock"
#endif
}
static bool selected_phase(bool high) {
    bool observed = false;
    for (unsigned attempt = 0; attempt < CLOCK_GUARD_POLLS; ++attempt) {
        if (x4pro_pin_read(X4PRO_PIN_SD_CLK) == high) { observed = true; break; }
    }
    if (!observed) return false;
    const uint32_t began = cycle_count();
    for (unsigned attempt = 0; attempt < CLOCK_GUARD_POLLS; ++attempt)
        if ((uint32_t)(cycle_count() - began) >= (card_ready ? SELECTED_PHASE_CYCLES : IDENTIFICATION_PHASE_CYCLES)) return true;
    return false; /* Stuck read-back or counter must never hang the SD owner. */
}
static bool tick(void) {
    if (gpio_fault) return false;
    x4pro_pin_level(X4PRO_PIN_SD_CLK, true);
    if (gpio_fault || !selected_phase(true)) return false;
    x4pro_pin_level(X4PRO_PIN_SD_CLK, false);
    return !gpio_fault && selected_phase(false);
}
static bool wait_dat0(bool level, uint32_t max_clocks, uint32_t budget_ms) {
    const uint64_t began = clock_api->monotonic_ms(clock_api->context);
    for (uint32_t i = 0; i < max_clocks; ++i) {
        if (x4pro_pin_read(X4PRO_PIN_SD_DAT0) == level) return true;
        if (!tick()) return false;
        if ((i & 255u) == 255u) {
            clock_api->sleep_ms(clock_api->context, 1);
            if (clock_api->monotonic_ms(clock_api->context) - began >= budget_ms) break;
        }
    }
    return false;
}
static bool cmd_bit(bool bit) { x4pro_pin_output(X4PRO_PIN_SD_CMD, bit); return tick(); }
static bool command(uint8_t index, uint32_t arg, uint8_t *response, size_t length) {
    if (length > 17u || (length && !response)) return false;
    uint8_t frame[6];
    x4pro_sd_command(index, arg, frame);
    for (int i = 0; i < 8; ++i) if (!tick()) return false;
    for (size_t byte = 0; byte < sizeof(frame); ++byte)
        for (int bit = 7; bit >= 0; --bit) if (!cmd_bit((frame[byte] >> bit) & 1)) return false;
    x4pro_pin_release(X4PRO_PIN_SD_CMD);
    /* CMD0 has no response on the native SD bus. */
    if (!length) { for (int i = 0; i < 8; ++i) if (!tick()) return false; return true; }
    bool seen = false;
    for (int i = 0; i < 64 && !seen; ++i) {
        seen = !x4pro_pin_read(X4PRO_PIN_SD_CMD);
        if (!seen && !tick()) return false;
    }
    if (!seen) return false;
    memset(response, 0, length);
    for (size_t byte = 0; byte < length; ++byte) {
        uint8_t value = 0;
        for (int bit = 0; bit < 8; ++bit) {
            bool level = x4pro_pin_read(X4PRO_PIN_SD_CMD);
            if (!tick()) return false;
            value = (uint8_t)((value << 1) | (level ? 1u : 0u));
        }
        response[byte] = value;
    }
    return true;
}
static bool response_for(uint8_t index, const uint8_t response[6]) {
    return (response[0] & 0xc0u) == 0u && (response[0] & 0x3fu) == index;
}
/* Close the peripheral before remuxing pins or removing card power. A failed
 * close keeps its token, so terminal cleanup can retry without losing custody. */
static bool close_sdmmc(void) {
    if (!sdmmc_token) return !sdmmc_fault;
    if (!sdmmc_api || !sdmmc_api->release(sdmmc_api->context, sdmmc_token)) {
        sdmmc_fault = true; fail("sdmmc close retained"); return false;
    }
    sdmmc_token = 0; sdmmc_fault = false; card_ready = false;
    return true;
}
static bool sdmmc_result(bool okay, const char *reason) {
    if (!okay) { sdmmc_fault = true; fail(reason); }
    return okay;
}
static bool read_sector(uint32_t lba, uint8_t out[512]) {
    if (sdmmc_api) {
        if (!out || !sdmmc_token || sdmmc_fault || (uint64_t)lba >= card_block_count) return false;
        return sdmmc_result(sdmmc_api->read(sdmmc_api->context, sdmmc_token, lba, 1u, out), "sdmmc read failed");
    }
    uint8_t response[6];
    if (!out || (!high_capacity && lba > UINT32_MAX / 512u) ||
        !command(17, high_capacity ? lba : lba * 512u, response, sizeof(response)) ||
        !response_for(17, response) || !wait_dat0(false, 131072u, 500u)) return false;
    if (!tick()) return false; /* Consume the DAT0 start bit before the first payload bit. */
    for (size_t byte = 0; byte < 512u; ++byte) {
        uint8_t value = 0;
        for (unsigned bit = 0; bit < 8u; ++bit) {
            value = (uint8_t)((value << 1) | (x4pro_pin_read(X4PRO_PIN_SD_DAT0) ? 1u : 0u));
            if (!tick()) return false;
        }
        out[byte] = value;
        if ((byte & 63u) == 63u) cooperate(64);
    }
    uint16_t received_crc = 0;
    for (unsigned bit = 0; bit < 16u; ++bit) {
        received_crc = (uint16_t)((received_crc << 1) |
                                  (x4pro_pin_read(X4PRO_PIN_SD_DAT0) ? 1u : 0u));
        if (!tick()) return false;
    }
    const bool stop = x4pro_pin_read(X4PRO_PIN_SD_DAT0);
    if (!tick()) return false;
    return stop && received_crc == x4pro_sd_crc16(out, 512u);
}
/* CMD9 R2 payload is CSD[127:0], following the 8-bit R2 header.
 * SD Physical Layer 5.3.2/5.3.3: capacity derives from CSD, never FatFs. */
static bool decode_csd(const uint8_t response[17], bool block_addressed, uint64_t *blocks) {
    const uint8_t *csd = response + 1;
    *blocks = 0;
    if (response[0] != 0x3fu || !(csd[15] & 1u) ||
        (csd[15] >> 1) != risc_sd_crc7(csd, 15)) return false;
    const unsigned version = csd[0] >> 6;
    const unsigned read_length = csd[5] & 15u;
    uint64_t count;
    if (version == 1u && block_addressed && read_length == 9u) {
        const uint32_t size = ((uint32_t)(csd[7] & 63u) << 16) |
                              ((uint32_t)csd[8] << 8) | csd[9];
        count = ((uint64_t)size + 1u) << 10;
    } else if (version == 0u && !block_addressed && read_length >= 9u && read_length <= 11u) {
        const uint32_t size = ((uint32_t)(csd[6] & 3u) << 10) |
                              ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        const unsigned multiplier = ((csd[9] & 3u) << 1) | (csd[10] >> 7);
        count = ((uint64_t)size + 1u) << (multiplier + 2u + read_length - 9u);
        if (count > (UINT64_C(1) << 23)) return false; /* 32-bit byte address. */
    } else return false; /* Unsupported SDUC or inconsistent OCR/CSD. */
    if (!count || count > (UINT64_C(1) << 32)) return false;
    *blocks = count;
    return true;
}
static bool init_card(void) {
    uint8_t response[17] = {0};
    if (gpio_fault || !close_sdmmc()) return false;
    high_capacity = false;
    card_block_count = 0;
    x4pro_pin_hold(X4PRO_PIN_SD_PWR, false);
    x4pro_pin_output(X4PRO_PIN_SD_PWR, true);
    if (clock_api) clock_api->sleep_ms(clock_api->context, 80);
    x4pro_pin_output(X4PRO_PIN_SD_PWR, false);
    if (clock_api) clock_api->sleep_ms(clock_api->context, 120);
    if (sdmmc_api) {
        /* The controller takes the same CLK/CMD/DAT0 pins, not GPIO5 power.
         * Retire static GPIO claims from startup/sleep before changing mux. */
        for (unsigned i = 1; i < 4; ++i) if (pins[i].token) {
            if (pins[i].held || !gpio_api->release(gpio_api->context, pins[i].token)) {
                gpio_fault = true; fail("sdmmc pin handoff failed"); return false;
            }
            pins[i].token = 0;
        }
        if (gpio_fault) return false;
        risc_sdmmc_card_info_v1 info = {0};
        const bool opened = sdmmc_api->open(sdmmc_api->context, X4PRO_PIN_SD_CLK,
            X4PRO_PIN_SD_CMD, X4PRO_PIN_SD_DAT0, RISC_SDMMC_MAX_HZ, &sdmmc_token, &info);
        if (!opened || !sdmmc_token || info.struct_size != sizeof(info) || info.reserved ||
            info.sector_size != 512u || !info.sector_count || info.sector_count > (UINT64_C(1) << 32) ||
            !info.clock_hz || info.clock_hz > RISC_SDMMC_MAX_HZ) {
            fail("sdmmc initialization failed");
            if (sdmmc_token) (void)close_sdmmc();
            return false; /* Never switch transport after a hardware failure. */
        }
        card_block_count = info.sector_count; card_ready = true;
        const bool okay = mount_filesystem();
        bootlog_mount_pending = okay;
        return okay;
    }
    x4pro_pin_release(X4PRO_PIN_SD_CMD);
    x4pro_pin_release(X4PRO_PIN_SD_DAT0);
    if (gpio_fault) return false;
    for (int i = 0; i < 80; ++i) if (!tick()) return false;
    if (!command(0, 0, response, 0)) { fail("CMD0 send failed"); return false; }
    if (!command(8, 0x1AAu, response, 6)) { fail("CMD8 no response"); return false; }
    if ((response[0] & 0x3fu) != 8u || response[3] != 1u || response[4] != 0xaau) {
        fail("CMD8 response invalid"); return false;
    }
    for (int i = 0; i < 200; ++i) {
        if (!command(55, 0, response, 6) || !command(41, 0x40100000u, response, 6)) {
            fail("ACMD41 failed"); return false;
        }
        if (response[1] & 0x80u) {
            high_capacity = (response[1] & 0x40u) != 0u;
            if (!command(2, 0, response, 17)) { fail("CMD2 failed"); return false; }
            if (!command(3, 0, response, 6) || !response_for(3, response)) {
                fail("CMD3 failed"); return false;
            }
            const uint32_t rca = ((uint32_t)response[1] << 24) | ((uint32_t)response[2] << 16);
            if (!rca || !command(9, rca, response, 17) ||
                !decode_csd(response, high_capacity, &card_block_count)) {
                fail("CMD9 CSD/capacity invalid"); return false;
            }
            if (!command(7, rca, response, 6) || !response_for(7, response) ||
                !wait_dat0(true, 131072u, 500u)) { fail("CMD7 select failed"); return false; }
            if (!high_capacity && (!command(16, 512u, response, 6) ||
                                   !response_for(16, response))) {
                fail("CMD16 block size failed"); return false;
            }
            card_rca = rca;
            card_ready = true;
            const bool okay = mount_filesystem();
            bootlog_mount_pending = okay;
            return okay;
        }
        if (clock_api) clock_api->sleep_ms(clock_api->context, 10);
    }
    fail("card idle");
    return false;
}

/* A native single-block write: CRC16, accepted data-response, busy release,
 * and card status must all succeed. Never retry an uncertain write. */
static bool write_sector(uint32_t lba, const uint8_t data[512]) {
    if (sdmmc_api) {
        if (!data || !sdmmc_token || sdmmc_fault || (uint64_t)lba >= card_block_count) return false;
        return sdmmc_result(sdmmc_api->write(sdmmc_api->context, sdmmc_token, lba, 1u, data), "sdmmc write failed");
    }
    uint8_t response[6];
    if (!data || (!high_capacity && lba > UINT32_MAX / 512u) ||
        !command(24, high_capacity ? lba : lba * 512u, response, 6) ||
        !response_for(24, response)) return false;
    const uint32_t status = ((uint32_t)response[1] << 24) | ((uint32_t)response[2] << 16) |
                            ((uint32_t)response[3] << 8) | response[4];
    if (status & 0xfdffe008u) return false;
    for (unsigned i = 0; i < 8; ++i) if (!tick()) return false;
    x4pro_pin_output(X4PRO_PIN_SD_DAT0, false); if (!tick()) return false;
    for (unsigned i = 0; i < 512; ++i) {
        for (int bit = 7; bit >= 0; --bit) {
            x4pro_pin_output(X4PRO_PIN_SD_DAT0, (data[i] >> bit) & 1u); if (!tick()) return false;
        }
        if ((i & 63u) == 63u) cooperate(64);
    }
    const uint16_t crc = x4pro_sd_crc16(data, 512);
    for (int bit = 15; bit >= 0; --bit) {
        x4pro_pin_output(X4PRO_PIN_SD_DAT0, (crc >> bit) & 1u); if (!tick()) return false;
    }
    x4pro_pin_output(X4PRO_PIN_SD_DAT0, true); if (!tick()) return false;
    x4pro_pin_release(X4PRO_PIN_SD_DAT0);
    if (!wait_dat0(false, 1024, 100)) return false;
    unsigned token = 0;
    for (unsigned i = 0; i < 5; ++i) {
        token = (token << 1) | (x4pro_pin_read(X4PRO_PIN_SD_DAT0) ? 1u : 0u); if (!tick()) return false;
    }
    if (token != 5u || !wait_dat0(true, 262144, 1000)) return false;
    if (!command(13, card_rca, response, 6) || !response_for(13, response)) return false;
    const uint32_t final_status = ((uint32_t)response[1] << 24) | ((uint32_t)response[2] << 16) |
                                  ((uint32_t)response[3] << 8) | response[4];
    return !(final_status & 0xfdffe008u);
}

static bool sync_card(void) {
    if (sdmmc_api) return !gpio_fault && sdmmc_token && !sdmmc_fault &&
        sdmmc_result(sdmmc_api->sync(sdmmc_api->context, sdmmc_token), "sdmmc sync failed");
    return !gpio_fault && wait_dat0(true, 262144, 1000);
}
/* All native one-bit transfers are synchronous. A failed GPIO transition is
 * uncertain transport state and cannot pass a power-down barrier. */
static bool transport_idle(void) { return !gpio_fault && !gpio_retained && !sdmmc_fault; }
static bool commit_sleep_rails(void) {
    if (!close_sdmmc()) return false;
    x4pro_pin_output(X4PRO_PIN_SD_CLK, false);
    x4pro_pin_input(X4PRO_PIN_SD_CMD, false);
    x4pro_pin_input(X4PRO_PIN_SD_DAT0, false);
    x4pro_pin_output(X4PRO_PIN_SD_PWR, true);
    x4pro_pin_hold(X4PRO_PIN_SD_PWR, true);
    return !gpio_fault;
}
static bool resume_sleep_media(void) {
    /* init_card releases the owned GPIO5 hold, cycles active-low power with
     * bounded settle delays, and reopens FatFs. No media is distinct from
     * uncertain rail/hold custody; the helper checks mounted/io_failed too. */
    (void)init_card();
    return !gpio_fault && !gpio_retained && !sdmmc_fault && pins[0].token && !pins[0].held;
}
#define STORAGE_VOLUME_TRY_COMMIT_POWER_DOWN commit_sleep_rails
#define STORAGE_VOLUME_TRY_RESUME_POWER_DOWN resume_sleep_media
#define STORAGE_VOLUME_SLEEP_UNSAFE() (valid_task() && (mutex_poisoned || gpio_fault || gpio_retained || sdmmc_fault))
#define STORAGE_VOLUME_EXTERNAL_GUARD
#define STORAGE_VOLUME_GUARD_ENTER guard_enter
#define STORAGE_VOLUME_GUARD_LEAVE guard_leave
#define STORAGE_VOLUME_ADMISSION_FROZEN() (export_state != EXPORT_LOCAL)
#define STORAGE_VOLUME_LABEL "X4PRO"
static uint32_t bootlog_budget_ms=15000,bootlog_sector_limit=2048;
#define STORAGE_VOLUME_OPERATION_BUDGET_MS bootlog_budget_ms
#define STORAGE_VOLUME_OPERATION_SECTOR_LIMIT bootlog_sector_limit
#include <volume.c>
#include "BootLog.h"
#include "Export.h"

/* Owner-task snapshot only. In particular, do not call valid_task(), ready(),
 * a guard or boot-log drain here: observation must remain safe after poison. */
static int32_t observe_state(void *context) {
    (void)context;
    if (mutex_poisoned || gpio_fault || gpio_retained || sdmmc_fault ||
        bootlog_retained || sleep_state == SLEEP_RETAINED || export_state == EXPORT_RETAINED)
        return RISC_STORAGE_STATE_RETAINED;
    /* The existing close contract retains a writer after failed media I/O.
     * Inspect the fixed in-memory handle slots only; never scan the card. */
    if (io_failed) for (unsigned i = 0; i < FILE_SLOTS; ++i)
        if (files[i].handle && (files[i].flags & RISC_STORAGE_OPEN_WRITE))
            return RISC_STORAGE_STATE_RETAINED;
    if (!started || !operation_mutex || quiescing || quiesced || power_down_prepared ||
        power_down_committed || sleep_state != SLEEP_ACTIVE || export_state != EXPORT_LOCAL ||
        !mounted || !card_ready || io_failed) return RISC_STORAGE_STATE_UNAVAILABLE;
    return RISC_STORAGE_STATE_READY;
}

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || operation_mutex || gpio_api || clock_api || sync_api || mutex_poisoned ||
        gpio_retained || !deps || count != 6) return false;
    const risc_hardware_device_v1 *hardware = NULL;
    const garden_gpio_v1 *gpio = NULL;
    const risc_platform_clock_api_v1 *clock = NULL;
    const risc_provider_sync_api_v1 *sync = NULL;
    const x4_power_ready_api_v1 *power = NULL;
    const risc_diagnostic_source_api_v1 *source = NULL;
    for (size_t i=0; i<count; ++i) {
        if (!deps[i].capability_id || deps[i].api_version != 1 || !deps[i].api) return false;
        const char *name = deps[i].capability_id;
        if (equal(name, "hardware.device") && !hardware) hardware = deps[i].api;
        else if (equal(name, "platform.gpio") && !gpio) gpio = deps[i].api;
        else if (equal(name, "platform.clock") && !clock) clock = deps[i].api;
        else if (equal(name, RISC_PROVIDER_SYNC_CAPABILITY) && !sync) sync = deps[i].api;
        else if (equal(name, RISC_DIAGNOSTIC_SOURCE_CAPABILITY) && !source) source = deps[i].api;
        else if (equal(name, X4_POWER_READY_CAPABILITY) && !power) power = deps[i].api;
        else return false;
    }
    if (!hardware || hardware->api_version != 1 || hardware->struct_size < sizeof(*hardware) ||
        !hardware->instance_id || !equal(hardware->compatible, "xteink,x4-pro-sd-native1") ||
        !equal(hardware->revision, "unspecified") || !equal(hardware->config_type, "gpio.bank") ||
        hardware->config_version != 1 || hardware->config_size != sizeof(risc_hw_gpio_bank_v1) || !hardware->config ||
        !gpio || gpio->api_version != 1 || gpio->struct_size < GARDEN_GPIO_DEEP_SLEEP_HOLD_V1_SIZE ||
        !gpio->claim || !gpio->read || !gpio->write || !gpio->release || !gpio->deep_sleep_hold ||
        !clock || clock->api_version != 1 || clock->struct_size < sizeof(*clock) || !clock->monotonic_ms || !clock->sleep_ms ||
        !sync || sync->api_version != 1 || sync->struct_size < sizeof(*sync) || !sync->is_owner ||
        !sync->create || !sync->try_lock || !sync->unlock || !sync->destroy ||
        !source || source->api_version != 1 || source->struct_size < sizeof(*source) || !source->read ||
        !power || power->api_version != 1 || power->struct_size < sizeof(*power) || !power->ready) return false;
    const risc_hw_gpio_bank_v1 *config = hardware->config;
    if (config->struct_size != sizeof(*config) || config->count != 4 || config->active_high != 1 ||
        config->pull_up != 1 || config->reserved || config->debounce_us || config->long_press_us || config->click_min_us) return false;
    for (unsigned i=0; i<RISC_HW_MAX_CHANNELS; ++i)
        if (config->pins[i] != (i<4 ? pin_numbers[i] : 0)) return false;
    if (!sync->is_owner(sync->context) || !power->ready(power->context)) return false;
    const risc_sdmmc_host_api_v1 *native_host = risc_gpio_sdmmc(gpio);
#if !X4PRO_SD_ALLOW_LEGACY_GPIO
    if (!native_host) { fail("hardware SDMMC host required"); return false; }
#endif
    gpio_api = gpio; sdmmc_api = native_host; clock_api = clock; sync_api = sync; diagnostic_source = source;
    quiescing = quiesced = gpio_fault = false;
    if (!sync_api->create(sync_api->context, &operation_mutex) || !operation_mutex) {
        gpio_api = NULL; sdmmc_api = NULL; clock_api = NULL; sync_api = NULL; diagnostic_source = NULL; return false;
    }
    if (!enter_lifecycle()) return false;
    x4pro_pin_output(X4PRO_PIN_SD_CLK, false);
    power_down_prepared = power_down_committed = false;
    sleep_state = SLEEP_ACTIVE;
    started = true;
    mounted = card_ready = io_failed = false;
    error[0] = 0;
    (void)init_card(); /* An absent card preserves refresh capability. */
    if (gpio_fault || sdmmc_fault) { io_failed = true; mounted = false; }
    const bool okay = !gpio_fault && !sdmmc_fault;
    return leave() && okay;
}
static bool release_pins(void) {
    if (!close_sdmmc()) return false;
    /* Cleanup is allowed after a transport fault. Establish safe static rails
     * before releasing anything; a failed write retains every owned token. */
    if (pins[0].token && (pins[0].held || !gpio_api->write(gpio_api->context, pins[0].token, true))) return false;
    if (pins[1].token && !gpio_api->write(gpio_api->context, pins[1].token, false)) return false;
    for (unsigned i=0; i<4; ++i) if (pins[i].token) {
        if (pins[i].held || !gpio_api->release(gpio_api->context, pins[i].token)) return false;
        pins[i].token = 0;
    }
    return true;
}
static bool quiesce(void) {
    if (mutex_poisoned || gpio_retained) return false;
    if (!operation_mutex) return !gpio_api && !clock_api && !sync_api;
    if (!valid_task()) return false;
    if (!quiescing) {
        if (!enter_lifecycle()) return false;
        if (has_handles() || export_state != EXPORT_LOCAL || power_down_committed || sleep_state != SLEEP_ACTIVE) { (void)leave(); return false; }
        /* Shutdown failures preserve exact remaining tokens and dependencies. */
        (void)f_mount(NULL, "", 0);
        mounted = card_ready = false;
        started = false;
        if (gpio_retained || !release_pins()) {
            gpio_fault = io_failed = true;
            (void)leave(); return false;
        }
        quiescing = true;
        if (!leave()) return false;
    }
    /* destroy may fail without losing the unlocked token; retry this same
     * terminal cleanup, with ordinary API admission fenced throughout. */
    if (!sync_api->destroy(sync_api->context, operation_mutex)) return false;
    operation_mutex = 0; quiesced = true;
    gpio_api = NULL; sdmmc_api = NULL; clock_api = NULL; sync_api = NULL; diagnostic_source = NULL;
    power_down_prepared = false;
    return true;
}
static void stop(void) { /* Successful quiesce has completed all fallible work. */ }
static const risc_driver_service_v2 driver = {{{{
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), "x4pro-sd",
    "storage.volume", 1, &filesystem_api, start, stop, quiesce
}, bootlog_descriptor_error, NULL}, NULL}, RISC_DRIVER_SERVICE_TAG_V1,
RISC_DRIVER_SERVICE_VERSION_V1, bootlog_service};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return NULL;
    if (!logging_api.prepared.base.sleep.terminal.power.volume.base.api_version) {
        logging_api.prepared.base.sleep = api;
        logging_api.prepared.base.sleep.terminal.power.volume.base.struct_size = sizeof(filesystem_api);
        logging_api.prepared.base.sleep.terminal.power.volume.base.last_error = bootlog_last_error;
        logging_api.prepared.base.export_tag = RISC_STORAGE_EXPORT_TAG;
        logging_api.prepared.base.export_version = 1u;
        logging_api.prepared.base.export_begin = export_begin;
        logging_api.prepared.base.export_read = export_read;
        logging_api.prepared.base.export_write = export_write;
        logging_api.prepared.base.export_sync = export_sync;
        logging_api.prepared.base.export_end = export_end;
        logging_api.prepared.prepare_tag = RISC_STORAGE_EXPORT_PREPARE_TAG;
        logging_api.prepared.prepare_version = 1u;
        logging_api.prepared.begin_prepare = export_begin_prepare;
        logging_api.prepared.prepare_step = export_prepare_step;
        logging_api.state_tag = RISC_STORAGE_STATE_TAG;
        logging_api.state_version = 1u;
        logging_api.observe = observe_state;
        filesystem_api.fs_tag=RISC_STORAGE_FS_TAG;filesystem_api.fs_version=1;
        filesystem_api.dir_tell=fs_dir_tell;filesystem_api.dir_seek=fs_dir_seek;
        filesystem_api.metadata=fs_metadata;filesystem_api.file_truncate=fs_truncate;
        filesystem_api.replace_file=fs_replace_file;filesystem_api.recover_replace=fs_recover_replace;
    }
    return &driver.poll.streams.driver;
}
