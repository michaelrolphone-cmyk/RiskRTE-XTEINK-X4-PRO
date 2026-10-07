/* X4 Pro I2C bus. Owns GPIO 39/38; consumers use only i2c.bus v1.
 * A canonical OS/CPU ABI1 mutex covers claims, whole transactions and
 * lifecycle. Transactions serialize within their caller-supplied total
 * timeout; lifecycle operations remain fail-fast. The pinned RTOS owns
 * RAM-aware synchronization; ELF BSS
 * may be in PSRAM and must not use bare compare-and-set instructions.
 * No raw firmware I2C import, custom spinlock or recursive admission. */
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include "os_cpu_v1.h"
#include <stddef.h>
#include <stdint.h>

#define MAX_CLAIMS 8u
#define MAX_TRANSFER_BYTES 256u
#define MAX_TIMEOUT_MS 1000u
#define COOPERATE_BYTES 8u
#define COOPERATE_MS 2u
static const risc_platform_clock_api_v1 *clock_api;
static uint64_t tokens[MAX_CLAIMS];
static uint8_t addresses[MAX_CLAIMS];
static uint64_t next_token = 1;
static bool started, unsafe_bus;
static x4_cpu_mutex operation_mutex;
static x4_cpu_task operation_owner;
static bool mutex_poisoned, admission_closed, quiesced;
static char last_error_text[64];
static uint64_t began_ms, last_ms, deadline_ms, yielded_ms;
static unsigned bytes_since_yield;

static bool valid_task(void) { return !xPortInIsrContext() && xTaskGetCurrentTaskHandle() != 0; }
static bool enter(void) {
    if (!valid_task() || !operation_mutex || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&admission_closed, __ATOMIC_ACQUIRE)) return false;
    /* Lifecycle/claim paths are deliberately fail-fast. */
    if (xQueueSemaphoreTake(operation_mutex, 0) != 1) return false;
    if (__atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&admission_closed, __ATOMIC_ACQUIRE)) {
        /* A failed ownership transition may have raced this take. Never enter
         * device state; releasing this temporary take cannot clear poison. */
        if (xQueueGenericSend(operation_mutex, 0, 0, 0) != 1)
            __atomic_store_n(&mutex_poisoned, true, __ATOMIC_RELEASE);
        return false;
    }
    __atomic_store_n(&operation_owner, xTaskGetCurrentTaskHandle(), __ATOMIC_RELEASE);
    return true;
}
/* Transactions are allowed to wait for another legitimate bus user, but only
 * inside the same absolute timeout that also covers the physical transfer.
 * This prevents GT911 polling from turning a coincident CW2017 sample into a
 * synthetic I2C failure. Recursive entry remains an immediate refusal. */
static bool enter_transaction(uint32_t timeout_ms, uint64_t *began, uint64_t *deadline) {
    if (!began || !deadline || !timeout_ms || !valid_task() || !operation_mutex || !clock_api ||
        __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&admission_closed, __ATOMIC_ACQUIRE)) return false;
    const x4_cpu_task current = xTaskGetCurrentTaskHandle();
    if (__atomic_load_n(&operation_owner, __ATOMIC_ACQUIRE) == current) return false;

    uint64_t previous = clock_api->monotonic_ms(clock_api->context);
    if (previous == UINT64_MAX || previous > UINT64_MAX - timeout_ms) return false;
    const uint64_t limit = previous + timeout_ms;
    *began = previous;
    *deadline = limit;
    uint32_t waits = 0;

    for (;;) {
        if (__atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&admission_closed, __ATOMIC_ACQUIRE)) return false;
        if (xQueueSemaphoreTake(operation_mutex, 0) == 1) {
            if (__atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE) ||
                __atomic_load_n(&admission_closed, __ATOMIC_ACQUIRE)) {
                if (xQueueGenericSend(operation_mutex, 0, 0, 0) != 1)
                    __atomic_store_n(&mutex_poisoned, true, __ATOMIC_RELEASE);
                return false;
            }
            __atomic_store_n(&operation_owner, current, __ATOMIC_RELEASE);
            return true;
        }
        const uint64_t now = clock_api->monotonic_ms(clock_api->context);
        if (now == UINT64_MAX || now < previous || now >= limit) return false;
        previous = now;
        if (++waits > timeout_ms) return false;
        /* A real scheduler yield, never a spin loop. The transfer's original
         * deadline is retained, so waiting cannot create a fresh budget. The
         * count bound also prevents a broken nonadvancing clock/sleep pair from
         * turning admission into an infinite retry loop. */
        clock_api->sleep_ms(clock_api->context, 1);
    }
}
static bool leave(void) {
    if (!valid_task() ||
        __atomic_load_n(&operation_owner, __ATOMIC_ACQUIRE) != xTaskGetCurrentTaskHandle()) {
        __atomic_store_n(&mutex_poisoned, true, __ATOMIC_RELEASE);
        return false;
    }
    __atomic_store_n(&operation_owner, (x4_cpu_task)0, __ATOMIC_RELEASE);
    if (xQueueGenericSend(operation_mutex, 0, 0, 0) != 1) {
        /* Give failure is an unsafe ownership transition, not free capacity.
         * Keep the mutex, ELF, claims and dependencies. Only reboot recovers. */
        __atomic_store_n(&mutex_poisoned, true, __ATOMIC_RELEASE);
        return false;
    }
    return true;
}
static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
/* Diagnostics are also gate-protected; a rejected contender never mutates the
 * active caller's state, clock, error text, claim table or physical pins. */
