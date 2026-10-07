/* X4 native one-bit SD transport and provider-owned FatFs filesystem.
 * No firmware filesystem, SDMMC or SPI imports; owns GPIO 5/40/41/42. */
#include "RiscPlatformClockV1.h"
#include "RiscProviderV2.h"
#include "RiscStorageVolumeV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include "x4pro_proto.h"
#include "../x4pro_i2c/os_cpu_v1.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const risc_platform_clock_api_v1 *clock_api;
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
        if ((uint32_t)(cycle_count() - began) >= SELECTED_PHASE_CYCLES) return true;
    return false; /* Stuck read-back or counter must never hang the SD owner. */
}
static bool tick(void) {
    /* Preserve identification-mode timing and setup until the card is selected. */
    if (card_ready) {
        x4pro_pin_level(X4PRO_PIN_SD_CLK, true);
        if (!selected_phase(true)) return false;
        x4pro_pin_level(X4PRO_PIN_SD_CLK, false);
        return selected_phase(false);
    }
    x4pro_pin_output(X4PRO_PIN_SD_CLK, true);
    x4pro_pin_output(X4PRO_PIN_SD_CLK, false);
    return true;
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
    high_capacity = false;
    x4pro_pin_hold(X4PRO_PIN_SD_PWR, false);
    x4pro_pin_output(X4PRO_PIN_SD_PWR, true);
    if (clock_api) clock_api->sleep_ms(clock_api->context, 80);
    x4pro_pin_output(X4PRO_PIN_SD_PWR, false);
    if (clock_api) clock_api->sleep_ms(clock_api->context, 120);
    x4pro_pin_release(X4PRO_PIN_SD_CMD);
    x4pro_pin_release(X4PRO_PIN_SD_DAT0);
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

static bool sync_card(void) { return wait_dat0(true, 262144, 1000); }
// All native one-bit transfers are synchronous; failed I/O stays quarantined.
static bool transport_idle(void) { return true; }
static void commit_sleep_rails(void) {
    // Only called after the existing barrier froze all handles and synced the
    // card. Keep read metadata and this ELF pinned until the deep-sleep reset.
    x4pro_pin_output(X4PRO_PIN_SD_CLK, false);
    x4pro_pin_input(X4PRO_PIN_SD_CMD, false);
    x4pro_pin_input(X4PRO_PIN_SD_DAT0, false);
    x4pro_pin_output(X4PRO_PIN_SD_PWR, true);
    x4pro_pin_hold(X4PRO_PIN_SD_PWR, true);
}
#define STORAGE_VOLUME_COMMIT_POWER_DOWN commit_sleep_rails
#define STORAGE_VOLUME_OS_CPU_MUTEX
#define STORAGE_VOLUME_LABEL "X4PRO"
#include "../storage_fatfs/volume.c"

static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    /* ModuleV2 serializes initial start/restart before publishing consumers. */
    if (!valid_task() || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return false;
    if (!operation_mutex) {
        operation_mutex = xQueueCreateMutex(1); /* queueQUEUE_TYPE_MUTEX */
        if (!operation_mutex) return false; /* No hardware before admission. */
    }
    if (__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&quiesced, false, __ATOMIC_RELEASE);
        __atomic_store_n(&quiescing, false, __ATOMIC_RELEASE);
    }
    if (!enter_lifecycle()) return false;
    bool okay = false;
    if (started || has_handles() || !dependencies) goto done;
    clock_api = 0;
    for (size_t i = 0; i < count; ++i)
        if (equal(dependencies[i].capability_id, "platform.clock") && dependencies[i].api_version == 1)
            clock_api = dependencies[i].api;
    if (!clock_api || clock_api->api_version != RISC_PLATFORM_CLOCK_API_V1 ||
        clock_api->struct_size < sizeof(*clock_api) || !clock_api->monotonic_ms ||
        !clock_api->sleep_ms) { clock_api = 0; fail("platform.clock missing or invalid"); goto done; }
    x4pro_pin_output(X4PRO_PIN_SD_CLK, false);
    power_down_prepared = power_down_committed = false;
    started = true;
    mounted = card_ready = io_failed = false;
    error[0] = 0;
    operation_start = clock_api->monotonic_ms(clock_api->context);
    (void)init_card(); /* An absent card does not remove the refresh capability. */
    okay = true;
done:
    return leave() && okay;
}
static void stop_locked(void) {
    /* Refuse unsafe unload: borrowers or unsynced failed writers retain module. */
    if (has_handles()) { fail("storage handles still live"); return; }
    (void)f_mount(0, "", 0);
    if (started) {
        x4pro_pin_output(X4PRO_PIN_SD_PWR, true);
        x4pro_pin_output(X4PRO_PIN_SD_CLK, false);
        x4pro_pin_release(X4PRO_PIN_SD_CMD);
        x4pro_pin_release(X4PRO_PIN_SD_DAT0);
    }
    started = card_ready = mounted = power_down_prepared = false;
}
static bool quiesce(void) {
    if (!valid_task() || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return false;
    if (!operation_mutex || __atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) return true;
    if (!enter_lifecycle()) return false;
    const bool safe = !has_handles();
    if (safe) {
        stop_locked();
        clock_api = 0;
        /* Fence admission before giving, but do not publish acceptance yet.
         * A concurrent lifecycle caller must not mistake this in-flight give
         * for completed quiescence and delete a still-owned mutex. */
        __atomic_store_n(&quiescing, true, __ATOMIC_RELEASE);
    }
    if (!leave() || !safe) return false;
    __atomic_store_n(&quiesced, true, __ATOMIC_RELEASE);
    return true;
}
static void stop(void) {
    if (!valid_task() || !operation_mutex || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return;
    if (!__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE) && !quiesce()) return;
    /* The runtime drains consumers before accepted quiesce and then unmaps.
     * No second take/give or fallible transition after that acceptance. */
    x4_cpu_mutex retired = operation_mutex;
    operation_mutex = 0;
    vQueueDelete(retired);
}
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), "x4pro-sd",
    "storage.volume", 1, &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : 0;
}
