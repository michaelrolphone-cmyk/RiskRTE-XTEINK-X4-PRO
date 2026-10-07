# X4 clock light sleep checkpoint

This opt-in deployment replaces the main clock's completed short top-right
`SLEEP NOT AVAILABLE` action with synchronous light sleep. The accepted Watch
crown mapping uses bit 2 for the completed short event (despite a stale comment
in crown.c describing it as long). A press in Quick Actions dismisses that modal;
a subsequent completed short press from the clock sleeps. Long presses and page
chords remain cancelled by the existing navigation debounce policy.

## Ownership and recovery

`x4pro-power` 0.1.0 owns GPIO3 at hardware instance 17 and exports the explicit
`x4.power@1` table. Its input token never escapes. `x4pro-power-buttons` 0.1.0
owns only GPIO0/7 at instance 6 and depends explicitly on power17 for key reads.
It exports only the existing navigation traits table, never a power suffix.
The generic buttons package 0.1.8 remains usable with the legacy three-pin graph.
Other hardware IDs, profiles and bindings are unchanged; Wi-Fi15 and BLE16 are
not reused. The default nine-provider graph stays selected without `--sleep`.

Only the opted-in Clock manifest and boot policy receive `x4.power@17`.
The hook requires 30ms observed key release with a bounded 400ms wait, then the
provider independently checks monotonic neutrality before calling the CPU port.
Every native attempt consumes neutrality. Wake/reset drops held input until a
fresh neutral sample; it cannot launch an app or immediately sleep again.

The clock closes touch, holds no display lease, preserves the completed e-paper
image and RAM, turns the frontlight off, and reconciles alarms immediately before
entry. An existing deadline selects bounded timed light sleep. Wake or ordinary
native refusal restores the invocation's confirmed frontlight preference and
reopens touch/navigation; native busy/active-wake refusal cannot spin-retry.
Native RETAINED returns immediately without provider restore, yielding, freeing
state or releasing grants. Provider failed release/destroy keeps claims for a
cleanup retry; uncertain unlock/native cleanup fences the provider permanently.
Runtime 0.1.44 supplies the existing pre-fini native retention barrier.

This checkpoint deliberately has **no deep/hybrid sleep, automatic idle sleep,
touch wake, panel rail shutdown or hardware qualification**. GPIO3 wakes the
light-sleep CPU, while an alarm may wake it by timer. The board rail and panel
state remain powered. No energy-use claim is made. No device is accessed.

## Integration

The original unit used Runtime `63d607b18c635533e97098235c30e845b249533d` (0.1.44).
The integrated product uses Runtime `a768245998ba334ef54da92f6573ae01b2320a4e` (0.1.45) and shared Reader
`cccfa3fd9b606998c27adb03665722ea44d5358f`. Use System Apps
`2aa0cf63346e507525af884bbfbf6b69313442c4` based on the controls
commit `2775b0b898c432825d9cb5685175c0647dd4dc30`.

1. Run `minimal/scripts/build_drivers.py` with its normal locked inputs plus
   `--sleep`. This builds the new power and distinct split-navigation packages.
2. Run System Apps `scripts/build_paper_clock.py` with `--navigation
   --alarm-client --quick-actions --local-sleep-source /absolute/x4/minimal/apps/portable_sleep.c
   --sleep-capability x4.power --sleep-sdk /absolute/runtime/sdk/driver` and a
   fresh `--output-dir`. Sleep Clock uses version0.2.1. Other flags and apps
   retain their existing controls selection; do not silently grant quick radios.
3. Pass `--sleep` to `minimal/scripts/build_test_bundle.py`. It selects the
   split graph and rejects power authority on any non-default app. The normal
   complete store/ELF admission preflight remains required for integration.
   `generate_profile.py --sleep` can stage the same graph independently.

The frozen provisioning-template creator remains a legacy nine-provider tool;
it does not yet accept the sleep topology. The integrator owns the final full
BIN and its cohort identity. This branch does not replace the existing controls
image and does not merge or change performance branches.

## Verification