static void fail(const char *text) {
    size_t i = 0;
    while (text[i] && i + 1u < sizeof(last_error_text)) { last_error_text[i] = text[i]; ++i; }
    last_error_text[i] = 0;
}
static void bit_delay(void) { for (volatile int i = 0; i < 40; ++i) {} }
static void sda(bool high) { high ? x4pro_pin_release(X4PRO_PIN_I2C_SDA) : x4pro_pin_output(X4PRO_PIN_I2C_SDA, false); }
static void scl(bool high) { high ? x4pro_pin_release(X4PRO_PIN_I2C_SCL) : x4pro_pin_output(X4PRO_PIN_I2C_SCL, false); }
static bool checkpoint(void) {
    const uint64_t now = clock_api->monotonic_ms(clock_api->context);
    if (now == UINT64_MAX || now < last_ms) { fail("i2c clock invalid"); return false; }
    last_ms = now;
    if (now >= deadline_ms) { fail("i2c deadline"); return false; }
    return true;
}
static bool byte_checkpoint(void) {
    if (!checkpoint()) return false;
    if (++bytes_since_yield >= COOPERATE_BYTES || last_ms - yielded_ms >= COOPERATE_MS) {
        /* SCL is low between bytes, including before repeated START. Hold the
         * admission gate while yielding; contenders remain inside their own
         * bounded admission budgets. */
        clock_api->sleep_ms(clock_api->context, 1);
        if (!checkpoint()) return false;
        bytes_since_yield = 0;
        yielded_ms = last_ms;
    }
    return true;
}
static bool raise_scl(void) {
    if (!checkpoint()) return false;
    scl(true); bit_delay();
    /* No clock-stretch polling or hidden busy-wait. A low line fails closed. */
    if (!x4pro_pin_read(X4PRO_PIN_I2C_SCL)) { fail("i2c SCL held low"); return false; }
    return checkpoint();
}
static bool write_byte(uint8_t value) {
    for (int bit = 7; bit >= 0; --bit) {
        if (!checkpoint()) return false;
        sda((value >> bit) & 1u); bit_delay();
        if (!raise_scl()) return false;
        scl(false);
    }
    sda(true); bit_delay();
    if (!raise_scl()) return false;
    const bool ack = !x4pro_pin_read(X4PRO_PIN_I2C_SDA);
    scl(false);
    if (!ack) { fail("i2c nack"); return false; }
    return byte_checkpoint();
}
static bool read_byte(bool ack, uint8_t *out) {
    uint8_t value = 0;
    sda(true);
    for (int bit = 7; bit >= 0; --bit) {
        if (!raise_scl()) return false;
        if (x4pro_pin_read(X4PRO_PIN_I2C_SDA)) value |= (uint8_t)(1u << bit);
        scl(false); bit_delay();
    }
    sda(!ack); bit_delay();
    if (!raise_scl()) return false;
    scl(false); sda(true);
    if (!byte_checkpoint()) return false;
    *out = value;
    return true;
}
/* One fixed STOP/line-release attempt on every started operation, even after
 * its deadline. No fresh timeout, retry, clock callback or scheduler wait is
 * introduced by cleanup: three fixed delays, four line changes, two reads.
 * A false readback retains the provider and all claims for a later release
 * retry or explicit reboot. It never declares uncertain hardware quiescent. */
