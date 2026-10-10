# X4 Minimal BLE telemetry source integration

This branch supplies separately pinned source and artifact qualification for the
parent's final cohort integration. It does not replace the active 0.1.16 build,
bump the product version, publish blocked System ancestry, or operate hardware.
It includes the accepted Home-lock sleep hook (original b78800a) so its Clock
qualification uses the same code as the selected controls cohort.

`minimal/apps/telemetry-sources.json` binds System, Utilities and unchanged shared
Drivers. `build_telemetry_providers.py` builds three isolated ordinary ABI2 ELFs:

- `telemetry-battery` 0.1.0, `battery-telem/manifest.json`, requires board.battery@1
  and provides sensor.telemetry@1. The sole selected board battery is X4 instance7.
- `ble-telemetry` 0.1.0, `ble-telem/manifest.json`, requires bluetooth.hci@1,
  platform.clock@1 and sensor.telemetry@1; provides bluetooth.telemetry@1.
- `telemetry-broadcast` 0.1.0, `broadcast/manifest.json`, requires platform.clock@1
  and bluetooth.telemetry@1; provides telemetry.broadcast@1. No bound KV grant.

Add all three boot driver rows with no hardware instance or private key bindings.
Keep `provider_activation=demand`; foreground Clock promotion pins the normal
boot graph, while retained timer-only Clock acquires none of these providers.
The existing ten X4 plus six common providers becomes nineteen, below Runtime's
24-provider limit. No temperature or PMU field is introduced.

Every rebuilt native app adds telemetry.broadcast@1, grant instance0, and uses its
existing shared namespace1 for `ble_broadcast` and `quick_radio`. Compile
PORTABLE_BLE_BROADCAST + PORTABLE_BLE_BROADCAST_DEFAULT_OFF. Scanner, HID and IQ
apps also compile PORTABLE_BLE_FOREGROUND. Explicit Bluetooth off/airplane and
missing radio preference prevent advertising, even with saved broadcast intent.
Clock has15 declarations and16 policy grants because KV maps to both1 and5;
transient service grants do not persist through deep sleep. The observed actual
controller peaks are12 foreground/deep and6 timer grants. Real Runtime testing
fills16 live grants and rejects17.

`telemetry_cohort.py` supplies exact selected versions, manifest/receipt/provider
validation and a pure `extend_boot` composition. Feed it rebuilt manifests and
the prior cohort's grant rows; it adds the one telemetry grant per app and the
three provider rows, rejecting missing apps, extra/duplicate authority, wrong
versions, missing namespace1 or capacity overflow. `install_providers` copies
only source/digest-checked local outputs into an empty staging location. The
parent must update its native cohort version/source locks and rebuild remaining
app receipts before final bundling; the old bundle validator intentionally does
not silently accept this new cohort.

Selected versions are Clock0.3.11, Springboard1.7.7, Settings1.3.15, Files1.5.10,
Wi-Fi1.1.12, Battery1.1.7, Calculator/Stopwatch0.1.14, Countdown0.1.13,
Timecard0.2.4, Points0.6.4, Scanner0.2.9, HID0.1.9, Waterfall0.2.5, Alarms0.2.9.
The parent must preserve accepted0.1.16 product0cd4053 and later Settings70c2282, Wi-Fi9dc231a and File Browser
4da504e (including volume9 and scrolling1.5.9) changes while integrating the common adapter hooks. Timecard
must adopt PortableBroadcastAppData around stat/read/replace, as in accepted
Watch source; RF temporal data already does. This branch does not replace those
separately owned final application sources.

Validation artifacts are local under build/telemetry-runtime, build/telemetry-
providers, the System build/telemetry-clock and build/telemetry-clock-tests, and
Utilities build/native-broadcast and dist/native-ble-broadcast. Clock220 cases,
Battery22 cases and the actual Runtime8 modes pass normal+ASan/UBSan. Runtime
loads the production three-provider chain with simulated HCI/gauge, verifies the
exact BTHome bytes for73%,3970mV,charging, cross-app provider persistence,
Bluetooth-off/airplane behavior, real live-slot bounds, silent timer startup and
retained cleanup failure. Client/AppData/service and shared provider fixtures
also pass. Clock, seven utilities and all three providers pass Xtensa target
imports/exports and structural loader validation. Battery flag-off Watch/raw
paper artifacts remain byte-identical.

No full final cohort BIN is produced here. The ongoing automatic-idle work must
also reconcile the accepted Watch PORTABLE_RADIO_CONTINUOUS_CAPTURE guard before
enabling sleep; selected X4 manual-only builds are unaffected. Physical BLE
reception, radio coexistence, sleep power and actual battery readings remain
unqualified. No RF transmission, publication, flashing or device access occurred.
