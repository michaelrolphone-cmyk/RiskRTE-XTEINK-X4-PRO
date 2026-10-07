/* Actual bit-bang provider, deterministic fake pins/clock, no device access.
 * Threads model the independent GT911 capture and battery-owner consumers. */
#include "RiscI2cBusV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_pins.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "../../Drivers/x4pro_i2c/os_cpu_v1.h"

extern const risc_driver_v2 *t5_driver_get(uint32_t);
static const risc_driver_v2 *driver;
static const risc_i2c_bus_api_v1 *bus;
static const risc_driver_diagnostics_v2 *diagnostics;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static atomic_uint_fast64_t now_ms;
static atomic_uint clock_step;
static unsigned sleeps, io_count, starts, stops, data_index, data_count;
static bool levels[49], data_script[4096], stuck_scl;
static bool block_first, blocked, resume_first, resume_on_sleep, owner_finished, reenter, in_reentry;
static _Thread_local unsigned caller;
static uint64_t battery_claim, touch_claim;
static bool first_result;
static bool block_give, give_blocked, allow_give, quiesce_result;
static bool fail_allocation, fail_give, isr_context, no_task;
static unsigned creates, deletes, gives, task_shift;
static bool shift_during_io;
struct QueueDefinition { pthread_mutex_t lock; bool alive; };
static struct QueueDefinition cpu_mutex;
struct QueueDefinition *xQueueCreateMutex(uint8_t type) {
    assert(type == 1 && !cpu_mutex.alive);
    if (fail_allocation) return 0;
    assert(!pthread_mutex_init(&cpu_mutex.lock, 0));
    cpu_mutex.alive = true; ++creates; return &cpu_mutex;
}
int xQueueSemaphoreTake(struct QueueDefinition *queue, uint32_t ticks) {
    assert(queue == &cpu_mutex && queue->alive && !ticks);
    return pthread_mutex_trylock(&queue->lock) == 0;
}
int xQueueGenericSend(struct QueueDefinition *queue, const void *item, uint32_t ticks, int position) {
    assert(queue == &cpu_mutex && queue->alive && !item && !ticks && !position);
    ++gives;
    if (block_give) {
        pthread_mutex_lock(&mutex);
        give_blocked = true; pthread_cond_broadcast(&condition);
        while (!allow_give) pthread_cond_wait(&condition, &mutex);
        pthread_mutex_unlock(&mutex);
    }
    if (fail_give) return 0;
    return pthread_mutex_unlock(&queue->lock) == 0;
}
void vQueueDelete(struct QueueDefinition *queue) {
    assert(queue == &cpu_mutex && queue->alive);
    assert(!pthread_mutex_trylock(&queue->lock)); // Never delete a held mutex.
    assert(!pthread_mutex_unlock(&queue->lock));
    assert(!pthread_mutex_destroy(&queue->lock));
    queue->alive = false; ++deletes;
}
int xPortInIsrContext(void) { return isr_context; }
struct tskTaskControlBlock *xTaskGetCurrentTaskHandle(void) {
    return no_task ? 0 : (struct tskTaskControlBlock *)(uintptr_t)(caller + 1 + task_shift);
}