static bool stop_bus(void) {
    scl(false); sda(false); bit_delay();
    scl(true); bit_delay();
    const bool clock_high = x4pro_pin_read(X4PRO_PIN_I2C_SCL);
    sda(true); bit_delay();
    const bool data_high = x4pro_pin_read(X4PRO_PIN_I2C_SDA);
    unsafe_bus = !clock_high || !data_high;
    if (unsafe_bus) fail("i2c STOP unconfirmed");
    return !unsafe_bus;
}
static int find_claim(uint64_t token) {
    if (!token) return -1;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) if (tokens[i] == token) return (int)i;
    return -1;
}
static bool claim_device(void *context, uint8_t address, uint64_t *out) {
    (void)context;
    if (out) *out = 0;
    if (!out || address < 0x08u || address > 0x77u || !enter()) return false;
    bool okay = false;
    if (!started || unsafe_bus || next_token == UINT64_MAX) goto done;
    for (size_t i = 0; i < MAX_CLAIMS; ++i)
        if (tokens[i] && addresses[i] == address) goto done;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) if (!tokens[i]) {
        addresses[i] = address;
        tokens[i] = next_token++;
        *out = tokens[i]; okay = true; break;
    }
done:
    if (!leave()) { *out = 0; return false; }
    return okay;
}
static bool transact(void *context, uint64_t token, const uint8_t *write_bytes, size_t write_length,
                     uint8_t *read_bytes, size_t read_length, uint32_t timeout_ms) {
    (void)context;
    if ((write_length && !write_bytes) || (read_length && !read_bytes) ||
        write_length > MAX_TRANSFER_BYTES || read_length > MAX_TRANSFER_BYTES - write_length ||
        !timeout_ms || timeout_ms > MAX_TIMEOUT_MS) return false;
    uint64_t admission_began = 0, admission_deadline = 0;
    if (!enter_transaction(timeout_ms, &admission_began, &admission_deadline)) return false;
    bool okay = false;
    const int slot = find_claim(token);
    if (!started || unsafe_bus || slot < 0) goto done;
    began_ms = last_ms = yielded_ms = admission_began;
    deadline_ms = admission_deadline;
    bytes_since_yield = 0;
    if (!checkpoint()) goto done; // No line change before admitted budget.
    scl(true); sda(true); bit_delay();
    if (!x4pro_pin_read(X4PRO_PIN_I2C_SCL) || !x4pro_pin_read(X4PRO_PIN_I2C_SDA)) {
        fail("i2c bus not idle"); goto cleanup;
    }
    if (!checkpoint()) goto cleanup;
    sda(false); bit_delay(); scl(false);
    if (!write_byte((uint8_t)(addresses[slot] << 1))) goto cleanup;
    for (size_t i = 0; i < write_length; ++i)
        if (!write_byte(write_bytes[i])) goto cleanup;
    if (read_length) {
        sda(true); bit_delay();
        if (!raise_scl()) goto cleanup;
        sda(false); bit_delay(); scl(false); // Repeated START, no intervening STOP.
        if (!write_byte((uint8_t)((addresses[slot] << 1) | 1u))) goto cleanup;
        for (size_t i = 0; i < read_length; ++i)
            if (!read_byte(i + 1u < read_length, &read_bytes[i])) goto cleanup;
    }
    okay = checkpoint();
cleanup:
    if (!stop_bus()) okay = false;
    /* Include STOP in the original total budget. A preemption or the fixed
     * safety cleanup can exceed it, but cannot turn an expired call into success. */
    if (!checkpoint()) okay = false;
done:
    return leave() && okay;
}
static bool release_device(void *context, uint64_t token) {
    (void)context;
    if (!enter()) return false;
    const int slot = find_claim(token);
    bool okay = slot >= 0;
    if (okay && unsafe_bus) okay = stop_bus(); // One checked cleanup retry only.
    const uint8_t address = okay ? addresses[slot] : 0;
    if (okay) { tokens[slot] = 0; addresses[slot] = 0; }
    if (!leave()) {
        /* Failed give/context check leaves the RTOS mutex owned and poisons
         * admission. Retain the exact claim metadata as well as its module. */
        if (okay) { tokens[slot] = token; addresses[slot] = address; }
        return false;
    }
    return okay;
}
static const risc_i2c_bus_contract_v1 api = {
    { RISC_I2C_BUS_API_V1, sizeof(api), 0, claim_device, transact, release_device },
    RISC_I2C_BUS_CONTRACT_TAG, RISC_I2C_BUS_CONTRACT_V1, RISC_I2C_BUS_SAFE_CONTRACT_FLAGS
};
static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    /* ModuleV2 serializes initial start/stop and publishes no capability until
     * start succeeds. There can be no legitimate first-allocation contender. */
    if (!valid_task() || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return false;
    if (!operation_mutex) {
        operation_mutex = xQueueCreateMutex(1); // queueQUEUE_TYPE_MUTEX
        if (!operation_mutex) return false; // No pins, claims or running state.
    }
    /* Only the serialized initial/restart callback may reopen admission. */
    if (__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&quiesced, false, __ATOMIC_RELEASE);
        __atomic_store_n(&admission_closed, false, __ATOMIC_RELEASE);
    }
    if (!enter()) return false;
    bool okay = false;
    if (clock_api || started || unsafe_bus || !dependencies || count != 1u) goto done;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) if (tokens[i]) goto done;
    const risc_provider_dependency_v1 *dep = &dependencies[0];
    const risc_platform_clock_api_v1 *candidate = dep->api;
    if (!equal(dep->capability_id, "platform.clock") || dep->api_version != 1 || !candidate ||
        candidate->api_version != RISC_PLATFORM_CLOCK_API_V1 || candidate->struct_size < sizeof(*candidate) ||
        !candidate->monotonic_ms || !candidate->sleep_ms) { fail("platform.clock invalid"); goto done; }
    clock_api = candidate;
    x4pro_pin_release(X4PRO_PIN_I2C_SDA);
    x4pro_pin_release(X4PRO_PIN_I2C_SCL);
    unsafe_bus = !x4pro_pin_read(X4PRO_PIN_I2C_SCL) || !x4pro_pin_read(X4PRO_PIN_I2C_SDA);
    if (unsafe_bus) { fail("i2c startup bus not idle"); goto done; }
    started = okay = true;
