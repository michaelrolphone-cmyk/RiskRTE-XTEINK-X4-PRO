# X4 UC8279 fast provider 0.1.15

This separate opt-in provider implements `display.output@1` for the 800×480
UC8279 ZHX panel at 20 MHz. `x4pro-panel` remains unchanged and is the default
fallback for both UC8279 and SSD1677. Select the new provider explicitly with
`--panel uc8279 --panel-driver uc8279-fast` in profile/bundle generation and
`--panel-driver uc8279-fast` in the provider builder. SSD selection is rejected.

## Hardware-selected mode 1 waveform (0.1.15)

Version 0.1.15 adopts the best overall result from the X4LAB v0.1.9
directional-balance run: mode 1, complementary directional `2+2` at
symmetric +/-14 V with factory VCOM. For active changed windows no taller than
160 rows, each white-to-black and black-to-white phase now occupies two scan
frames (`0x42`/`0x82`) instead of one (`0x41`/`0x81`). The driver explicitly
programs POWER_SETTING `{07,17,3A,3A,03}` after reset and again after every PON;
it does not write VCOM DC, preserving the factory reference.

The hardware comparison found this profile produced the cleanest overall black
and white block motion. The symmetric +/-15 V control improved white clearing
but left a solid gray rectangle behind the moving black target, so maximum
rails are not adopted. Changed-byte windows, truthful DTM1/DTM2 ordering, the
four-frame endpoint redraw, complete plane reconciliation, and POF behavior are
unchanged. Broad 480-row motion still uses the existing one-frame absolute bank.

## Complementary directional overdrive (0.1.14)

Version 0.1.14 preserves the 0.1.13 changed-byte windows and the 0.1.12
four-frame full-target endpoint redraw. For active windows no taller than 160
rows, it replaces two identical absolute target frames with a two-phase
differential overdrive while retaining the same two-phase scan budget:

- uploads the last physically completed target to DTM1 for the exact effective
  window, then uploads the new target to DTM2;
- phase A places VCOM on the high rail and the controller `01` transition bucket
  on the low rail; all other buckets track the high rail;
- phase B places VCOM on the low rail and the controller `10` transition bucket
  on the high rail; all other buckets track the low rail;
- unchanged `00` and `11` pixels track VCOM during both phases and therefore
  receive only the small matched-rail residual rather than the full transition
  field;
- each transition direction receives one opposite-rail source-to-VCOM impulse,
  rather than sharing two identical source-to-ground-style absolute pulses;
- DTM1 deliberately remains the pre-refresh target during resident settling so
  repeated DRF operations reinforce the same completed transition. The next
  active frame always uploads a fresh truthful OLD plane before its NEW plane.

Broader 480-row motion retains the one-frame absolute profile to avoid doubling
a full-screen payload and reducing broad-transition cadence. The 2.3-second
settle, four-frame exact-target endpoint redraw, complete DTM1/DTM2
reconciliation and POF sequence remain unchanged.

This waveform intentionally increases the effective transition field by using
opposite VCOM and source rails. Hardware contrast, current, temperature and
long-run panel behavior remain device-test results rather than host-test claims.

## Tight changed-pixel windows and stronger motion (0.1.13)

Version 0.1.13 preserves the 0.1.12 final-target endpoint redraw, complete
DTM1/DTM2 reconciliation, validated BUSY lifecycle and POF ordering. It changes
only active DEFAULT/LOW_LATENCY motion:

- compares the submitted MONO1 target with the last physically completed target;
- derives a byte-aligned bounding window from actual XOR changes, even when an
  application submits a full-screen damage contract;
- preserves horizontal bounds instead of expanding every fast update to 800
  pixels, while retaining the lab-qualified 40/80/160/480-row gate heights;
- uses a two-frame absolute A2 pulse for effective windows no taller than 160
  rows and retains one frame for broader/full-visible motion;
- completes identical targets without panel I/O and without cancelling an
  already armed final-target settle.

