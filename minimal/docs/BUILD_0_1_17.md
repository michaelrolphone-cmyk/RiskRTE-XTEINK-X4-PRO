# X4 Minimal 0.1.17 development cohort

This cohort combines completed native app changes over the frozen 0.1.16 platform. It retains UC8279 fast provider 0.1.6, accepted pixel polarity, 2300 ms resident-image settling, 30-second awake maintenance, clean desk-clock waveform, both landscape directions and early battery-boot diagnostics. Battery-only/RST startup still needs device confirmation; a host build does not establish an electrical repair.

## Selected changes

- Explicit Runtime 0.1.63 `demand-retained` policy. Sparse boot activates only needed dependencies. Foreground promotion retains active providers and arms retention on later first use; it does not load unused providers. Used mappings survive app returns. Installation/update admission remains; no boot/launch checksum gate is added.
- Default-off BLE telemetry from the shared capability chain. Foreground BLE/IQ users and storage custody pause advertising. Timer-only sparse wakes do not activate telemetry.
- Reversible automatic Light sleep across the 18 selected apps. The foreground call stack and drafts survive a clean wake. Default idle timeout is 60 seconds; the persisted below-10% battery action selects 20 seconds, 15% brightness while preserving OFF, and disabled radio intent. The unused deep-after preference is not advertised as automatic Deep/Hybrid support. Explicit Home desk locking keeps its separate deep/sparse lifecycle.
- Actual active RF capture inhibits automatic idle. Clean wake restores saved background radio intent; it does not restart foreground capture.
- Points and Timecard use finger-tracked bounded list scrolling, momentum and completed-frame hit identity, alongside existing Settings, Files and Wi-Fi scrolling and Quick Controls motion. Remaining list conversions are separate work.
- Guarded native app acquisition, key-value access and cleanup. Uncertain ownership retains the invocation and suppresses later I/O, launch or unload.
- OTA Update and App Store have X4 product/cohort boundaries and native-time paper UI. Their release feed is unconfigured. Check reports unavailable configuration and performs no network transaction. Local paired update payloads are build artifacts, not published releases.

## Apps

Clock, Springboard, File Browser, Bluetooth Scanner, Points in Time, Settings, Calculator, Stopwatch, Countdown, Timecard, Battery, Alarms, Wi-Fi, Bluetooth Touchpad, Bluetooth Buttons, RF Spectrogram, OTA Update and App Store.

Serial Monitor's tested paper client is preserved separately. It is not bundled until a real minimal Runtime USB/UART transport and board power contract are implemented. No audio or LoRa hardware availability is invented. The next Contexts feature is not part of this cohort.

## Build and provenance

Use the exact source lock, native app receipts and manifest versions. Select `--sleep --desk-clock --sparse-clock --ble-telemetry --idle-policy --retain-promoted-providers --panel uc8279 --panel-driver uc8279-fast` when composing. Native compilation includes the platform's early startup source; the generic Runtime BIN is not interchangeable.

`stage_clock_admission.py` derives Clock's native receipt from its clean source, compiled SDK, helper and existing build evidence. The composer admits the complete app/provider graph and rejects a mixed SDK, helper, source, version, authority or partial profile. SDK hash verification is offline packaging work, not a device touch/boot/launch operation.

The full 16 MiB USB image is flashed at 0x0 and overwrites NVS/appdata. Preserve data before flashing. Software validation and exact image extraction are recorded with the candidate. Physical radio, battery boot, sleep current, wake and display qualification remain separate.
