#pragma once
/* Optional hardware one-bit SDMMC service on an existing scoped GPIO table.
 * Filesystem, power policy and host-export ownership remain in the ELF driver.
 * This tagged extension does not change garden_gpio_v1 or its legacy layout. */
#include "GardenPlatformV1.h"
#include <stddef.h>
#include <stdint.h>

#define RISC_GPIO_SDMMC_TAG_V1 0x53444D31u
#define RISC_SDMMC_SECTOR_BYTES 512u
#define RISC_SDMMC_MAX_SECTORS 8u
#define RISC_SDMMC_MAX_HZ 20000000u

typedef struct {
    uint32_t struct_size, sector_size;
    uint64_t sector_count;
    uint32_t clock_hz, reserved;
} risc_sdmmc_card_info_v1;

typedef struct {
    uint32_t api_version, struct_size;
    void *context;
    /* Pins must be distinct and inside this GPIO scope. No power pin is
     * driven here. A nonzero token on false means cleanup is still required;
     * keep it and call release. A successful open copies all card metadata. */
    bool (*open)(void *, uint8_t clk, uint8_t cmd, uint8_t dat0, uint32_t max_hz,
                 uint64_t *token, risc_sdmmc_card_info_v1 *info);
    /* Synchronous copied transfers, 1..8 sectors. Unaligned/internal/PSRAM
     * buffers are supported. No caller buffer is borrowed after return.
     * Failed transfers permit cleanup only; never retry an uncertain write. */
    bool (*read)(void *, uint64_t token, uint64_t lba, uint32_t count, void *out);
    bool (*write)(void *, uint64_t token, uint64_t lba, uint32_t count, const void *data);
    bool (*sync)(void *, uint64_t token);
    /* Stops/deinitializes the controller before retiring the pins and token.
     * False retains ownership, including after a failed open or transfer. */
    bool (*release)(void *, uint64_t token);
} risc_sdmmc_host_api_v1;

typedef struct {
    garden_gpio_v1 base;
    uint32_t sdmmc_tag, sdmmc_version;
    risc_sdmmc_host_api_v1 sdmmc;
} risc_gpio_sdmmc_api_v1;

static inline const risc_sdmmc_host_api_v1 *risc_gpio_sdmmc(const garden_gpio_v1 *gpio) {
    if (!gpio || gpio->api_version != 1 || gpio->struct_size < sizeof(risc_gpio_sdmmc_api_v1)) return NULL;
    const risc_gpio_sdmmc_api_v1 *ext = (const risc_gpio_sdmmc_api_v1 *)gpio;
    const risc_sdmmc_host_api_v1 *api = &ext->sdmmc;
    if (ext->sdmmc_tag != RISC_GPIO_SDMMC_TAG_V1 || ext->sdmmc_version != 1 ||
        api->api_version != 1 || api->struct_size < sizeof(*api) ||
        !api->open || !api->read || !api->write || !api->sync || !api->release) return NULL;
    return api;
}
