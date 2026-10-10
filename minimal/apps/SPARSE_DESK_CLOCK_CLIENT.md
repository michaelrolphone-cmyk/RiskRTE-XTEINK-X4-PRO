# Optional sparse X4 timer sleep

`PORTABLE_DESK_CLOCK_SPARSE_START` extends the existing `PORTABLE_DESK_CLOCK`
application client. The adapter's pure `portable_desk_adapter_timer_only()`
query selects this path only after the parent admits a validated retained timer
record. Timer admission forces Deep without acquiring a preference store. A
normal foreground invocation still uses the existing Light/Deep preference.

The timer invocation borrows the adapter's existing display and alarm service,
and acquires exactly these three additional grants, in this order:

1. `X4_POWER_CAPABILITY` instance 17
2. `display.output` instance 3, identity-checked against the borrowed display
3. `runtime.retained-wake` instance 0

It never acquires or probes touch, storage volume, Wi-Fi, Bluetooth HCI,
navigation, or battery in order to stop them. It does not load preferences,
announce radios-off policy, or call the Quick Actions brightness accessor.
Power authority remains with the existing power0.1.2 provider, including its
board keepalive transaction and terminal native deep entry. No driver changes
are made by this client.

The owner checks key neutrality before the shared renderer runs. A confirmed
rendered record is staged before any panel preparation. Alarm reconciliation
then checks prepare/step/status, preserves output uncertainty, and bounds deep
entry by the alarm's conservatively shortened deadline. The frontlight is set
to zero before the panel is prepared. Native ordinary refusal restores the
panel first, keeps the timer frontlight at zero, clears the staged record only
after successful restoration, and releases grants in reverse order. The parent
can then promote to foreground. No app yield, KV operation, retained stage/clear,
or grant release is allowed with live prepared holds.

A false acquire can conceal failed-start cleanup retained by Runtime even when
its output is empty. Under the new flag the client stops immediately, preserving
all prior grants. Retained-wake CONTEXT or unknown read/stage outcomes likewise
stop all I/O, including cleanup. A known INVALID stage refusal can be cleared
before release; any clear failure preserves custody. The native entry and typed
panel APIs retain their ordinary/retained/unknown classification. The staged
payload must match the final proposal exactly before native entry.

## Validation

`minimal/test/run_sparse_desk_clock_sleep_test.sh` compiles the actual client,
with deterministic shared-loop and provider/Runtime fixtures. It tests exact
acquisition identity/order, forbidden provider/KV calls, alarms and elapsed
bounds, held and uncertain keys, ordinary/retained/unknown panel and native
results, staged payload custody, acquisition retention (including empty output),
reverse partial release, failed rollback, catch-up and repeated attempts. It
runs both with and without Quick Actions. `SANITIZE=1` enables ASan/UBSan and
uses `ASAN_OPTIONS=detect_leaks=0`. The new-flag foreground Light path also
checks conservative failed-acquire/release handling without changing any
prior flag combination.

These are host composition fixtures, not real providers or hardware tests.
The optional sparse adapter and parent demand-root/profile integration are
separate qualification gates. Build inputs require the canonical typed-power
SDK, pinned Runtime SDK and shared header exposing the new pure adapter query.

Verified on 2026-10-07:

- New client: 110 scenarios × 2 brightness profiles, normal and ASan/UBSan.
- Existing desk lifecycle: 166 scenarios × 2 profiles, normal and ASan/UBSan.
- Existing real paper Clock/adapter/client composition: 45 fresh processes × 2
  profiles, normal and ASan/UBSan, using provider/Runtime fixtures.
- Existing light-only Clock: 28 cases, normal and ASan/UBSan.
- `minimal/test/run_sparse_desk_clock_compatibility_test.sh`: all four prior
  `PORTABLE_DESK_CLOCK`/`PORTABLE_QUICK_ACTIONS` combinations have byte-identical
  preprocessed sources and `-Os` objects against public X4 `f6c12e0`, on host
  GCC14.2 and pinned ESP32-S3 Xtensa GCC8.4. Both sparse brightness profiles also
  compile on each compiler with `-Wall -Wextra -Werror -pedantic`.

Canonical inputs are Reader `aac8c06d3221139084acd0cfc64f7b0ba194a97a`
(the full revision is recorded in the receipt), Runtime
`7e79a8f06c1ee3b71214408c2477d9aaca985834` (0.1.50), and shared Clock baseline
`3bcc9b3` with the agreed sparse adapter header. The compatibility runner
requires `XTENSA_CC` in addition to the same three SDK environment variables.
Exact source/object hashes are in
`test/desk_clock/sparse_sleep_client_validation.json`.

The sparse actual-client tests intentionally substitute the shared loop; they
do not establish real sparse adapter composition or real-provider/native
ownership. Parent demand-profile integration, full target bundle, hardware
keepalive/rail measurements and flashing remain separate gates. No product
source lock, version, provider implementation or BIN changes are included.
