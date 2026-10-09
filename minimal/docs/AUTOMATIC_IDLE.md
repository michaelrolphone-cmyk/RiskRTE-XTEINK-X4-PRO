# Explicit ordinary-app Light policy helper

`minimal/apps/portable_idle_sleep.c` is an optional app link input. It changes no
provider/Runtime ABI, raw GPIO ownership, brownout setting or hardware claim.
The existing manual `portable_sleep.c`, Home Deep lock and sparse timer path
remain separate.

The caller holds the foreground stack and private grants, stops telemetry and
consumers, closes raw-touch subscriptions, resets navigation and submits no
pending display frame. Healthy continuous SDR/audio capture and active Wi-Fi
or update work inhibit automatic entry. The helper checks and borrows these
exact existing typed providers: x4.power17, display3, raw touch4, volume9,
Wi-Fi15, Bluetooth16. It receives the borrowed tagged alarm.service@2 table.
The caller supplies shared Settings KV1 and the confirmed brightness accessor.

The helper shuts down radios, prepares a fresh alarm ticket, blanks the
backlight, prepares touch/panel, prepares and commits reversible SD sleep,
charges all preparation time to the alarm deadline, and invokes only Light.
After return it restores SD/panel/touch before consuming the identical alarm
resume ticket, then restores brightness and releases its own grants. It never
stages a retained record or invokes Deep. Return0 is a checked clean refusal;
return1 is a checked wake; return-1 is a foreground/alarm failure after restored
custody; return-2 forbids any further normal cleanup or provider work.

The shared adapter restores confirmed saved radio intent only after successful
typed restoration. Background telemetry may resume under its existing saved
policy. Foreground SDR/audio capture does not restart automatically.

`app_grants(..., idle_policy=True)` is a bounded composition opt-in for a known
selected app on the explicit sleep graph. It requires the complete typed
capability set and maps physical IDs exactly; it gives ordinary apps no
retained-wake or raw-platform grants. Existing calls remain Clock-only.
This helper commit does not enable a distributable product or change0.1.16.
Central composition must record the helper hash and rebuild the entire cohort
under its separately allocated product version.

Verification tools:

- `run_idle_alarm_sleep_test.py --system ... --runtime ... --utilities ...
  --candidate <Clock build>`: actual production tagged alarm service and helper,
  54 normal and54 ASan/UBSan cases.
- `run_idle_panel_policy_test.py --candidate <Clock build> --provider-sdk ...`:
  actual fast panel0.1.6 and helper; 2300ms settling, 30000ms maintenance,
  maintenance/frame overlap, typed drain/refusal/rollback and no prepared polling.
- `run_idle_grant_runtime_test.py --runtime ...`: actual Runtime/Graph with host
  provider bodies and the X4 device/dependency map. Exact acquisitions succeed;
  wrong IDs, duplicate and missing grants fail before activation.
- `python -m unittest discover -s minimal/test -p '*grants_test.py'`: bounded
  opt-in, ordinary-app authority, existing grants and namespace regressions.

Host and target evidence do not qualify physical sleep, battery, wake accuracy,
power consumption or cold boot. No image was flashed or published.
