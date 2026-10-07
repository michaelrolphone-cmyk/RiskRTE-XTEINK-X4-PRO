# Opt-in typed X4 desk-clock client

`PORTABLE_DESK_CLOCK` adds application-owned coordination to `portable_sleep.c`.
Without that macro, the preprocessed source and GCC host object remain identical
to the light-only client at `cf0120d`. The shared paper Clock, renderer, settings,
and adapter are separate System Apps dependencies. No driver, source lock,
provisioning image, or default deployment selection changes here.

## Authority and ordering

The namespace-1 sleep preference admits only Light/Deep (mask 3), with Light as
the missing, corrupt, unreadable, unknown, or stale-Hybrid fallback. Deep requires
checked typed display power, touch power, storage sleep, X4 power, retained-wake,
Wi-Fi, and Bluetooth interfaces. Explicit instances are X4 power17, Wi-Fi15,
Bluetooth16, and Runtime retained-wake0; other provider bindings must be unique.
Missing/older interfaces refuse before preparation and produce the shared
one-shot failure notice. No touch subscription or display frame may remain at
provider preparation. The tests enforce the Runtime's 16 live-grant ceiling.

1. Acquire and validate all required interfaces; preserve grants through entry.
2. Observe continuously released owned GPIO3 for 30 ms, bounded to 80 samples.
   Held entry/wake input never becomes a command. Uncertain key-read custody
   propagates `-2`, including while the shared renderer holds a frame.
3. Shared code paints, checks presentation, and stages the candidate while no
   provider pad holds exist. The client then drains Wi-Fi with
   `disconnect_checked` and proves DOWN, then disables Bluetooth and proves OFF.
   Only then does it mark the shared radios-off refusal notice. It does not
   rewrite preferences, reconnect, or automatically restore Bluetooth.
4. Reconcile alarms with checked `prepare_sleep`/`step`/copied status. Failure or
   uncertainty is never treated as readiness. After an OK decision no further
   alarm-service/writer call intervenes before entry; preparation is serialized.
5. Turn off the frontlight, prepare touch and display, then use the distinct
   zero-handle `prepare_sleep`/`commit_sleep` storage transaction. The legacy
   terminal `commit_power_down` API is never called.
6. Shared code anchors a qualified RTC second before staging/preparation, then
   checks time after preparation without app yielding or polling the graph.
   Bounded minute-boundary catch-up resumes and clears before a full repaint.
   The client compares the proposal to the exact already-staged encoded record,
   subtracts elapsed time, and enters with the shorter clock/alarm bound. Whole-second alarm deadlines
   are conservatively shortened by one second, minimum 1 ms. Expired bounds
   refuse rather than receiving a fresh unchanged timer duration.
7. Native success never returns. Ordinary refusal resumes storage, display,
   touch and brightness in checked reverse order, then clears the pending
   record. Runtime stage/clear are never attempted while a pad hold is live.
   Storage MEDIA_UNAVAILABLE confirms rail custody but does not claim usable
   media. Unknown/refused rollback and any RETAINED outcome stop all further
   I/O, restore, release and unload attempts. CPU/native entry latency remains
   outside the app's final timestamp correction; hardware timing is unqualified.

Only `portable_desk_clock_boot_read`, called by shared `app_main`, reads the
Runtime-owned classified record. It validates type/schema/payload and accepts
only a TIMER cause, then checks grant release before returning the decoded value.
It performs no painting, preparation, staging, or entry during boot record read.

## Verification

Provide `RISCRTE_RUNTIME_ROOT` with `sdk/app/RiscRetainedWakeV1.h`,
`RISCRTE_APPS_ROOT` with the matching shared Clock/client header,
`RISCRTE_DESK_SDK_ROOT` with canonical typed driver suffixes, and optionally
`RISCRTE_SETTINGS_ROOT` for the mask-aware `PortableSleepPolicy.h`.

- `minimal/test/run_desk_clock_sleep_test.sh`: 166 focused scenarios × 2
  brightness profiles. Includes missing/malformed grants, every typed prepare
  error, rollback failures, resource retention, cancellation, alarms, radio
  cleanup/readback, native refusal codes, staged-record cleanup, and boot causes.
- `minimal/test/run_desk_clock_typed_app_test.sh`: real paper Clock, adapter,
  renderer and X4 client, with typed provider/Runtime fakes. 45 fresh processes
  × 2 Quick Actions/Quick Radios profiles include terminal entry, partial
  refusal, retained custody, fresh timer reconstruction with exact previous
  pixels within the same acquired frame, retained seed failure, full-refresh
  cadence, and GPIO boot without seeding. Persisted BT-On
  is never reapplied during a timer-resume-to-deep cycle. Strict storage gates
  reject stage/clear, app yield, KV I/O and grant release while prepared, matching
  Runtime `src/bootstrap/RetainedWakeRuntime.inc` and
  `src/ports/esp32s3/CpuPort.cpp::providerStorageSafe`. Runtime
  `test/run_retained_wake_test.sh` owns the native CPU/Runtime barrier tests.
- `minimal/test/run_clock_sleep_test.sh`: existing 28 default light cases.
- All three runners pass with `SANITIZE=1` (ASan+UBSan, leak detection disabled
  for the executor). The default-light preprocessed source and `-Os` GCC14
  object compare byte-for-byte with `cf0120d`; object SHA-256:
  `0f512ef14dca1cffaccc529eb249accc29279508d9af54f92388280cead419e0`.

The client also compiles with the pinned Xtensa ESP32-S3 GCC8.4 toolchain.
The first two are host composition tests, not real hardware-provider execution.
A final locked graph, target bundle, retained Runtime/native CPU qualification,
radio/rail measurements, full BIN, and device flashing remain integration gates.
