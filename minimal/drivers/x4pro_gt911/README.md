# X4 GT911 ordinary provider

Package `x4pro-gt911` 0.1.6 adapts the Reader source 0.1.5 at
`34d8e694d89a1e72d8854403d8592c289fae3ddc` to scoped typed GPIO,
owner-task synchronization, clock and safe-contract I2C dependencies. It has
no MMIO, firmware pin, RTOS or board-driver imports. Physical verification is
pending.

## Configuration and sequencing

The envelope is exactly `goodix,gt911`, revision `unspecified`, `touch.i2c@2`.
The shared `RiscTouchI2cV2.h` record's `base.struct_size` covers the full v2
record. This adapter validates the complete configuration and all required
table suffixes before I/O:

- Raw 480×800 coordinates, primary address 0x5d and alternate address 0x14
- GPIO4 active-low reset, GPIO10 bidirectional IRQ strap with no pull-up
- GPIO2 active-low power, distinct from the board-alive GPIO1 owner
- I2C SDA39/SCL38, controller 0 or 1, nonzero frequency at most 400 kHz
- Reset assert/recovery 10/10 ms; exact protocol-specific waits below

`board.power.ready` is checked before claiming GPIO2. Claiming GPIO2 stages
LOW before output enable, then startup waits 50 ms. Each address-selection
sequence claims IRQ output at the selected level, asserts RESET LOW, waits
10 ms, raises RESET, waits 10 ms, writes the same IRQ level, waits 50 ms,
releases/reclaims IRQ as an input without pulls, then waits 50 ms. The first
probe uses LOW/0x5d; a safely released failed probe allows HIGH/0x14. Product
ID bytes must begin `911`, followed by a successful status acknowledgement.
Both addresses must be reserved by the board profile before driver admission.

The GPIO ABI has fixed-direction claims. IRQ direction changes therefore use
checked release/reclaim operations. Failed GPIO claims fence the provider,
including failures with no returned token. Rejected I2C claims may fall back
only under the checked no-side-effect safe contract; contradictory claim
results retain dependencies. A failed probe release never permits reset or
reprobe.

## Source behavior retained

The public capability stays `input.touch.raw@1`. There is one subscription,
with monotonically increasing non-reused subscription tokens per loaded ELF.
Coordinates are not rotated, scaled or translated. One contact gets ID 1;
multitouch or an out-of-range coordinate clears active state and signals a
queue gap. A full 32-event queue signals a gap and requires a snapshot. The
home bit remains the primary touch button. Each accepted poll services one
report, for bounds `max_reports` 1 through 16, as in source 0.1.5.

State commits before ACK so an ambiguous ACK does not duplicate edges.
Transient status/point read failures preserve unread queued events and the
last authoritative state. Every public operation is serialized across the
complete report/state transition with the scoped owner-task guard, including
reentrant callbacks. Calls from other tasks fail without side effects.

Quiescence refuses a live subscription without invalidating it. Once no
subscriber remains, teardown closes admission, drains I2C, writes GPIO2 HIGH,
holds it off, releases IRQ and RESET, then uses the checked
`retire_held_output` suffix to hand the held output to CPU boot custody.
Failed releases, off writes, holds or retirement retain exact outstanding
resources for checked cleanup retry. A retained hold result or failed unlock
fences all further calls. `stop` performs no fallible work.

## Checks

Run `minimal/test/run_gt911_test.sh` with `RISCRTE_RUNTIME_ROOT` and
`RISCRTE_READER_ROOT`. The runner composes headers through the canonical SDK
preparer. Set `SANITIZE=1` for ASan/UBSan; under ptrace use
`ASAN_OPTIONS=detect_leaks=0`. Set `NATIVE_DRIVER_CC` to the ESP32-S3 Xtensa GCC
and `PYTHON` to an interpreter with pyelftools for target compilation,
relocation verification, import/export boundary checks and a disassembly
check that forbids provider-BSS compare-and-set instructions.

Host coverage includes exact GPIO/timing traces, configuration rejection
before I/O, raw limits, subscription lifecycle, read/ACK failures, queue
resynchronization, reentry/foreign-task rejection and fallible cleanup.
A 20,000-report independent source-state oracle compares snapshots and event
queues, including randomized input, invalid packets, unsubscribed updates,
queue overflow and retries after ambiguous ACKs. These are host and binary
checks, not touch-panel hardware validation.