The driver still accepts only the typed 20 MHz UC8279 path. It does not import
40 MHz SPI, counterpulses, voltage changes, undocumented TCON/PLL settings or
the destructive later laboratory sequences. Horizontal partial-window behavior
requires device qualification; the host model verifies RAM cursor mapping,
old/new target preservation, bounded slices and every retained failure path.

## Exact final-target endpoint redraw (0.1.12)

Version 0.1.12 restored one-frame absolute active motion after the differential
path proved too faint, then retained the final target for 2.3 seconds. At the
endpoint it uploads the complete target with hidden rows white, performs one
four-frame full-visible absolute redraw, synchronizes full DTM1 and DTM2, and
issues POF only after validated BUSY completion. No blank, black-fill or inverse
intermediate frame is presented.

## Protocol and source custody

The waveform and window protocol are adapted from T5S3-Reader's standalone
[X4 lab 0.1.5](https://github.com/michaelrolphone-cmyk/T5S3-Reader/tree/05d811ae3a75b0711540484ccdbee32464042dd6/experiments/x4-high-fps),
exact commit `05d811ae3a75b0711540484ccdbee32464042dd6`, modes 4/6/7/9.
`lab-source.json` pins each inspected source blob. The lab's Bayer pattern is
test content; this driver preserves the application's native MONO1 pixels.
No 40/80 MHz experiment, later min-TCON experiment, voltage/VCOM-level/booster
programming, PMIC change, or standalone direct-GPIO abort handler is imported.

A repeated FLG 0x71 / VER 0x70 probe must match, reject floating/ambiguous data,
and identify LUT 0x68 or 0x69 with BUSY_N idle. This provider uses Runtime's
explicit three-wire suffix at 100 kHz for that probe. Runtime owns MOSI input
and pull-up, CS and clock; DC/reset/BUSY retain their separate scoped GPIO.
The native three-wire probe is a new integration route and still needs hardware
qualification, even though the subsequent selected lab protocol was tested.

After reset, a presentation without reconstructed history and every explicit CLEAN use the source's
OTP full-clean baseline: white DTM1, new DTM2, genuine BUSY assertion/completion,
and new DTM1 synchronization (three 60 KB controller planes including 120 blank
rows). Later DEFAULT/LOW_LATENCY presentations use external A2 at PLL 0x0F.
Effective windows up to 160 rows use the complementary two-phase differential
bank. Registers 0x20, 0x21 and 0x24 encode high then low (`0x42,0x82`), register
0x22 holds low for both two-frame phases (`0x82,0x82`), and register 0x23 holds
high for both (`0x42,0x42`). This makes one transition active only in phase A and the
other only in phase B while unchanged buckets track VCOM. Broader/full-visible
motion retains the corrected one-frame absolute bank (`0x81/0x41`).

PTIN/PTL establishes the XOR-derived, byte-aligned horizontal window and the
smallest tested 40/80/160/480-row vertical window. Complementary overdrive
uploads DTM1 OLD and DTM2 NEW inside that same window; broad absolute motion
uploads DTM2 only. PTOUT closes the RAM phase before external PSR, PFS, gate
scan, CDI, CCSET, TSSET, LUT, PON-if-needed and DRF. Narrow/medium payload is
twice the effective window bytes; broad absolute payload is one plane. Bytes in
the expanded gate window but outside submitted damage come from the physically
completed image, preventing unrelated caller edits from becoming visible. Only
accepted, completed pixels become history. Caller seeding does not qualify an
unshown image for the fast mode. Resume clears inferred history; a validated
caller reconstruction can seed an OTP QUALITY partial, otherwise the next frame
uses a full baseline.

## Ownership, timing and failure behavior

Requires Runtime 0.1.58's optional `garden_spi_v1.claim_three_wire` suffix.
Legacy SPI semantics and global GPIO scopes are unchanged. All code executes
on the existing serialized owner, with normal provider sync and frame leases.
CS stays held across every plane; exchange buffers are at most 512 bytes.
Each poll checks time between exchanges and is capped at 16,384 payload bytes
and a requested 8 ms slice. Native exchange/end waits are capped at 8 ms and
remaining transaction budget; SDK tick rounding/setup can exceed the requested
wall-clock interval. The overall plane deadline is 1 second and the overall
presentation deadline is 10 seconds. Runtime and the ordinary app adapter keep
input polling cooperative while the panel is transferring or BUSY.

Every PON and DRF must assert BUSY_N within 100 ms; DRF must finish within
3.5 seconds. Missed assertion never counts as a fast success. SPI/GPIO failures
retain mapped ownership; failed drain/release never discards a pending token.
A failed/uncertain presentation blocks further frame acquisition and power
preparation until restart, because PTIN or an active waveform may remain.
Power preparation, reset holds, retirement and cleanup retries otherwise retain
the existing typed lifecycle. Snapshot metrics report actual payload/GPIO counts.
CLEAN_PRESENT is deliberately not advertised: ordinary application full-scene
redraws in the paired interactive app cohort use LOW_LATENCY and stay fast after
the baseline. QUALITY now selects stock OTP; explicit maintenance CLEAN is still accepted. For CLEAN, transfer-end is the end of the pre-refresh upload; payload also
includes its post-BUSY DTM1 sync before COMPLETE.

The nominal 10 Hz / 100 ms capability values are scheduling hints derived from
the selected lab mode. The user supplied a 9.97 FPS full-screen comparison
baseline; its raw log was not recovered in this task. An earlier reported lab
summary was 9.79 FPS (approximately 20.1 ms upload and 81.4 ms BUSY). Neither is
an integrated production-driver measurement. Host timings model wire time and
BUSY inputs and cannot establish visible latency, ghosting or battery current.

## Verification and hardware recipe

Run `minimal/test/run_uc8279_fast_test.sh`, its `SANITIZE=1` variant,
`minimal/test/run_uc8279_fast_target_test.sh`,
`minimal/test/uc8279_fast_profile_test.py`, and the actual System-adapter/Runtime
fixture `minimal/test/run_uc8279_fast_cadence_test.py`. The fallback panel and
board-graph suites must continue to pass. LeakSanitizer is unavailable under
this executor's ptrace environment; ASan/UBSan run with `detect_leaks=0`.

For a separately authorized hardware test, select the new provider explicitly,
verify the repeated probe and recorded 20 MHz typed profile, then compare full
normal frames with the user-reported 9.97 FPS baseline. Record submission,
transfer, DRF, BUSY assertion/completion, payload counts and completed tokens.
Exercise narrow horizontal windows and 40/80/160-row bands at several locations,
full frames, identical no-op targets, CLEAN, long repeated updates, failed/missed
BUSY, touch during transfers, power refusal, sleep/resume and fallback selection.
Confirm small/medium windows upload one OLD and one NEW plane, emit the
`41/81` complementary VCOM bank, select one direction per phase, and retain the
same two-phase BUSY cadence. Confirm broad windows retain one-frame absolute
DTM2-only operation and that endpoint redraw/POF behavior remains unchanged.
Record transition contrast in both optical directions, temperature and supply
current in addition to UI text and motion quality.
No hardware was accessed or firmware flashed for this change.

## Completed-image snapshot (0.1.1)

The optional `RiscDisplayOutputSnapshotV1.h` suffix follows the unchanged
history/power/metrics descriptor prefixes. `copy_completed` copies only the
physically completed MONO1 image into caller-owned memory, in native coordinates.
It performs no hardware or clock operation, frame acquisition, presentation,
allocation or sleep. No caller pointer or token is retained. The owner must be
awake and ready, with valid completed history and no leased or in-flight frame.
Seed-only images, failed presentations, sleep/resume and retained failures deny
availability until another frame completes.

The caller supplies MONO1, stride at least 100 and size at least stride×480.
Only the 100 visible bytes per row are copied; padding is preserved. Invalid
format/capacity, integer overflow and overlap with provider buffers or descriptor
are rejected before any destination write. Absence or refusal tells the app to
render without a transition. Tests cover descriptor size/tag/version/function
validation, completed-image fidelity, padding, stale lease/history aliases,
owner/retained/leased/queued/active/seed-only/sleep/resume refusal, destination
purity and no hardware/clock/lease changes.

## Absolute target polarity correction (0.1.2)

The lab's literal register21..24 rails were `41,81,41,81`. The primary FreeInk
X4 absolute grayscale encoding, inverted plane writes and empirical register
assignment corroborate wire `{DTM1,DTM2}` selecting `00→24,01→22,10→23,11→21`.
Its X4 AA bank corroborates `0x4x` as black-directed and `0x8x` as white-directed.
This implies the old absolute bank selected an inverse of the stale OLD bit;
it did not make the NEW target independent of OLD as intended.

The correction sets21..24 to `81,81,41,41`: both NEW=1 buckets whiten and both
NEW=0 buckets blacken. Only21 and24 change. SDK MONO1 remains1=black, and its
conversion to the controller's complemented wire bytes remains intact. SPI,
geometry, PLL, one-frame pulse count, transfer volume, BUSY checks and command
order are unchanged. No settling repetitions or UI changes are included.

This mapping is corroborated stock-source inference, not an independently
measured four-plane controller truth table or a directly retrieved UC8279
silicon datasheet. The new test model decodes emitted LUTs and both RAM planes,
and it reproduces the old failure before the correction. Physical confirmation
of polarity/contrast remains pending. `polarity-source.json` pins the primary
sources and the inferred mapping for review.


## Resident-image settling (introduced 0.1.3; duration extended in 0.1.4)

After each fast target genuinely completes BUSY, the provider releases its frame
lease and keeps refreshing the resident controller image for 2,300 ms. Each
repeat sends only PTIN, PTL, DRF and PTOUT. It sends no DTM1/DTM2 pixels and does
not change the selected 20 MHz transport, 800x600 controller geometry, visible
800x480 offset 120, PLL 0x0F or one-frame LUT. The first baseline and explicit
CLEAN retain their existing OTP behavior and do not arm extra repeats.

A valid newer submission immediately stops further old-target repeats. Any
already-issued DRF must genuinely assert and finish BUSY before the replacement
upload starts. Unfinished windows accumulate across consecutive partial targets;
their vertical union expands only to tested 40/80/160/480-row windows. This lets
the final resident refresh settle all recently changed bands without uploading
those bands again. The 2,300 ms window restarts only on a newly completed target,
never on a repeat. No new DRF starts at or beyond the deadline; an already active
pulse is observed through completion and closed before the provider becomes idle.

Settling runs through the existing provider poll callback even after the original
presentation token completes. Each callback uses the existing 8 ms budget and at
most eight control steps, then returns to input/scheduler work. Original token
status and presentation metrics stay complete and unchanged. Diagnostics append
settling state/completed/issued counts. A repeat failure invalidates history and
blocks reuse and sleep; it does not retroactively fail an already completed
token, but does fail a queued replacement. Failed bus drain keeps its token.

Power preparation and teardown cancel new repeats and drain only an already
issued pulse within the caller's existing total budget. A healthy pending repeat
does not require a power caller to retry BUSY. Insufficient budget returns a
truthful timeout, and uncertain BUSY or I/O retains resources. Resume discards
settling and requires a new baseline. Light sleep that pauses owner polling can
finish the electrical pulse while asleep; subsequent polling observes completion
and the elapsed deadline without restarting expired repetitions.

Focused tests cover zero pixel retransmission, unchanged metrics, idle no-work,
2,300 ms bounds, one-ms pulses, supersession at every repeat phase, accumulated
separate bands, replacement via wait_present, CLEAN, sleep/teardown, short sleep
budgets, missing/stuck BUSY, queued failure, SPI/GPIO faults, clock failures and
owner/budget guards. The real System adapter plus Runtime fixture continues
polling inputs with no presentation token during settling: at 1/8/20/50 ms waits,
modeled completed repeats were 110/72/58/24, all with zero pixel payload and
maximum input gaps of 2/8/20/50 ms. These are 20 ms BUSY model measurements,
not physical panel quality or power measurements. Hardware settling remains to
be tested; this change does not flash a device.


## Desk-clock QUALITY contract (0.1.5)

Intent dispatch is explicit:

- DEFAULT(0) and LOW_LATENCY(1): existing absolute one-frame A2 and2.3-second
  resident-image settling after completed history; full OTP baseline if absent.
- QUALITY(2): stock OTP partial when nonempty damage has completed history or
  a valid `seed_previous` reconstruction. Otherwise full OTP baseline.
- CLEAN(3): full OTP baseline, regardless of damage/history.

QUALITY partial uses the pre-existing X4 stock path from `x4pro_panel`: PLL0x0E,
CDI0xD7, CCSET0x02, TSSET0x5A, PFS0x20, gate scan0x02, PON if needed, then exact
aligned PTIN/PTL and post-PON OTP PSR `17 4D`. This is the normal built-in partial
waveform, not the external one-frame A2 LUT. CLEAN/cold baseline retains CDI0x97
and TSSET0x1E. Geometry stays800×600 gates with120 blank rows; SPI stays20MHz.
No voltage, TCON, compact-geometry or overclock settings are introduced.

Before each QUALITY partial, the provider uploads a complete merged NEW plane
and the canonical completed/seeded OLD plane. This repairs stale OLD RAM left by
A2 and controller RAM lost across reset. After observed BUSY completion it closes
the partial window and synchronizes OLD from the merged completed image. Each
normal partial transfers three60KB planes(180KB total), while only the exact
aligned damage window receives the physical refresh. Rows/bytes outside the
submitted damage union remain the prior visible image. No repeated A2 settling
is armed. An existing settling pulse drains before the quality transfer begins.

Deep-wake caller sequence: acquire MONO1; reconstruct the exact prior visible
image; call `seed_previous`; render the changed clock image into the same lease;
submit native-coordinate damage with QUALITY; wait for completion. Seeding is
an assertion about the visible image and does not itself make the read-only
completed snapshot available. A successful quality frame restores completed
history. Invalid/missing reconstruction must use CLEAN/full baseline instead.

Compatibility: old app builds used QUALITY for all MONO1 output while provider
versions through0.1.4 treated it as fast. Those apps will now receive OTP.
The paired interactive adapter must explicitly select LOW_LATENCY; the selected
`PORTABLE_PAPER_TRANSITIONS` adapter does so. The Clock-specific lock/wake and
orientation policy belongs to the app, not this provider. No Runtime API change
is required. All timing fixtures are host models; physical quality remains a
separate check.


## Awake resident maintenance (0.1.6; removed in 0.1.9)

After a completed interactive fast frame finishes its 2.3-second settling,
ordinary owner polling schedules one full-visible-area resident refresh about
30 seconds later. That single pulse is the bounded maintenance burst. The next
30-second interval starts when its BUSY assertion/completion and PTOUT finish.
Late polling performs one pulse and does not catch up missed intervals.

The maintenance pulse reuses the selected absolute bank and resident DTM2 image.
It uploads no pixels, rerasterizes no app, creates no presentation token, and
leaves the completed frame, snapshot and presentation metrics intact. All 480
visible rows are covered, including unchanged white areas outside recent damage.
The same bounded resident state machine handles both settling and maintenance;
they cannot overlap. Diagnostics append `idle=enabled/active/due_ms` after the
resident scheduler counters.

Every accepted new target cancels the pending deadline. An active pulse drains
before that target starts; a successful subsequent fast target rearms settling
and then maintenance. QUALITY, CLEAN, reconstruction-only history and cold OTP
baseline do not arm maintenance. Power preparation cancels it and drains only
an active pulse within the caller's existing budget. Resume leaves it disabled
until a new fast frame completes. This excludes the locked desk clock and
prevents background wake-ups. There is no sleep timer, app work request, input
activity update or input-idle reset.

The host model verifies the boundary, late polling, owner/budget/lease gates,
replacement, quality and sleep cancellation, unchanged resident bytes/metrics,
and retained failures. The actual adapter/Runtime cadence fixture verifies one
zero-payload event after 30 seconds while input polling continues. These checks
do not establish physical contrast, power consumption, or recovery from earlier
panel experiments; those observations remain a hardware qualification step.


## Plane-coherent idle and bounded fast rendering (0.1.9)

Version 0.1.9 retains the delayed-owner-poll fixes from 0.1.8 and changes the
rendering lifecycle based on the subsequent panel diagnostics:

- removes the 30-second full-visible no-upload resident DRF;
- makes DEFAULT a one-frame differential update whenever DTM1 is trustworthy;
- keeps LOW_LATENCY as a bounded one-frame absolute burst (16 frames or 2 s);
- ends a stale absolute burst with a two-frame target update and full DTM1 reseed;
- after 2.3 s without a replacement frame, writes the complete current image to
  both 800x600 controller planes and issues POF without entering deep sleep;
- wakes only as part of a queued presentation and replays PSR, timing controls
  and the external LUT after PON before DRF;
- invalidates controller-plane state on every uncertain BUSY, SPI, GPIO, reset,
  or lifecycle failure.

The host completed-image shadow remains authoritative. The idle plane rewrite
uses white hidden rows and the full visible target, so a later refresh cannot
replay mixed-generation DTM2 bands. No inversion/counterpulse, voltage, TCON,
compact geometry, SPI overclock, undocumented PLL, PMIC or battery changes are
included.


## Optional frontlight tone forwarding (0.1.10)

The optional `RiscDisplayOutputFrontlightV1.h` suffix follows the exact existing
base/history/power/metrics/snapshot prefixes. It forwards the dimensionless
cool-to-warm ratio through the already-required `display.frontlight@1`
dependency's optional `RiscFrontlightToneV1.h` suffix. Zero is cool, maximum is
warm, and midpoint is neutral; these values are not calibrated Kelvin claims.
The underlying provider preserves logical brightness, including OFF.

Both calls require normal serialized owner admission, a started provider and
an awake lifecycle. Tone does not acquire or release a frame, submit a
presentation, advance a waveform, read the clock, or perform panel I/O. Existing
rendering, settling and plane-coherence state is unchanged. Only the underlying
frontlight callback may touch its output. No rollback, retry or cleanup I/O is
issued after a failed callback.

`set_tone` returns false for a zero maximum, an out-of-range value, an absent or
malformed suffix, a provider refusal, or failed admission. `get_tone` returns
OK (0) only after a successful callback returns a nonzero maximum and a valid
ratio; UNAVAILABLE (1) only when an otherwise admitted dependency lacks a valid
tone suffix; FAILED (-1) for provider failure, invalid arguments or lifecycle
failure. Output pointers must be distinct and non-null, and remain unchanged
unless the complete operation succeeds, including owner unlock.

Host tests cover legacy exact-size and malformed descriptors, logical OFF,
readback validation, callback failures, invalid arguments, reentry, non-owner
and lifecycle refusal, retained/unlock failures, and unchanged display activity.


## Explicit sleep-overlay settling (0.1.11)

This successor preserves the exact 0.1.10 ordinary rendering state machine.
It adds an optional token-bound settling request/status suffix after the exact
frontlight tone prefix. Only an owner explicitly preparing a final sleep image
uses it. Normal app/GameBoy frame completion, burst policy, RAM synchronization,
2.3-second quiet period and POF/PON cadence are unchanged.

A request for the just-completed overlay enables resident settling for that
absolute target if its existing burst policy selected quiet WAIT. Matching
status reports PENDING until the repeats, dual-plane sync and validated POF
finish; invalid/newer tokens and uncertain custody fail. The callbacks do not
lease frames, perform panel I/O or sample time. Ordinary Runtime polling does
the work. A newer frame preempts exactly as before.

Home explicitly awaits this condition only before sleep, keeping input active
so fresh contact or navigation cancels the sleep and restores the foreground.
The separate transition/cadence investigation is NOT part of this version.
Physical panel contrast remains unverified.
