/* X4 native one-bit CLK/CMD/DAT0 SD transport, ordinary provider ABI2.
 * Protocol derived from Drivers/x4pro_sd/driver.c at Reader 34d8e694.
 * Filesystem implementation remains shared in Reader storage_fatfs/volume.c.
 * GPIO authority and synchronization are scoped to this hardware.device. */
#include <RiscPlatformClockV1.h>
#include <RiscProviderV2.h>
#include <RiscProviderSyncV1.h>
#include <RiscStorageVolumeV1.h>
#include <GardenPlatformV1.h>
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
static const risc_provider_sync_api_v1 *sync_api;
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
    if (!valid_task() || !sync_api->unlock(sync_api->context, operation_mutex)) {
        mutex_poisoned = true; return false;
    }
    return true;
}
static bool started, high_capacity;
static char error[80];
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
static bool read_sector(uint32_t lba, uint8_t out[512]) {
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
static bool init_card(void) {
    uint8_t response[17] = {0};
    if (gpio_fault) return false;
    high_capacity = false;
    x4pro_pin_hold(X4PRO_PIN_SD_PWR, false);
    x4pro_pin_output(X4PRO_PIN_SD_PWR, true);
    if (clock_api) clock_api->sleep_ms(clock_api->context, 80);
    x4pro_pin_output(X4PRO_PIN_SD_PWR, false);
    if (clock_api) clock_api->sleep_ms(clock_api->context, 120);
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
            if (!rca || !command(7, rca, response, 6) || !response_for(7, response) ||
                !wait_dat0(true, 131072u, 500u)) { fail("CMD7 select failed"); return false; }
            if (!high_capacity && (!command(16, 512u, response, 6) ||
                                   !response_for(16, response))) {
                fail("CMD16 block size failed"); return false;
            }
            card_rca = rca;
            card_ready = true;
            return mount_filesystem();
        }
        if (clock_api) clock_api->sleep_ms(clock_api->context, 10);
    }
    fail("card idle");
    return false;
}

/* A native single-block write: CRC16, accepted data-response, busy release,
 * and card status must all succeed. Never retry an uncertain write. */
static bool write_sector(uint32_t lba, const uint8_t data[512]) {
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

static bool sync_card(void) { return !gpio_fault && wait_dat0(true, 262144, 1000); }
/* All native one-bit transfers are synchronous. A failed GPIO transition is
 * uncertain transport state and cannot pass a power-down barrier. */
static bool transport_idle(void) { return !gpio_fault && !gpio_retained; }
static bool commit_sleep_rails(void) {
    x4pro_pin_output(X4PRO_PIN_SD_CLK, false);
    x4pro_pin_input(X4PRO_PIN_SD_CMD, false);
    x4pro_pin_input(X4PRO_PIN_SD_DAT0, false);
    x4pro_pin_output(X4PRO_PIN_SD_PWR, true);
    x4pro_pin_hold(X4PRO_PIN_SD_PWR, true);
    return !gpio_fault;
}
#define STORAGE_VOLUME_TRY_COMMIT_POWER_DOWN commit_sleep_rails
#define STORAGE_VOLUME_EXTERNAL_GUARD
#define STORAGE_VOLUME_GUARD_ENTER guard_enter
#define STORAGE_VOLUME_GUARD_LEAVE guard_leave
#define STORAGE_VOLUME_LABEL "X4PRO"
#include <volume.c>

static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    if (started || operation_mutex || gpio_api || clock_api || sync_api || mutex_poisoned ||
        gpio_retained || !deps || count != 5) return false;
    const risc_hardware_device_v1 *hardware = NULL;
    const garden_gpio_v1 *gpio = NULL;
    const risc_platform_clock_api_v1 *clock = NULL;
    const risc_provider_sync_api_v1 *sync = NULL;
    const x4_power_ready_api_v1 *power = NULL;
    for (size_t i=0; i<count; ++i) {
        if (!deps[i].capability_id || deps[i].api_version != 1 || !deps[i].api) return false;
        const char *name = deps[i].capability_id;
        if (equal(name, "hardware.device") && !hardware) hardware = deps[i].api;
        else if (equal(name, "platform.gpio") && !gpio) gpio = deps[i].api;
        else if (equal(name, "platform.clock") && !clock) clock = deps[i].api;
        else if (equal(name, RISC_PROVIDER_SYNC_CAPABILITY) && !sync) sync = deps[i].api;
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
        !power || power->api_version != 1 || power->struct_size < sizeof(*power) || !power->ready) return false;
    const risc_hw_gpio_bank_v1 *config = hardware->config;
    if (config->struct_size != sizeof(*config) || config->count != 4 || config->active_high != 1 ||
        config->pull_up != 1 || config->reserved || config->debounce_us || config->long_press_us || config->click_min_us) return false;
    for (unsigned i=0; i<RISC_HW_MAX_CHANNELS; ++i)
        if (config->pins[i] != (i<4 ? pin_numbers[i] : 0)) return false;
    if (!sync->is_owner(sync->context) || !power->ready(power->context)) return false;
    gpio_api = gpio; clock_api = clock; sync_api = sync;
    quiescing = quiesced = gpio_fault = false;
    if (!sync_api->create(sync_api->context, &operation_mutex) || !operation_mutex) {
        gpio_api = NULL; clock_api = NULL; sync_api = NULL; return false;
    }
    if (!enter_lifecycle()) return false;
    x4pro_pin_output(X4PRO_PIN_SD_CLK, false);
    power_down_prepared = power_down_committed = false;
    started = true;
    mounted = card_ready = io_failed = false;
    error[0] = 0;
    (void)init_card(); /* An absent card preserves refresh capability. */
    if (gpio_fault) { io_failed = true; mounted = false; }
    const bool okay = !gpio_fault;
    return leave() && okay;
}
static bool release_pins(void) {
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
        if (has_handles() || power_down_committed) { (void)leave(); return false; }
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
    gpio_api = NULL; clock_api = NULL; sync_api = NULL;
    power_down_prepared = false;
    return true;
}
static void stop(void) { /* Successful quiesce has completed all fallible work. */ }
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), "x4pro-sd",
    "storage.volume", 1, &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}
