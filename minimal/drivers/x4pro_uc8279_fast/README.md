# X4 UC8279 fast provider 0.1.2

This separate opt-in provider implements `display.output@1` for the 800×480
UC8279 ZHX panel at 20 MHz. `x4pro-panel` remains unchanged and is the default
fallback for both UC8279 and SSD1677. Select the new provider explicitly with
`--panel uc8279 --panel-driver uc8279-fast` in profile/bundle generation and
`--panel-driver uc8279-fast` in the provider builder. SSD selection is rejected.

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

After reset, the first presentation and every explicit CLEAN use the source's
OTP full-clean baseline: white DTM1, new DTM2, genuine BUSY assertion/completion,
and new DTM1 synchronization (three 60 KB controller planes including 120 blank
rows). Normal later presentations use the one-frame absolute target LUT at PLL
0x0F: white-target entries 0x81, black-target entries 0x41, VCOM entry 0x01.
Version0.1.2 corrects registers0x21/0x24 relative to the laboratory; every
remaining42-byte table field and one-frame duration stays unchanged. No old-plane transfer occurs
in that mode. Full visible updates transfer **48,000 bytes**; 40/80/160-row
bands transfer **4,000/8,000/16,000 bytes**.

PTIN/PTL establishes a full-width window before DTM2, then PTOUT closes the RAM
phase. The source's PTIN/PTL, external PSR, PFS, gate scan, CDI, CCSET, TSSET, LUT,
PON-if-needed, DRF and PTOUT sequence follows. Partial damage expands its vertical
union to the smallest tested 40/80/160/480-row height; the source's full-width
RAM protocol is retained. Bytes outside the aligned submitted damage union come from the physically completed image,
preventing unrelated caller edits from becoming visible. Only accepted,
completed pixels become history. Caller seeding does not qualify an unshown
image for the fast mode. A resume requires a new baseline.

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
redraws use QUALITY and stay fast after the baseline, while explicit maintenance
CLEAN is still accepted. For CLEAN, transfer-end is the end of the pre-refresh upload; payload also
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
Exercise 40/80/160-row bands at several locations, full frames, CLEAN, long
repeated updates, failed/missed BUSY, touch during transfers, power refusal,
sleep/resume and fallback selection. Confirm normal fast frames have no DTM1
sync and that image quality is acceptable for actual UI text and transitions.
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
