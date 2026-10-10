# X4 GT911 ordinary provider

Package `x4pro-gt911` 0.1.10 extends the Reader source 0.1.5 at
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

## Contact reports and retained behavior

The public capability stays `input.touch.raw@1`. Up to four independent
subscribers have monotonically increasing non-reused tokens per loaded ELF.
Coordinates remain raw 480×800 and are not rotated, scaled or translated.
The provider accepts zero through five actual controller contacts. A READY
status at `0x814e` supplies the count; one bounded read starts at `0x814f`
and reads exactly eight bytes per contact: track ID, little-endian X, Y and
size, then a reserved byte. The former `0x8150` read omitted the track ID.

Hardware finger IDs 0–15 map to public IDs 1–16, independent of record order.
Hardware ID 0 keeps legacy public ID 1. Snapshots are sorted by public ID.
Disappearing IDs emit UP at their last coordinates before new IDs emit DOWN;
all new DOWNs precede retained IDs' MOVEs within that same atomic report.
Each phase stays sorted by ID. Retained IDs emit MOVE only when coordinates change. Reordering records alone
emits nothing. Size and reserved bytes are not coordinates or extra contacts.
No second contact is synthesized for controllers reporting one finger.

This order lets gesture consumers cancel on a second contact before acting on
simultaneous motion, even when a later unchanged ready report has advanced the
snapshot timestamp. No edge is removed or reordered across hardware reports;
independent earlier gestures and same-millisecond UP/DOWN sequences survive.

The complete report is validated before any event or snapshot update. Counts
above five, duplicate IDs, special/non-finger IDs (including HotKnot ID 32),
or out-of-range coordinates invalidate active state and signal GAP to every
subscriber, then attempt to acknowledge the discarded report. Sequence
capacity is checked for the full transition before emitting any event.
A full 32-event queue signals GAP only to the affected subscriber; its queued
prefix is discarded and its consumer must take a snapshot. The Home status
bit remains the primary touch button. Each accepted poll services one report,
for bounds `max_reports` 1 through 16, as in source 0.1.5.

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

## Reversible loaded-provider power lifecycle

`RiscTouchPowerV1.h` is a canonical Reader SDK addition. Discover the optional
`risc_touch_power_api_v1` suffix with `risc_touch_power(api)`; the tag is `TPW1`,
version 1, and the `risc_touch_api_v1` prefix is unchanged. The provider still
publishes only `input.touch.raw@1`. This version adds no imports or raw/native
hardware access. The startup trace and power/custody implementation are
unchanged by the multi-contact extension. Normal hardware-ID-0 single-touch
events retain the source 0.1.5 behavior.

Both `prepare(context, timeout_ms)` and `resume(context, timeout_ms)` are
serialized owner-executor calls on the same loaded provider. Timeout zero
performs a non-mutating state poll; positive budgets are capped at 1000 ms.
Outcomes are OK (0), BUSY (-1), TIMEOUT (-2), UNAVAILABLE (-3), RETAINED (-4),
PLATFORM (-5), and INVALID (-6, oversized budget). Other tasks and reentry are
BUSY. Calls before successful startup or during terminal shutdown are
UNAVAILABLE. The caller keeps its provider grant and all dependencies alive.

Prepare refuses any live subscription without altering its queued events or
snapshot. After admission it fences ordinary input, discards old contact/Home
state and events, releases the exact I2C claim, writes GPIO2 HIGH, and holds it
off. It preserves the loaded provider, lock and owned GPIO tokens. Repeated
successful preparation performs no additional hardware work.

Resume can complete partially refused preparation, then disables hold, writes
GPIO2 LOW, waits 50 ms and runs the same scoped reset/address-selection waits
as startup. It checks 0x5d first, then 0x14 only after a confirmed previous claim
release. Every fallible step persists its exact stage and token. A failed bus
or GPIO release leaves that same token pending and prevents later claims,
reset or address fallback until a checked retry succeeds. An ordinary missing
chip or failed probe with confirmed release returns PLATFORM and stays fenced;
a later resume can retry, or prepare can cancel partial recovery safely.

