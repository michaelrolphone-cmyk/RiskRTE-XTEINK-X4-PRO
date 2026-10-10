# SSD1677 owner-task presentation progress (panel0.1.23)

The existing provider poll suffix now advances SSD1677 as well as UC8279.
SSD advertises ASYNC_PRESENT; present_status and wait_present(timeout=0) stay
observational. The existing positive-timeout synchronous path remains intact,
including its caller-selected timeout/abort behavior. A tiny positive timeout
is still not a substitute for provider polling.

Each SSD poll sends at most512 pixel bytes, checks time every8 bytes or one
register command of at most5 bytes, and stops at min(requested budget,8ms).
Native GPIO/synchronization callbacks themselves must be bounded; the owner
cannot preempt one callback. Thus the wall-time limit includes one finite
transfer/register quantum beyond the slice. No poll sleeps or spins on BUSY.
The whole asynchronous operation has a10-second deadline and preserves the
existing500ms readiness bounds on either side of the window setup.

Full/partial BW and RED data, cursor/window addressing, control bytes and all
four supported intents retain the synchronous sequence. Explicit previous-image
seeding remains available. SSD does not gain UC's inferred completed history.
Queued/active release, power preparation and quiescence remain refused; there is
no new cancellation or force-release API. Failed GPIO/unlock custody remains
retained. A stuck physical BUSY does not authorize power-off or release.

The UC8279 normal poll body and blocking paths are unchanged. The separately
owned UC8279 fast-waveform package is not edited.

Run the existing minimal/test/run_panel_test.sh with RISCRTE_RUNTIME_ROOT and
RISCRTE_READER_ROOT set to the selected source checkouts. SANITIZE=1 enables
ASan/UBSan; this executor requires ASAN_OPTIONS=detect_leaks=0 because of ptrace.
The suite includes88 cases per mode, exact full/partial and all-intent wire
hash equivalence, owner/reentry, no-work polls, readiness/deadline and retained
failure cases.

minimal/test/run_panel_cadence_test.py now accepts --ssd and --refresh-ms. It
builds the real System adapter, Runtime owner scheduler and panel together.
0/17/2300ms modeled refresh, three modeled GPIO costs and caller waits of
1/8/20/50ms pass normally and with ASan/UBSan. Maximum modeled provider slice is
8ms and touch-sampling gap9ms. These are host-model observations, not hardware
measurements. The historical base-prefix fixture still omits seed_previous, so
its SSD submissions remain full absolute frames; seeded partial equivalence is
covered by the provider suite.

The existing minimal/test/run_panel_target_test.sh compiles a fresh target with
NATIVE_DRIVER_CC and validates bounded imports/exports and relative relocations.
Fresh target receipt and integration evidence are supplied separately. No device
flash, product merge or physical qualification is implied.
