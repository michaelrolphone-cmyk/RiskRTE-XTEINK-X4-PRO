#include "RiscPlatformClockV1.h"
#include "RiscStorageVolumeV1.h"
#include <stdio.h>
#include <string.h>

unsigned x4_sd_fake_ticks;
bool x4_sd_fake_bad_pin;
static unsigned slept_ms;
uint32_t x4pro_sd_test_cycle_count(void) { static uint32_t cycles; return cycles += 8; }
static void sleep_ms(void *context, uint32_t ms) { (void)context; slept_ms += ms; }
static uint64_t monotonic_ms(void *context) { (void)context; return slept_ms; }
static int expect(bool condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}
int main(void) {
    int failures = 0;
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    failures += expect(driver && driver->capability, "driver ABI");
    if (failures) return 1;
    const risc_storage_volume_api_v1 *volume = driver->capability;
    const risc_platform_clock_api_v1 clock = {
        RISC_PLATFORM_CLOCK_API_V1, sizeof(clock), 0, monotonic_ms, sleep_ms
    };
    const risc_provider_dependency_v1 dependency = {"platform.clock", 1, &clock};
    failures += expect(driver->start(&dependency, 1), "missing card keeps provider available");
    failures += expect(!volume->ready(volume->context), "missing card is not ready");
    char error[80] = {0};
    failures += expect(volume->last_error(volume->context, error, sizeof(error)) &&
                       strcmp(error, "CMD8 no response") == 0, "bounded native CMD8 absence");
    failures += expect(volume->refresh(volume->context), "refresh serviced without media");
    failures += expect(!volume->ready(volume->context), "refresh does not fabricate media");
    failures += expect(volume->file_open_read(volume->context, "/book.epub", 0) ==
                       RISC_STORAGE_FILE_INVALID, "no file handle without filesystem");
    risc_storage_dirent_v1 entry;
    uint64_t size = 0;
    bool is_directory = false;
    failures += expect(volume->dir_open(volume->context, "/") == RISC_STORAGE_DIR_INVALID &&
                       !volume->dir_next(volume->context, 1u, &entry) &&
                       !volume->stat(volume->context, "/", &size, &is_directory),
                       "no directory or stat without media");
    failures += expect(slept_ms == 400, "bounded board-proven power settle sleeps");
    failures += expect(x4_sd_fake_ticks > 0 && x4_sd_fake_ticks < 1200, "bounded clock traffic");
    failures += expect(!x4_sd_fake_bad_pin, "SD provider owns only assigned pins");
    driver->stop();
    failures += expect(!volume->ready(volume->context), "stop invalidates readiness");
    if (failures) return 1;
    puts("x4 SD absent: PASS");
    return 0;
}
