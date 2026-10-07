# X4 Pro I²C admission and lifetime

`x4pro-i2c 0.1.4` keeps the existing `i2c.bus@1` provider prefix. GPIO38/39
and every START/byte/repeated-START/STOP remain in this external ELF. The repair
is required because GT911 capture runs on a separate task from the optional
battery consumer; placing battery reads on the UI owner did not serialize them.

## Canonical synchronization

One nonrecursive FreeRTOS mutex is created before startup publishes an API.
Claims, release, diagnostics and lifecycle operations remain **zero-wait**.
Complete bus transactions instead wait cooperatively for another legitimate bus
owner, bounded by the caller's existing total timeout. Recursive attempts still
fail immediately without changing pins or the active operation. ISR/no-task
calls are refused. The holder is checked on
give. A failed give poisons this generation: retain its mutex, code, claims and
dependencies until reboot, never pretend cleanup or deletion succeeded.
Successful quiescence requires no claims, no active operation and confirmed idle
lines. Only the serialized provider executor's final `stop` deletes the mutex,
after quiescence and consumer revocation; callbacks cannot race deletion/unmap.
`vQueueDelete` is a void SDK operation, so its preconditions, not a fabricated
return value, are checked.

The six exact imports already belong to the frozen privileged OS/CPU ABI1:
`xQueueCreateMutex`, `xQueueSemaphoreTake`, `xQueueGenericSend`, `vQueueDelete`,
`xPortInIsrContext`, `xTaskGetCurrentTaskHandle`. Ordinary app lookup cannot gain
these privileges. No loader inventory or ABI is expanded. The private header is
checked against the real pinned SDK during hosted driver builds.

Do not replace this with a bare atomic flag in ELF BSS: the loader puts provider
data in PSRAM. In pinned ESP-IDF4.4.7, [the SDK spinlock](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hw_support/include/soc/spinlock.h)
uses [external-RAM compare-and-set](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_hw_support/compare_set.c)
for S3 PSRAM; raw generated S32C1I does not implement that adaptation. The RTOS
owns its synchronization layout and memory handling. No SDK locking internals
are copied, and no claim is made that every SDK queue allocation is internal RAM.

## Bounds and failure behavior

- Eight address claims maximum; duplicate addresses and stale/zero tokens fail.
  Tokens do not restart at 1 across an in-place stop/start and never wrap/reuse.
- At most 256 total write+read payload bytes; requested timeout 1–1000 ms.
  Null/oversized/invalid requests have no pin effects. Read-only and address-only
  operations are valid, and combined register reads keep their repeated START.
- One absolute monotonic budget covers admission and transfer. A transaction
  contender sleeps in 1 ms scheduler-backed increments while another task owns
  the bus and retries only until that same deadline. Same-task reentry is refused
  immediately. Check each bit/phase and after STOP; reject invalid, backward or
  overflowing clocks. Never grant a fresh nested timeout.
- Yield through the existing `platform.clock.sleep_ms(1)` after eight bytes or
  two elapsed milliseconds, with SCL low. The mutex remains held; contenders
  stay inside their own bounded admission budgets. The clock implementation does
  not introduce a recursive callback deadlock. Each bit's fixed delay is finite.
- Every started operation attempts one fixed STOP/line release, including NACK
  on the read address and transfer timeout. Cleanup has four line changes,
  three fixed delays and two reads, with no fresh timeout or retry loop.
  Preemption and this safety cleanup may run beyond the requested deadline;
  such a call returns false. This is not a hard real-time wall-clock guarantee.
- A low SCL after release is refused; this bit-bang implementation does not wait
  for clock stretching. Unconfirmed STOP blocks further transfers/admission.
  The exact claim survives failed release. A later release performs one checked
  STOP retry; only confirmed idle permits retiring the token. There is no bus
  reset, nine-clock recovery, guessed peripheral register write or forced unload.

`bash test/run_x4pro_i2c_test.sh` exercises actual provider code with deterministic
battery/touch-address pthread contention, repeated START/STOP, no-write busy
refusal, reentry, allocation/context/lifetime failures, deadlines and poisoned
mutex cleanup. Host tests and Xtensa import/relocation checks do not establish
physical timing, touch wake or sleep current. X4 sleep/shutdown guards remain off.

## Compatible, coherent upgrade

The canonical `sdk/driver/RiscI2cBusV1.h` originated in this Reader repository;
its serialized/deadline contract was last strengthened by `9730c690` (2026-09-28).
The original API-1 struct and callback offsets remain unchanged. This repair adds
an optional table suffix identified by the `I2CS` tag, contract version 1 and
three separately checked guarantees: serialized operation, total deadline and
retained release. An unrelated larger table is not proof of compatibility.
The suffix is provider-declared behavior, not trust, a permission or a new loader
ABI. The updated battery and GT911 drivers check it before touching any hardware.
Legacy API-1 callers still see the original prefix; legacy providers remain
usable by their existing callers but cannot satisfy these updated X4 consumers.

There is no provider-package minimum-version field in `RequirementV2` (capability
and API only) or `OrdinaryRequirement` (capability and minimum API only).
`OrdinaryResourceImport.minVersion` governs resource packages and is not a device
provider gate. Reusing the existing size/version convention with a tagged suffix
is therefore narrower than adding a second dependency resolver/version scheme.
No Watch SDK snapshot or runtime fixture is edited as an assumed upstream owner.

Install the coherent X4 set together before boot: `x4pro-i2c 0.1.2`,
`x4pro-gt911 0.1.3` and `x4pro-battery 0.1.2`. Older GT911 code can discard a
failed-release token; updating only the bus does not repair that consumer.
The ordinary X4 package builder produces the complete matching set. This is not
a claim that the generic installer can atomically upgrade every live dependency.

The clock follow-through advances this package to 0.1.3. Admission-closed and
quiescence-accepted are distinct: acceptance is published only after the final
mutex give succeeds. A delayed-give pthread regression proves another lifecycle
caller cannot accept quiescence or delete the still-owned mutex in that window.
The delivered battery-only 0.1.2 set remains recorded above as historical custody.


## 0.1.4 contention repair

Physical X4 testing showed intermittent CW2017 `VERSION` read failures while
GT911 capture was polling normally. The failure was not a gauge-format problem:
the shared I²C provider used a zero-wait mutex for transfers, so a battery poll
that coincided with a touch transaction was rejected before touching the bus.
The battery driver then correctly reported that rejected transport as
`cw2017 version read`.

0.1.4 fixes the transport root cause. Normal transactions cooperatively wait for
the current bus owner within their original 1–1000 ms total deadline. They do
not spin and do not receive a fresh transfer timeout after admission. Claim,
release and lifecycle operations remain fail-fast. The deterministic pthread
regression now proves both cases: a contender succeeds when the active transfer
drains inside its budget, and fails with zero pin activity when the owner remains
busy through the deadline.
