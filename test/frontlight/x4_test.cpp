#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <pthread.h>
extern "C" {
#include "../../Drivers/x4pro_i2c/os_cpu_v1.h"
}
#include "BoardX4Pro.h"
#include "FrontlightLevels.h"
#include "../../src/native/NativeBatteryGauge.h"
#include "x4pro_mmio.h"
extern "C" const risc_driver_v2* t5_driver_get(uint32_t abi);
static std::map<uint32_t, uint32_t> regs;
static bool held[2];
static unsigned writes;
static const risc_driver_v2* testProvider;
static const risc_frontlight_api_v1* testLight;
static bool failAllocation, failTake, failGive, isrContext, noTask, shiftTask;
static unsigned creates, deletes, gives;
static bool reenter, inReentry, blockWrite, writeBlocked, resumeWrite;
static bool blockGive, giveBlocked, resumeGive;
static std::mutex coordination;
static std::condition_variable changed;
static thread_local unsigned taskIdentity;
struct QueueDefinition { pthread_mutex_t lock; bool alive; };
static QueueDefinition cpuMutex;
extern "C" QueueDefinition* xQueueCreateMutex(uint8_t type) {
  assert(type == 1 && !cpuMutex.alive);
  if (failAllocation) return nullptr;
  assert(!pthread_mutex_init(&cpuMutex.lock, nullptr));
  cpuMutex.alive = true; ++creates; return &cpuMutex;
}
extern "C" int xQueueSemaphoreTake(QueueDefinition* queue, uint32_t ticks) {
  assert(queue == &cpuMutex && queue->alive && !ticks);
  return !failTake && pthread_mutex_trylock(&queue->lock) == 0;
}
extern "C" int xQueueGenericSend(QueueDefinition* queue, const void* item, uint32_t ticks, int position) {
  assert(queue == &cpuMutex && queue->alive && !item && !ticks && !position);
  ++gives;
  if (blockGive) {
    std::unique_lock<std::mutex> lock(coordination);
    giveBlocked = true; changed.notify_all();
    changed.wait(lock, [] { return resumeGive; });
  }
  return !failGive && pthread_mutex_unlock(&queue->lock) == 0;
}
extern "C" void vQueueDelete(QueueDefinition* queue) {
  assert(queue == &cpuMutex && queue->alive);
  assert(!pthread_mutex_trylock(&queue->lock)); // Never delete a held mutex.
  assert(!pthread_mutex_unlock(&queue->lock));
  assert(!pthread_mutex_destroy(&queue->lock));
  queue->alive = false; ++deletes;
}
extern "C" int xPortInIsrContext() { return isrContext; }
extern "C" tskTaskControlBlock* xTaskGetCurrentTaskHandle() {
  return noTask ? nullptr : reinterpret_cast<tskTaskControlBlock*>(reinterpret_cast<uintptr_t>(&taskIdentity) + (shiftTask ? 1 : 0));
}
static void hardwareCheckpoint() {
  if (reenter && !inReentry) {
    inReentry = true;
    const unsigned before = writes;
    uint16_t value = 77, maximum = 88;
    assert(!testLight->set_level(nullptr, 1, 1000));
    assert(!testLight->get_level(nullptr, &value, &maximum));
    assert(value == 77 && maximum == 88);
    assert(!testProvider->quiesce()); testProvider->stop();
    assert(!testProvider->start(nullptr, 0));
    assert(writes == before && !deletes);
    inReentry = false;
  }
  if (blockWrite) {
    std::unique_lock<std::mutex> lock(coordination);
    writeBlocked = true; changed.notify_all();
    changed.wait(lock, [] { return resumeWrite; });
    blockWrite = false;
  }
}
constexpr uint32_t base = 0x60019000;
constexpr uint32_t timer = base + 0xa0, clockSelect = base + 0xd0;
constexpr uint32_t systemClock = 0x600c0018, systemReset = 0x600c0020;
extern "C" uint32_t x4pro_reg_read(uint32_t addr) { return regs[addr]; }
extern "C" void x4pro_reg_write(uint32_t addr, uint32_t value) {
  // Model the timer and channel self-clearing parameter-update strobes.
  if (addr == timer) value &= ~(1u << 25);
  if (addr == base || addr == base + 0x14) value &= ~(1u << 4);
  regs[addr] = value;
  ++writes;
  hardwareCheckpoint();
}
extern "C" void x4pro_pin_output(uint32_t pin, bool high) {
  assert(pin == 8 || pin == 9);
  assert(!high); // No full-brightness GPIO pulse, even during initialization.
  regs[X4PRO_GPIO_MATRIX_BASE + pin * 4] = 0x100;
  ++writes;
  hardwareCheckpoint();
}
extern "C" void x4pro_pin_hold(uint32_t pin, bool hold) {
  assert(pin == 8 || pin == 9);
  held[pin - 8] = hold;
  ++writes;
  hardwareCheckpoint();
}
bool nativeBatteryReadSnapshot(NativeBatterySnapshot*) { return false; }
bool halStoragePrepareForSleep() { return true; }
void halStorageMediaUnavailable() {}
static void expectDuty(const risc_frontlight_api_v1* light, uint16_t expected) {
  uint16_t actual = 99, maximum = 99;
  assert(light->get_level(nullptr, &actual, &maximum));
  assert(actual == expected && maximum == 1024);
  for (unsigned c = 0; c < 2; ++c) {
    assert(regs[base + c * 0x14 + 8] == unsigned(expected) * 16);
    assert((regs[base + c * 0x14] & 4) == (expected ? 4u : 0u));
    assert(regs[X4PRO_GPIO_MATRIX_BASE + (8 + c) * 4] == (expected ? 73 + c : 0x100));
    assert(held[c] == (expected == 0));
  }
}
int main(int argc, char** argv) {
  auto* provider = t5_driver_get(2);
  assert(provider && !t5_driver_get(1));
  auto* light = static_cast<const risc_frontlight_api_v1*>(provider->capability);
  testProvider = provider; testLight = light;
  uint16_t value = 7, maximum = 13;
  if (argc == 2) {
    // Each poison case runs in a fresh process: a poisoned generation may
    // recover only on reboot, never by resetting flags for the test.
    const bool startGive = !std::strcmp(argv[1], "start-give");
    failGive = startGive;
    assert(provider->start(nullptr, 0) != startGive);
    if (!startGive) assert(light->set_level(nullptr, 2, 1000));
    const unsigned beforeDeletes = deletes;
    if (startGive) {
      assert(held[0] && held[1]);
    } else if (!std::strcmp(argv[1], "give")) {
      failGive = true; assert(!light->set_level(nullptr, 3, 1000));
    } else if (!std::strcmp(argv[1], "get-give")) {
      failGive = true; assert(!light->get_level(nullptr, &value, &maximum));
      assert(value == 7 && maximum == 13);
    } else if (!std::strcmp(argv[1], "quiesce-give")) {
      failGive = true; assert(!provider->quiesce());
      assert(held[0] && held[1]);
    } else if (!std::strcmp(argv[1], "owner")) {
      // A task identity change after take is caught before the give.
      blockWrite = true;
      bool result = true;
      std::thread operation([&] { result = light->set_level(nullptr, 3, 1000); });
      { std::unique_lock<std::mutex> lock(coordination);
        changed.wait(lock, [] { return writeBlocked; });
        shiftTask = true; resumeWrite = true; changed.notify_all(); }
      operation.join(); assert(!result); shiftTask = false;
    } else { assert(false); }
    failGive = false;
    const unsigned beforeWrites = writes;
    assert(!light->set_level(nullptr, 0, 1000));
    assert(!light->get_level(nullptr, &value, &maximum));
    assert(!provider->quiesce()); provider->stop();
    assert(!provider->start(nullptr, 0));
    assert(writes == beforeWrites && deletes == beforeDeletes && cpuMutex.alive);
    std::printf("X4 frontlight poisoned mutex retained: %s PASS\n", argv[1]);
    return 0;
  }
  assert(!light->get_level(nullptr, &value, &maximum));
  assert(value == 7 && maximum == 13);
  assert(!light->set_level(nullptr, 1, 1000));
  BoardX4Pro::setBacklightLevel(1); // No provider is a safe no-op.
  assert(!BoardX4Pro::attachFrontlight(nullptr));
  auto invalid = *light; invalid.struct_size = 0;
  assert(!BoardX4Pro::attachFrontlight(&invalid));
  regs[systemReset] = 0xa0000800;
  regs[systemClock] = 0x12000000;
  assert(!provider->start(nullptr, 1));
  isrContext = true; assert(!provider->start(nullptr, 0));
  isrContext = false; noTask = true; assert(!provider->start(nullptr, 0));
  noTask = false; failAllocation = true; assert(!provider->start(nullptr, 0));
  assert(!creates && !writes); failAllocation = false;
  failTake = true; assert(!provider->start(nullptr, 0));
  assert(creates == 1 && !writes); failTake = false;
  assert(provider->start(nullptr, 0));
  assert(!provider->start(nullptr, 0));
  assert(regs[timer] == (400u << 4 | 10u));
  assert(regs[clockSelect] == 3);
  assert(regs[systemReset] == 0xa0000000 && regs[systemClock] == 0x12000800);
  expectDuty(light, 0);
  assert(BoardX4Pro::attachFrontlight(light));
  assert(BoardX4Pro::capabilities().hasBacklight);
  constexpr uint16_t expected[] = {0, 1, 2, 5, 10, 20, 51, 102, 256, 512, 1024};
  for (unsigned i = 0; i <= 10; ++i) {
    BoardX4Pro::setBacklightLevel(i);
    expectDuty(light, expected[i]);
    if (i) assert(expected[i] > expected[i - 1]);
  }
  BoardX4Pro::setBacklightLevel(255); expectDuty(light, 1024);
  // Sleep/off -> saved-level restore, repeated edits, and restart all traverse
  // the production board adapter and installed-provider interface.
  for (unsigned i = 1; i <= 10; ++i) {
    BoardX4Pro::setBacklightLevel(0); expectDuty(light, 0);
    BoardX4Pro::restoreBacklightLevel(i); expectDuty(light, expected[i]);
    BoardX4Pro::restoreBacklightLevel(i); expectDuty(light, expected[i]);
  }
  const auto before = writes;
  assert(!light->set_level(nullptr, 1, 0));
  assert(!light->set_level(nullptr, 11, 10));
  assert(!light->get_level(nullptr, nullptr, &maximum));
  assert(writes == before); expectDuty(light, 1024);
  // Every fine tenths-percent request is distinct. Broad 16-bit ratios stay
  // monotonic and within bounds; positive rounding never silently turns off.
  uint16_t last = 0;
  for (unsigned i = 0; i <= 1000; ++i) {
    assert(light->set_level(nullptr, i, 1000));
    assert(light->get_level(nullptr, &value, &maximum));
    if (i) assert(value > last);
    last = value;
  }
  last = 0;
  for (unsigned i = 0; i <= 65535; ++i) {
    assert(light->set_level(nullptr, i, 65535));
    assert(light->get_level(nullptr, &value, &maximum));
    assert(value >= last && value <= 1024);
    if (i && i < 65535) assert(value >= 1 && value < 1024);
    last = value;
  }
  // ISR/no-task and contended admission never write hardware.
  for (unsigned condition = 0; condition < 3; ++condition) {
    isrContext = condition == 0; noTask = condition == 1; failTake = condition == 2;
    const unsigned noWrites = writes;
    assert(!light->set_level(nullptr, 1, 1000));
    assert(!light->get_level(nullptr, &value, &maximum));
    assert(!provider->quiesce()); provider->stop();
    assert(writes == noWrites && !deletes);
    isrContext = noTask = failTake = false;
  }
  reenter = true;
  assert(light->set_level(nullptr, 1, 1000));
  reenter = false;
  // Deterministic independent-task contention during actual production writes.
  blockWrite = true;
  bool operationResult = false;
  std::thread operation([&] { operationResult = light->set_level(nullptr, 2, 1000); });
  { std::unique_lock<std::mutex> lock(coordination);
    changed.wait(lock, [] { return writeBlocked; }); }
  unsigned noWrites = writes;
  assert(!light->set_level(nullptr, 3, 1000));
  assert(!light->get_level(nullptr, &value, &maximum));
  assert(!provider->quiesce()); provider->stop();
  assert(writes == noWrites && !deletes);
  { std::lock_guard<std::mutex> lock(coordination); resumeWrite = true; changed.notify_all(); }
  operation.join(); assert(operationResult); expectDuty(light, 2);
  // Final give is not accepted quiescence: a racing stop cannot delete yet.
  blockGive = true;
  bool quiesceResult = false;
  std::thread quiescing([&] { quiesceResult = provider->quiesce(); });
  { std::unique_lock<std::mutex> lock(coordination);
    changed.wait(lock, [] { return giveBlocked; }); }
  noWrites = writes;
  assert(!provider->quiesce()); provider->stop();
  assert(!light->set_level(nullptr, 1, 1000));
  assert(writes == noWrites && !deletes);
  { std::lock_guard<std::mutex> lock(coordination); resumeGive = true; changed.notify_all(); }
  quiescing.join(); assert(quiesceResult); blockGive = false;
  provider->stop(); assert(creates == deletes && !cpuMutex.alive);
  assert(provider->start(nullptr, 0)); expectDuty(light, 0);
  // Changed timer: fail dark, do not silently retime another owner; getters
  // report failure rather than claim the cached value is still applied.
  regs[timer] = 123;
  assert(!light->set_level(nullptr, 500, 1000));
  assert(held[0] && held[1] && regs[timer] == 123);
  assert(!light->get_level(nullptr, &value, &maximum));
  assert(provider->quiesce());
  regs.clear();
  assert(provider->start(nullptr, 0)); expectDuty(light, 0);
  BoardX4Pro::restoreBacklightLevel(3); expectDuty(light, 5);
  assert(provider->quiesce()); assert(provider->quiesce());
  assert(held[0] && held[1]);
  assert(!light->set_level(nullptr, 1, 1000));
  for (unsigned source : {0u, 1u, 3u}) {
    regs.clear(); regs[clockSelect] = source;
    assert(provider->start(nullptr, 0));
    assert(regs[timer] == ((source == 1 ? 800u : 400u) << 4 | 10u));
    assert(provider->quiesce());
  }
  regs.clear(); regs[clockSelect] = 2;
  auto rejected = writes;
  assert(!provider->start(nullptr, 0)); assert(writes == rejected);
  // Reject either claimed channel and all shared-timer conflicts with no writes.
  for (unsigned c = 0; c < 8; ++c) {
    regs.clear(); regs[clockSelect] = 3; regs[base + c * 0x14] = 4;
    rejected = writes;
    assert(!provider->start(nullptr, 0)); assert(writes == rejected);
  }
  regs.clear(); regs[base + 2 * 0x14] = 5; // Unset global clock + active timer1.
  rejected = writes;
  assert(!provider->start(nullptr, 0)); assert(writes == rejected);
  regs[clockSelect] = 3; regs[base + 0xa8] = 0x12345;
  assert(provider->start(nullptr, 0));
  BoardX4Pro::setBacklightLevel(1); expectDuty(light, 1);
  assert(provider->quiesce());
  assert(regs[base + 2 * 0x14] == 5 && regs[base + 0xa8] == 0x12345);
  provider->stop(); assert(creates == deletes && !cpuMutex.alive);
  puts("X4 frontlight: real board/provider path, fine duty, bounds, conflicts, dark failure, sleep/restore PASS");
}