Host commands require `RISCRTE_RUNTIME_ROOT`, `RISCRTE_READER_ROOT` and (for the
clock) `RISCRTE_APPS_ROOT`:

- `bash minimal/test/run_power_test.sh`: 11 ownership/neutrality/refusal/retention
  scenarios, including failed release and destroy retry.
- `bash minimal/test/run_power_navigation_test.sh`: real power + navigation
  providers, exact GPIO split, short press, page key, and held wake suppression.
- `bash minimal/test/run_clock_sleep_test.sh`: 28 real clock/adapter/hook cases,
  both orientations, with/without Quick Actions: wake, native busy, native
  retained, held key, alarm timer, alarm refusal and missing grant. Explicit Home
  routing cannot turn a sleep press into a launch.
- Repeat these with `SANITIZE=1 SANITIZERS=undefined` for UBSan (passed here).
  Default `SANITIZE=1` selects ASan+UBSan. macOS26.5/Apple clang17 ASan stalled
  inside sanitizer dyld initialization before main on this host, including an
  escalated run; it is not reported as passed.
- `X4_GRAPH_ONLY=1 bash minimal/test/run_profile_test.sh`: both default and sleep
  graphs admit through real Runtime with zero preflight I/O. Overlapping GPIO3,
  missing power17 and wrong dependency binding fail before I/O. Without this
  option it also runs the existing panel/SD suites (GNU timeout needed for SD).
- `python3 minimal/test/sleep_profile_test.py`: exact unchanged bindings and
  Clock-only explicit grant policy; navigation alone adds no authority.
- `bash minimal/test/run_light_buttons_target_test.sh`: actual Xtensa8.4 links,
  normalized relative relocations, sole provider export, strcmp-only imports,
  and no PSRAM compare-and-set for frontlight/buttons/power/power-buttons.
- Runtime `test/run_light_sleep_test.sh`: actual CPU input/stale/foreign-token,
  active-wake/busy rejection, repeated wake and cleanup retention passed.

Sleep Clock Xtensa link and native ELF structural validation passed. Baseline
controls Clock ELF and new non-sleep Clock ELF were byte-identical with
`--navigation --alarm-client --quick-actions`:
`848cfb3385dc018c0b7b22fb783b82b71e15cd5aff20b807eb01e24a5c7874e9`.
The existing paper QuickActions suite passed its 62 normal cases; its sanitizer
phase additionally hits the existing macOS `-no-pie`/`-Werror` incompatibility.

The all-provider build on this macOS Xtensa8.4 toolchain hits a BFD assertion in
unchanged x4pro_panel. Changed provider ELFs and Clock compile separately. The
single X4 integrator must complete its full target/store/BIN validation using
its working build environment; no full BIN is claimed by this checkpoint.

## Integrated cloud checkpoint

The product integration combines this opt-in graph with Runtime 0.1.45 and
panel0.1.19 cooperative UC8279 transfer. GCC host builds required braces around
one test-only conditional that Apple Clang had accepted. Power, split navigation
and all28 Clock cases pass with ASan+UBSan (`detect_leaks=0`, because this
executor does not support LeakSanitizer under tracing). Both panel graphs and
three invalid ownership graphs pass the real Runtime zero-I/O preflight.
All11 provider packages, including both navigation selections, and sleep Clock
compile with the pinned Xtensa8.4 toolchain. The full packaged store admission
and final image custody are recorded with the candidate, not inferred here.

## Qualified light-only Settings profile

Product 0.1.6 builds Settings 1.3.4 without `--sleep-settings`. The generic
Light/Deep/Hybrid preference is not meaningful to this X4 hook and must remain
hidden until a provider consumes those modes. Crown manual light sleep, alarms,
Home and QuickActions are unchanged. The bundle validates the Settings ELF hash,
version, clean build receipt and absence of `PORTABLE_SLEEP_SETTINGS`, rejecting
the unsupported selector before store assembly. Other applications retain their
exact 0.1.5 artifacts and source receipts. The 0.1.5 BIN is preserved unchanged.
