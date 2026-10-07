/* X4 Pro dual frontlight: active-high GPIO8/9, 25 kHz, 10-bit PWM.
 * Board/profile and ESP-IDF 4.4.7 register sources: docs/X4_FRONTLIGHT.md.
 * This ELF exclusively owns low-speed timer0/channels0+1 and these two pads.
 * No firmware LEDC API, peripheral-wide reset, ISR, task or software PWM.
 */
#include "RiscFrontlightV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include "../x4pro_i2c/os_cpu_v1.h"
#include <stddef.h>

#define LEDC_BASE 0x60019000u
#define SYSTEM_CLOCK 0x600c0018u
#define SYSTEM_RESET 0x600c0020u
#define LEDC_CLOCK_BIT (1u << 11)
#define TIMER0 (LEDC_BASE + 0xa0u)
#define CLOCK_SELECT (LEDC_BASE + 0xd0u)
#define CHANNEL(c) (LEDC_BASE + (c) * 0x14u)
#define CHANNEL_UPDATE (1u << 4)
#define CHANNEL_ENABLE (1u << 2)
#define TIMER_UPDATE (1u << 25)
#define DUTY_FULL 1024u

static bool started;
/* Provider BSS can live in PSRAM. Reuse the RTOS's RAM-aware synchronization,
 * not a bare compare-and-set flag that emits PSRAM-unsafe S32C1I. */
static x4_cpu_mutex operation_mutex;
static x4_cpu_task operation_owner;
static bool mutex_poisoned, admission_closed, quiesced;

static bool valid_task(void) { return !xPortInIsrContext() && xTaskGetCurrentTaskHandle() != 0; }
static bool enter(void) {
    if (!valid_task() || !operation_mutex || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&admission_closed, __ATOMIC_ACQUIRE)) return false;
    /* Nonrecursive, zero wait: neither reentry nor another task may spin. */
    if (xQueueSemaphoreTake(operation_mutex, 0) != 1) return false;
    if (__atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&admission_closed, __ATOMIC_ACQUIRE)) {
        if (xQueueGenericSend(operation_mutex, 0, 0, 0) != 1)
            __atomic_store_n(&mutex_poisoned, true, __ATOMIC_RELEASE);
        return false;
    }
    operation_owner = xTaskGetCurrentTaskHandle();
    return true;
}
static bool leave(void) {
    if (!valid_task() || operation_owner != xTaskGetCurrentTaskHandle()) {
        __atomic_store_n(&mutex_poisoned, true, __ATOMIC_RELEASE);
        return false;
    }
    operation_owner = 0;
    if (xQueueGenericSend(operation_mutex, 0, 0, 0) != 1) {
        /* Retain this mutex and provider generation on uncertain ownership.
         * Never delete it or claim safe quiescence after a failed give. */
        __atomic_store_n(&mutex_poisoned, true, __ATOMIC_RELEASE);
        return false;
    }
    return true;
}
static uint16_t duty;
static uint32_t timer_config;
static uint32_t clock_source;
static const uint32_t light_pins[] = {X4PRO_PIN_LIGHT_COOL, X4PRO_PIN_LIGHT_WARM};

static void pins_off(void) {
    for (unsigned c = 0; c < 2; ++c) {
        // Program LOW before releasing a retained sleep hold: no bright flash.
        x4pro_pin_output(light_pins[c], false);
        x4pro_pin_hold(light_pins[c], false);
        x4pro_pin_hold(light_pins[c], true); // Keep LOW through deep sleep/reset.
    }
}

static bool configure(void) {
    const uint32_t clock = x4pro_reg_read(CLOCK_SELECT) & 3u;
    // Do not retime an existing user, or guess the calibrated RTC source.
    if (clock == 2u) return false;
    for (unsigned c = 0; c < 8; ++c) {
        const uint32_t config = x4pro_reg_read(CHANNEL(c));
        if ((config & CHANNEL_ENABLE) && (c < 2 || !(config & 3u) || !clock)) return false;
    }
    // Scan for conflicts before enabling the peripheral. Never reset LEDC.
    x4pro_reg_write(SYSTEM_CLOCK, x4pro_reg_read(SYSTEM_CLOCK) | LEDC_CLOCK_BIT);
    x4pro_reg_write(SYSTEM_RESET, x4pro_reg_read(SYSTEM_RESET) & ~LEDC_CLOCK_BIT);
    clock_source = clock ? clock : 3u; // Prefer fixed 40 MHz XTAL when unused.
    if (!clock) x4pro_reg_write(CLOCK_SELECT, (x4pro_reg_read(CLOCK_SELECT) & ~3u) | clock_source);
    // 8 fractional divider bits: sourceHz * 256 / (25000 * 1024).
    // X4 active/idle CPU profiles keep APB at 80 MHz; XTAL is 40 MHz.
    timer_config = ((clock_source == 1u ? 800u : 400u) << 4) | 10u;
    pins_off();
    for (unsigned c = 0; c < 2; ++c) {
        x4pro_reg_write(CHANNEL(c), CHANNEL_UPDATE); // timer0, disabled, idle LOW
        x4pro_reg_write(CHANNEL(c) + 4u, 0); // hpoint
        x4pro_reg_write(CHANNEL(c) + 8u, 0); // duty
        x4pro_reg_write(CHANNEL(c) + 0xcu, 0); // no fade/start
    }
    x4pro_reg_write(TIMER0, timer_config | (1u << 23));
    x4pro_reg_write(TIMER0, timer_config | TIMER_UPDATE);
    return true;
}