static void reentrant_calls(void) {
    if (!reenter || in_reentry) return;
    in_reentry = true;
    const unsigned before = io_count;
    uint8_t w = 4, r = 0; uint64_t claim = 99; char error[64];
    assert(!bus->transact(0, touch_claim, &w, 1, &r, 1, 20));
    assert(!bus->claim_device(0, 0x51, &claim) && claim == 0);
    assert(!bus->release_device(0, battery_claim));
    assert(!driver->quiesce()); driver->stop();
    assert(!diagnostics->last_error(error, sizeof(error)));
    assert(io_count == before);
    in_reentry = false;
}
static void record_io(void) {
    ++io_count;
    if (shift_during_io) { shift_during_io = false; task_shift = 16; }
    reentrant_calls();
    if (caller == 1 && block_first && !blocked) {
        pthread_mutex_lock(&mutex);
        blocked = true; pthread_cond_broadcast(&condition);
        while (!resume_first) pthread_cond_wait(&condition, &mutex);
        pthread_mutex_unlock(&mutex);
    }
}
void x4pro_pin_release(uint32_t pin) {
    assert(pin == X4PRO_PIN_I2C_SDA || pin == X4PRO_PIN_I2C_SCL);
    record_io();
    if (pin == X4PRO_PIN_I2C_SDA && !levels[pin] && levels[X4PRO_PIN_I2C_SCL]) ++stops;
    levels[pin] = true;
}
void x4pro_pin_output(uint32_t pin, bool high) {
    assert(!high && (pin == X4PRO_PIN_I2C_SDA || pin == X4PRO_PIN_I2C_SCL));
    record_io();
    if (pin == X4PRO_PIN_I2C_SDA && levels[pin] && levels[X4PRO_PIN_I2C_SCL]) ++starts;
    levels[pin] = false;
}
bool x4pro_pin_read(uint32_t pin) {
    record_io();
    if (pin == X4PRO_PIN_I2C_SCL) return !stuck_scl;
    assert(pin == X4PRO_PIN_I2C_SDA);
    return data_index < data_count ? data_script[data_index++] : true;
}
static uint64_t monotonic(void *context) {
    (void)context;
    return atomic_fetch_add(&now_ms, atomic_load(&clock_step));
}
static void sleep_ms(void *context, uint32_t ms) {
    (void)context;
    assert(ms == 1); ++sleeps; atomic_fetch_add(&now_ms, ms);
    if (resume_on_sleep) {
        pthread_mutex_lock(&mutex);
        if (blocked && !resume_first) {
            resume_first = true;
            pthread_cond_broadcast(&condition);
        }
        while (!owner_finished) pthread_cond_wait(&condition, &mutex);
        pthread_mutex_unlock(&mutex);
    }
    reentrant_calls();
}
static const risc_platform_clock_api_v1 clock_api = {
    1, sizeof(clock_api), 0, monotonic, sleep_ms
};
static void push(bool value) { assert(data_count < sizeof(data_script)); data_script[data_count++] = value; }
static void reset_model(void) {
    assert(!block_first);
    io_count = starts = stops = sleeps = data_index = data_count = 0;
    levels[X4PRO_PIN_I2C_SDA] = levels[X4PRO_PIN_I2C_SCL] = true;
    atomic_store(&now_ms, 100); atomic_store(&clock_step, 0);
    resume_on_sleep = owner_finished = false;
    reenter = in_reentry = stuck_scl = false;
}
static void append_read(unsigned write_len, const uint8_t *read, unsigned read_len) {
    push(true); // initial idle line
    for (unsigned i = 0; i < 1u + write_len + (read_len ? 1u : 0u); ++i) push(false); // ACKs
    for (unsigned i = 0; i < read_len; ++i)
        for (int bit = 7; bit >= 0; --bit) push((read[i] >> bit) & 1u);
    push(true); // STOP release readback
}
static void prepare_read(unsigned write_len, const uint8_t *read, unsigned read_len) {
    reset_model(); append_read(write_len, read, read_len);
}
static void start_bus(void) {
    const risc_provider_dependency_v1 dep = {"platform.clock", 1, &clock_api};
    assert(driver->start(&dep, 1));
}
static void *battery_thread(void *unused) {
    (void)unused; caller = 1;
    uint8_t reg = 4, value = 0;
    first_result = bus->transact(0, battery_claim, &reg, 1, &value, 1, 20);
    assert(value == 53);
    pthread_mutex_lock(&mutex);
    owner_finished = true;
    pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&mutex);
    return 0;
}
static void *quiesce_thread(void *unused) {
    (void)unused; caller = 1; quiesce_result = driver->quiesce(); return 0;
}
int main(int argc, char **argv) {
    (void)argv;
    driver = t5_driver_get(2); assert(driver && !t5_driver_get(1));
    bus = driver->capability; assert(risc_i2c_bus_has_safe_contract(bus)); diagnostics = (const risc_driver_diagnostics_v2 *)driver;
    reset_model();
    const risc_provider_dependency_v1 initial = {"platform.clock", 1, &clock_api};
    isr_context = true; assert(!driver->start(&initial, 1));
    isr_context = false; no_task = true; assert(!driver->start(&initial, 1));
    no_task = false; fail_allocation = true; assert(!driver->start(&initial, 1));
    assert(!creates && !io_count); fail_allocation = false;
    assert(!driver->start(0, 0));
    risc_platform_clock_api_v1 bad = clock_api; bad.api_version = 2;
    risc_provider_dependency_v1 dep = {"platform.clock", 1, &bad};
    assert(!driver->start(&dep, 1)); bad = clock_api; bad.monotonic_ms = 0;
    assert(!driver->start(&dep, 1)); bad = clock_api; bad.struct_size = 1;
    assert(!driver->start(&dep, 1)); assert(io_count == 0);
    start_bus();
    assert(bus->claim_device(0, 0x63, &battery_claim));
    assert(bus->claim_device(0, 0x5d, &touch_claim));
    uint64_t token = 9;
    assert(!bus->claim_device(0, 0x63, &token) && !token);
    assert(!bus->claim_device(0, 0, &token));
    const unsigned before = io_count;
    assert(!driver->start(&dep, 1) && !driver->quiesce()); driver->stop();
    assert(io_count == before);

    uint8_t data[256] = {0}, value = 0, reg = 4, expected = 53;
    prepare_read(1, &expected, 1);
    assert(bus->transact(0, battery_claim, &reg, 1, &value, 1, 20));
    assert(value == 53 && starts == 2 && stops == 1 && data_index == data_count);
    prepare_read(1, &expected, 1); reenter = true;
    assert(bus->transact(0, battery_claim, &reg, 1, &value, 1, 20)); reenter = false;
    assert(starts == 2 && stops == 1);

    /* First caller is suspended inside its first pin operation. A transaction
     * contender waits within its original deadline instead of fabricating a
     * device read failure. Claim/release/lifecycle paths remain fail-fast. */
    prepare_read(1, &expected, 1);
    append_read(2, &expected, 1);
    pthread_t thread; block_first = true; blocked = resume_first = owner_finished = false;
    assert(!pthread_create(&thread, 0, battery_thread, 0));
    pthread_mutex_lock(&mutex);
    while (!blocked) pthread_cond_wait(&condition, &mutex);
    pthread_mutex_unlock(&mutex);
    const unsigned count = io_count;
    uint8_t touch_reg[2] = {0x81, 0x4e};
    // Lifecycle and claim mutations remain fail-fast while another task owns
    // the bus; unlike normal transactions they never queue behind it.
    assert(!bus->release_device(0, battery_claim) && !bus->release_device(0, touch_claim));
    assert(!bus->claim_device(0, 0x51, &token) && !token);
    assert(!driver->quiesce()); driver->stop(); assert(io_count == count);
    resume_on_sleep = true;
    assert(bus->transact(0, touch_claim, touch_reg, 2, &value, 1, 20));
    resume_on_sleep = false;
    assert(value == expected && sleeps >= 1);
    pthread_join(thread, 0); assert(first_result); block_first = false;
    assert(io_count > count && starts == 4 && stops == 2 && data_index == data_count);

    /* A contender whose owner does not drain before the total budget expires
     * still fails without touching bus pins or lifecycle state. */
    prepare_read(1, &expected, 1);
    block_first = true; blocked = resume_first = owner_finished = false;
    assert(!pthread_create(&thread, 0, battery_thread, 0));
    pthread_mutex_lock(&mutex);
    while (!blocked) pthread_cond_wait(&condition, &mutex);
    pthread_mutex_unlock(&mutex);
    const unsigned timeout_count = io_count;
    assert(!bus->transact(0, touch_claim, touch_reg, 2, &value, 1, 3));
    assert(io_count == timeout_count && sleeps >= 3);
    pthread_mutex_lock(&mutex); resume_first = true; pthread_cond_broadcast(&condition); pthread_mutex_unlock(&mutex);
    pthread_join(thread, 0); assert(first_result); block_first = false;

    reset_model();
    assert(!bus->transact(0, battery_claim, 0, 1, data, 1, 20));
    assert(!bus->transact(0, battery_claim, data, 1, 0, 1, 20));
    assert(!bus->transact(0, battery_claim, data, 257, data, 0, 20));
    assert(!bus->transact(0, battery_claim, data, 256, data, 1, 20));
    assert(!bus->transact(0, battery_claim, data, 1, data, 1, 0));
    assert(!bus->transact(0, battery_claim, data, 1, data, 1, 1001));
    assert(!bus->transact(0, UINT64_MAX, data, 1, data, 1, 20)); assert(io_count == 0);

    reset_model(); push(true); push(true); atomic_store(&clock_step, 1);
    assert(!bus->transact(0, battery_claim, &reg, 1, &value, 1, 5));
    assert(stops == 1 && io_count < 40); atomic_store(&clock_step, 0);
    reset_model(); atomic_store(&now_ms, UINT64_MAX - 5);
    assert(!bus->transact(0, battery_claim, &reg, 1, &value, 1, 20)); assert(io_count == 0);
    reset_model(); atomic_store(&now_ms, UINT64_MAX);
    assert(!bus->transact(0, battery_claim, &reg, 1, &value, 1, 20)); assert(io_count == 0);

    /* NACK on the read-address phase must still emit exactly one STOP. */
    reset_model(); push(true); push(false); push(false); push(true); push(true);
    assert(!bus->transact(0, battery_claim, &reg, 1, &value, 1, 20));
    assert(starts == 2 && stops == 1);
    prepare_read(16, 0, 0); reenter = true;
    assert(bus->transact(0, battery_claim, data, 16, 0, 0, 20));
    reenter = false; assert(sleeps >= 2 && stops == 1);

    /* An unconfirmed STOP blocks all new I/O and retains exact claims. */
    reset_model(); push(true); push(true); push(false); // idle, address NACK, bad STOP
    assert(!bus->transact(0, battery_claim, &reg, 1, &value, 1, 20));
    const unsigned failed_count = io_count;
    assert(!bus->transact(0, touch_claim, touch_reg, 2, &value, 1, 20));
    assert(!bus->claim_device(0, 0x51, &token) && !token);
    assert(!driver->quiesce() && io_count == failed_count);
    push(false); assert(!bus->release_device(0, battery_claim));
    push(true); assert(bus->release_device(0, battery_claim));
    assert(!bus->release_device(0, battery_claim));
    prepare_read(2, &expected, 1);
    assert(bus->transact(0, touch_claim, touch_reg, 2, &value, 1, 20));
    assert(bus->release_device(0, touch_claim));
    block_give = true; give_blocked = allow_give = false;
    assert(!pthread_create(&thread, 0, quiesce_thread, 0));
    pthread_mutex_lock(&mutex);
    while (!give_blocked) pthread_cond_wait(&condition, &mutex);
    pthread_mutex_unlock(&mutex);
    assert(!driver->quiesce()); driver->stop(); assert(cpu_mutex.alive);
    pthread_mutex_lock(&mutex); allow_give = true; pthread_cond_broadcast(&condition); pthread_mutex_unlock(&mutex);
    pthread_join(thread, 0); block_give = false;
    assert(quiesce_result && driver->quiesce());
    const unsigned accepted_gives = gives, accepted_io = io_count, accepted_deletes = deletes;
    fail_give = true; driver->stop(); fail_give = false;
    assert(gives == accepted_gives && io_count == accepted_io && deletes == accepted_deletes + 1);
    assert(!cpu_mutex.alive);
    const unsigned stopped_count = io_count;
    assert(!bus->transact(0, touch_claim, touch_reg, 2, &value, 1, 20));
    assert(!bus->claim_device(0, 0x63, &token) && io_count == stopped_count);
    assert(driver->quiesce()); driver->stop();
    start_bus(); assert(bus->claim_device(0, 0x63, &token) && token != battery_claim);
    assert(!bus->release_device(0, battery_claim)); assert(bus->release_device(0, token));

    /* Failed final line cleanup retains clock/start state until a real retry. */
    stuck_scl = true; assert(!driver->quiesce());
    const unsigned retained_count = io_count;
    const risc_provider_dependency_v1 good = {"platform.clock", 1, &clock_api};
    assert(!driver->start(&good, 1) && io_count == retained_count);
    stuck_scl = false; assert(driver->quiesce());
    start_bus(); assert(driver->quiesce()); driver->stop();
    assert(creates == deletes && !cpu_mutex.alive);
    start_bus();
    assert(bus->claim_device(0, 0x63, &token));
    const unsigned protected_deletes = deletes;
    if (argc > 1) {
        /* Simulate an illegal context change inside an already-admitted call,
         * so the checked leave, not merely admission, detects foreign owner. */
        prepare_read(1, &expected, 1); shift_during_io = true;
        const unsigned prior_gives = gives;
        assert(!bus->transact(0, token, &reg, 1, &value, 1, 20));
        assert(task_shift == 16 && gives == prior_gives);
        task_shift = 0;
    } else {
        fail_give = true;
        assert(!bus->release_device(0, token));
        fail_give = false;
    }
    assert(!bus->claim_device(0, 0x51, &token) && !token);
    assert(!driver->quiesce()); driver->stop();
    assert(!driver->start(&initial, 1) && deletes == protected_deletes && cpu_mutex.alive);
    puts("X4 I2C: bounded-wait battery/touch serialization, repeated START, deadlines, reentry and retained cleanup PASS");
}