Failed GPIO claims/writes, any failed unhold, retained hold-enable results,
contradictory I2C claim returns and failed unlock permanently return RETAINED.
No later I/O or successful unload is possible for that instance. A normal
hold-enable refusal has confirmed rollback and may be retried. Terminal
quiescence also handles prepared and partially recovered stages without
forgetting outstanding claims, held outputs or pin ownership.

The finite state machine has no unbounded retry or wait loop. Each I2C transfer
receives at most the remaining budget and at most 20 ms. Requested waits and
maximum issued transfer time are conservatively charged even if the clock
stalls; elapsed time is also checked before every stage. A backwards clock or
expired deadline returns TIMEOUT with the precise completed stage preserved.
Scoped GPIO/claim/release operations are nonblocking. Scheduler latency and
the mandatory unlock may exceed the cooperative deadline; expired work never
reports success. An earlier operation failure remains the reported failure,
except an uncertain unlock upgrades it to RETAINED.

Successful recovery exposes neutral snapshots and suppresses all contact and
Home edges until a fresh, valid, acknowledged all-neutral controller report.
Missing READY, malformed input, read failure or ambiguous ACK does not rearm
input. Only subsequent new presses can activate apps. Subscription tokens are
never resurrected. This is recovery after a returned/refused sleep request;
a real deep-sleep wake resets the MCU and takes ordinary startup instead.

The 0.1.9 change is source/host/ELF work; delivered product images are unchanged.
It does not alter the sleep coordinator or perform system sleep entry.

## Protocol evidence and hardware limit

The register layout and capacity are documented in Goodix's
[GT911 Programming Guide](https://www.crystalfontz.com/controllers/uploaded/GT911ProgrammingGuide.pdf),
sections 3.2 (Touch Number at `0x804c`) and 3.3 (coordinate reports), pages 5
and 14–16. The maintained [Linux Goodix driver](https://github.com/torvalds/linux/blob/master/drivers/input/touchscreen/goodix.c)
also decodes eight-byte GT9x contact records with a low-nibble track ID.
This provider implements that documented wire format independently.

The GT911 supports up to five contacts, but the fitted panel and its current
controller configuration determine how many are actually reported. This
change does not write or replace controller configuration. A physical X4
must still demonstrate two distinct IDs in a simultaneous two-finger report;
host tests and a compiled ELF cannot establish that the panel does so. If
the controller remains configured for one point, the provider continues to
report that one actual point. Multi-finger scroll remains hardware-unverified.

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
A 20,000-report independent source-state oracle compares single-touch
snapshots and event queues, including randomized input, invalid packets,
unsubscribed updates, queue overflow and retries after ambiguous ACKs. Its
malformed-count case now uses six contacts because two are valid.

The multi-contact suite links the production driver and tests a literal
wire report, record reorder, stable IDs through individual lifts and
replacement, all five contact slots, invalid count/ID/duplicate/coordinate
cases, all 16 short-read prefixes of a two-contact report, uncertain ACKs,
overflow during a report with multiple subscribers, subscription cleanup,
and held two-finger input across power recovery and the neutral gate. The
HID integration fixture now forwards every actual fixture contact to its
matching wire ID. These are host and binary checks, not panel validation.

`minimal/test/run_hid_gt911_test.py` additionally links the actual provider,
HID app and shared UI adapter. Its default suite includes tap-drag/release,
horizontal/vertical locked scrolling and free diagonal scrolling, with the
scroll API suffix supplied by the shared Utilities renderer fixture. It also
retains reconnect, click, keys, pairing, Home/Back and cleanup scenarios. Run
with a matching Utilities checkout that includes those four gesture scenes;
the full selected suite exercises 40 app/scene combinations across normal
and ASan/UBSan builds. HID packets and physical touch are host doubles.

The power suite adds 49 named scenarios, including full primary/alternate
GPIO failure matrices, every probe-transfer failure, held contact/Home and
ambiguous neutral ACKs, stale subscriptions, owner/reentry checks, repeated
recovery with stable power/reset custody, conservative stalled-clock budgets,
non-monotonic/late clocks, and 33-point recovery/terminal-cleanup expiry
matrices. All 77 existing named scenarios and the 20,000-report source oracle
still run, plus eight multi-contact scenarios.