static bool configured(void) {
    if (!(x4pro_reg_read(SYSTEM_CLOCK) & LEDC_CLOCK_BIT) ||
        (x4pro_reg_read(SYSTEM_RESET) & LEDC_CLOCK_BIT) ||
        (x4pro_reg_read(CLOCK_SELECT) & 3u) != clock_source ||
        (x4pro_reg_read(TIMER0) & 0x1ffffffu) != timer_config) return false;
    for (unsigned c = 0; c < 2; ++c) {
        // Never recover by rewriting a channel/timer that another owner took.
        if ((x4pro_reg_read(CHANNEL(c)) & 3u) != 0) return false;
    }
    return true;
}

static uint16_t ratio_to_duty(uint16_t requested, uint16_t maximum) {
    if (!requested) return 0;
    if (requested == maximum) return DUTY_FULL;
    uint32_t value = ((uint32_t)requested * DUTY_FULL + maximum / 2u) / maximum;
    // Sub-count requests remain lit; exact full-on is reserved for maximum.
    if (!value) value = 1;
    if (value >= DUTY_FULL) value = DUTY_FULL - 1u;
    return (uint16_t)value;
}

static bool set_level(void *context, uint16_t requested, uint16_t maximum) {
    (void)context;
    if (!maximum || requested > maximum || !enter()) return false;
    if (!started) { (void)leave(); return false; }
    if (!configured()) {
        // Fail dark without stealing back a changed timer/channel.
        pins_off(); duty = 0;
        (void)leave(); return false;
    }
    const uint16_t next = ratio_to_duty(requested, maximum);
    if (!next) pins_off(); // Exact GPIO-low endpoint, including sleep entry.
    for (unsigned c = 0; c < 2; ++c) {
        x4pro_reg_write(CHANNEL(c) + 8u, (uint32_t)next << 4);
        // Immediate single update, increment direction, zero fade scale.
        x4pro_reg_write(CHANNEL(c) + 0xcu, (1u << 31) | (1u << 30) | (1u << 20) | (1u << 10));
        x4pro_reg_write(CHANNEL(c), CHANNEL_UPDATE | (next ? CHANNEL_ENABLE : 0u));
        if (next) {
            x4pro_pin_hold(light_pins[c], false);
            x4pro_reg_write(X4PRO_GPIO_MATRIX_BASE + light_pins[c] * 4u, 73u + c);
        }
    }
    duty = next;
    return leave();
}

static bool get_level(void *context, uint16_t *out, uint16_t *maximum) {
    (void)context;
    if (!out || !maximum || !enter()) return false;
    const bool valid = started && configured();
    const uint16_t value = duty;
    if (!leave() || !valid) return false;
    *out = value; *maximum = DUTY_FULL;
    return true;
}

static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    (void)dependencies;
    /* ModuleV2 serializes initial/restart callbacks before publishing clients. */
    if (count || !valid_task() || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return false;
    if (!operation_mutex) {
        operation_mutex = xQueueCreateMutex(1); // queueQUEUE_TYPE_MUTEX
        if (!operation_mutex) return false; // No hardware before admission.
    }
    if (__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&quiesced, false, __ATOMIC_RELEASE);
        __atomic_store_n(&admission_closed, false, __ATOMIC_RELEASE);
    }
    if (!enter()) return false;
    const bool ready = !started && configure();
    if (ready) { duty = 0; started = true; }
    return leave() && ready;
}

static bool quiesce(void) {
    if (!valid_task() || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return false;
    if (!operation_mutex || __atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) return true;
    if (!enter()) return false;
    if (started) {
        pins_off();
        if (configured()) {
            for (unsigned c = 0; c < 2; ++c) {
                x4pro_reg_write(CHANNEL(c), CHANNEL_UPDATE);
                x4pro_reg_write(CHANNEL(c) + 0xcu, 0);
            }
        }
    }
    started = false; duty = 0;
    /* Close admission before the final give, publish acceptance only after it.
     * A concurrent stop must not delete a mutex whose give is still running. */
    __atomic_store_n(&admission_closed, true, __ATOMIC_RELEASE);
    if (!leave()) return false;
    __atomic_store_n(&quiesced, true, __ATOMIC_RELEASE);
    return true;
}
static void stop(void) {
    if (!valid_task() || !operation_mutex || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return;
    if (!__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE) && !quiesce()) return;
    /* The executor revoked/drained callbacks before accepted quiescence.
     * No fallible second take/give follows that acceptance. */
    x4_cpu_mutex retired = operation_mutex;
    operation_mutex = 0;
    vQueueDelete(retired);
}
static const risc_frontlight_api_v1 api = {
    RISC_FRONTLIGHT_API_V1, sizeof(api), 0, set_level, get_level
};
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), "x4pro-frontlight",
    "display.frontlight", 1, &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver : NULL;
}
