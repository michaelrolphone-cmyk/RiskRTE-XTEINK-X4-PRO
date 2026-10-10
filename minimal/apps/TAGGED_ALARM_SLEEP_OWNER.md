# Qualified alarm API2 sleep ownership

`ALARM_SERVICE_TAGGED_V2` selects the qualified Utilities tagged descriptor in
the X4 application-local sleep owner. The base pointer remains the existing
`alarm_service_v1 *` source signature, but the selected provider must negotiate
`alarm.service@2`. Light, foreground Deep and sparse timer Deep validate the
complete descriptor before dispatching alarm callbacks. API1, short/unknown
descriptors, inconsistent feature bits and missing required callbacks refuse.
Light also requires the advertised resume feature; Deep does not use it.

## Successful Light boundary

The owner passes the unchanged successful `prepare_sleep` ticket to the
validated `alarm_service_resume` helper exactly once, immediately after native
Light returns `RISC_LIGHT_SLEEP_OK`, before restoring brightness or releasing
its grant. No mutating service call intervenes. Native refusal and retained
native results do not consume the ticket. Deep entry, including simulated
terminal entry, never invokes the Light-resume callback.

A checked ordinary resume failure becomes the existing app foreground-failure
result (`-1`) after normal brightness/grant restoration. The owner never retries
the ticket. `ALARM_RETAINED` or output-custody uncertainty immediately maps to
app retained result `-2`: no later alarm/provider call, brightness restoration,
retained clear or grant release. Deep preparation also recognizes retention
from prepare/step results and copied status error, preserving its existing
staged record and peripheral custody.

All behavior is conditional on the new flag. No product manifest, pin, source
lock, provider implementation or BIN is changed here. In particular, the
publisher must select API2 in the final application requirements and provide
the qualified headers. The flag-off System API1 header lineage contains legacy
helpers that differ from Utilities API1; it must not be replaced merely to
compile an unselected profile.

## Qualified inputs

- X4 integration baseline: `a5150a8042b7dce2468906ecc7ef7fbf5b302451`
- System: `81f884b8a053cf917054fb1433c7850714cd0c48`
- Utilities: `637e13b0bce62ad49b756bec2468a6271d163fc7`
- Runtime: `30dcec5ce6ce33223f2b203a2399283e1f758567` (0.1.52)
- Canonical typed-power Reader SDK: `aac8c06d3221139084acd0cfc64f7b0ba194a97a`

`run_tagged_alarm_sleep_owner_test.sh` stages Utilities alarm headers alongside
canonical Runtime/Reader headers. It checks the exact clean qualified source
revisions. Set `RISCRTE_APPS_ROOT`, `RISCRTE_RUNTIME_ROOT`,
`RISCRTE_UTILITIES_ROOT`, and `RISCRTE_DESK_SDK_ROOT`; use `SANITIZE=1` for
ASan/UBSan (`ASAN_OPTIONS=detect_leaks=0`).

The host suite links the actual Utilities native-UTC visual Points alarm
provider with the actual X4 client. Its dependency storage/time and X4 hardware,
Runtime and shared Clock rendering are fixtures. The alarm state machine,
prepare/resume ticket custody and reconciliation are production code. Copied
status retention and malformed descriptors are explicit fault injections.

88 scenarios × 2 brightness profiles pass normally and under ASan/UBSan,
covering malformed descriptors, actual due/near/future alarms, successful and
repeated Light boundaries, stale tickets, RTC/backward errors, retained native
and provider results, descriptor revalidation after wake, ordinary refusal,
terminal Deep, and failed restoration/release. The existing 28 Light cases,
166 × 2 desk lifecycle cases, 45 × 2 real shared-app compositions and 110 × 2
sparse client cases also pass normally and under ASan/UBSan.

`test_tagged_alarm_sleep_target.py` composes the actual System Clock, adapter
and local X4 client for Light/Desk/Sparse × Quick off/on. Host GCC14 and pinned
Xtensa ESP32-S3 GCC8.4 produce byte-identical flag-off preprocessed source and
objects against the X4 baseline. All six complete flag-off application ELFs
also match exactly. All six API2 application ELFs and the qualified native-UTC
visual alarm provider ELF pass the real ELF structural validator; application
imports/exports remain within the existing native allowlist. Separate staged
include roots preserve the explicit header lineage boundary.

Evidence: `test/desk_clock/tagged_alarm_sleep_owner_validation.json`.
Target outputs are development-only under `build/tagged-alarm-sleep`.
These checks do not execute target instructions or qualify physical Light/Deep
sleep, RTC accuracy, rails or native hardware cleanup. Full product publication,
installation and hardware validation remain separate integration gates.