done:
    return leave() && okay;
}
static bool quiesce(void) {
    if (!valid_task() || __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return false;
    if (!operation_mutex || __atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) return true;
    if (!enter()) return false;
    bool okay = false;
    for (size_t i = 0; i < MAX_CLAIMS; ++i) if (tokens[i]) goto done;
    if (clock_api && !stop_bus()) goto done;
    started = false; clock_api = 0; okay = true;
    /* Close admission before give, but publish accepted quiescence only
     * afterwards. A contender in the final-give window must never see an
     * accepted latch and delete a still-owned mutex. */
    __atomic_store_n(&admission_closed, true, __ATOMIC_RELEASE);
    if (!leave()) return false;
    __atomic_store_n(&quiesced, true, __ATOMIC_RELEASE);
    return true;
done:
    return leave() && okay;
}
static void stop(void) {
    if (!operation_mutex || !__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&mutex_poisoned, __ATOMIC_ACQUIRE)) return;
    /* The provider executor revokes/drains consumers before successful
     * quiescence and calls stop before unmapping. No public callback may race
     * this final delete, just as no callback may race the subsequent dlclose.
     * No second take/give, STOP or other fallible transition after the runtime
     * has accepted quiesce. vQueueDelete has no fallible result. */
    x4_cpu_mutex retired = operation_mutex;
    operation_mutex = 0;
    vQueueDelete(retired);
}
static bool last_error(char *destination, size_t capacity) {
    if (!destination || !capacity || !enter()) return false;
    size_t i = 0;
    while (last_error_text[i] && i + 1u < capacity) { destination[i] = last_error_text[i]; ++i; }
    destination[i] = 0;
    const bool present = last_error_text[0] != 0;
    return leave() && present;
}
static const risc_driver_diagnostics_v2 driver = {
    { RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2), "x4pro-i2c",
      "i2c.bus", 1, &api, start, stop, quiesce }, last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.base : 0;
}
