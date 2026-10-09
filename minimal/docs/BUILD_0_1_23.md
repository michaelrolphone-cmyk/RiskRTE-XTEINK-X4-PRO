# X4 Minimal 0.1.23 RF-only Contexts integration

This cohort adds the RF-only Contexts editor, model service and Clock/Waterfall rendezvous over the frozen 0.1.20 input/display baseline. It retains UC8279 fast provider 0.1.6, accepted pixel polarity, 2300 ms resident-image settling, 30-second awake maintenance, clean desk-clock waveform, both landscape directions and early battery-boot diagnostics. Battery-only/RST startup still needs device confirmation; a host build does not establish an electrical repair.

## Input repairs

- GT911 0.1.8 supplies independent bounded queues to the shared UI adapter and HID applications. The old single-subscriber provider rejected the HID app's second subscription with `Raw touch start failed`. Actual HID controllers linked to the production GT911 provider reproduce the old failure and pass pairing, reports and cleanup with the repair. Hardware pairing still needs confirmation.
- Home highlights only a completed valid tap. Upward and horizontal app-opening swipes preserve the unpressed Clock/Points image and cannot activate the underlying block.
- Springboard 1.7.13 accepts deliberate left/right swipes to advance/return one viewport, clamped at the grid ends. Vertical finger scrolling remains. Navigation commits on release after a 44-pixel, direction-locked gesture; it cannot launch an icon as a tap.
- Clock startup logs now name absolute `uptime_ms` and cumulative `since_clock_start_ms`. The cumulative value is not a duration for the named stage.

## Battery-startup evidence

The new optional native observer copies the last named boot/provider/app milestone and the first completed display frame into the existing X4 RTC breadcrumb before USB availability can discard a line. The next boot automatically prints `X4_BOOT previous-stage` and `X4_BOOT previous-line`. No command is needed. The first completed frame can be the boot logo; it does not assert full application startup. The previous record is checksum-validated evidence only: RTC memory may not survive battery removal, brownout or external reset. No retained value controls rails, startup, sleep or authorization.

This is a diagnostic increment, not a confirmed battery-only/RST boot repair. The device is known to remain running after USB-assisted boot and unplugging USB. Touch/draw chatter is excluded from retained recording; normal Runtime/Watch builds leave the observer disabled.

## Selected changes

- Runtime retains immutable installed ELF input bytes for up to four images/1 MiB in PSRAM within the prepared store session. A repeated launch avoids its full-file read and whole-image structural parse; every invocation still receives a fresh relocated mapping, globals, init and fini. A new session/update restart discards the cache. The X4 build explicitly enables the cache; shared Runtime defaults it off. Covered app/provider allocation failures disable and free it before one retry. Native SDK allocations outside those wrappers are not covered; non-PSRAM builds remain uncached. No file-change/hash probe is added.
- Cold ELF reads still use at most 4 KiB per storage call and check deadlines per call. They yield after 32 KiB or 2 ms of work instead of forcing one tick after every chunk. A five-launch host sequence using earlier X4 default/Springboard bytes reduces 314 reads to 120, 1,276,812 bytes to 487,520 and instantaneous-read delay requests from 314 to 14. All five relocations remain. These are deterministic operation counts, not device timing.

- Explicit Runtime 0.1.73 `demand-retained` policy. Sparse boot activates only needed dependencies. Foreground promotion retains active providers and arms retention on later first use; it does not load unused providers. Used mappings survive app returns. Installation/update admission remains; no boot/launch checksum gate is added.
- Default-off BLE telemetry from the shared capability chain. Foreground BLE/IQ users and storage custody pause advertising. Timer-only sparse wakes do not activate telemetry.
- Reversible automatic Light sleep across the 19 selected apps. The foreground call stack and drafts survive a clean wake. Default idle timeout is 60 seconds; the persisted below-10% battery action selects 20 seconds, 15% brightness while preserving OFF, and disabled radio intent. The unused deep-after preference is not advertised as automatic Deep/Hybrid support. Explicit Home desk locking keeps its separate deep/sparse lifecycle.
- Actual active RF capture inhibits automatic idle. Clean wake restores saved background radio intent; it does not restart foreground capture.
- Points and Timecard use finger-tracked bounded list scrolling, momentum and completed-frame hit identity, alongside existing Settings, Files and Wi-Fi scrolling and Quick Controls motion. Settings root/time fields, the continuous Springboard grid and the seven overflow RF views now use the same bounded motion and completed-frame selection. Springboard reaches the final App Store/Contexts row and shows complete Touchpad, BT Buttons and RF Spectrum captions.
- Bluetooth Scanner results and sensor details, plus OTA/App Store catalogs, now use continuous paper list scrolling with momentum and completed-frame identities. Live row replacement/removal cannot silently select or install a different item.
- Guarded native app acquisition, key-value access and cleanup. Uncertain ownership retains the invocation and suppresses later I/O, launch or unload.
- OTA Update and App Store have X4 product/cohort boundaries and native-time paper UI. Their release feed is unconfigured. Check reports unavailable configuration and performs no network transaction. Local paired update payloads are build artifacts, not published releases.

## Apps

Clock, Springboard, File Browser, Bluetooth Scanner, Points in Time, Settings, Calculator, Stopwatch, Countdown, Timecard, Battery, Alarms, Wi-Fi, Bluetooth Touchpad, Bluetooth Buttons, RF Spectrogram, OTA Update, App Store and Contexts.

Serial Monitor's tested paper client is preserved separately. It is not bundled until a real minimal Runtime USB/UART transport and board power contract are implemented. No audio or LoRa hardware availability is invented.

## Build and provenance

Use the exact source lock, native app receipts and manifest versions. Select `--sleep --desk-clock --sparse-clock --ble-telemetry --idle-policy --retain-promoted-providers --contexts-rf-only --static-spiffs --panel uc8279 --panel-driver uc8279-fast` when composing. Native compilation includes the platform's early startup source; the generic Runtime BIN is not interchangeable.

`stage_clock_admission.py` derives Clock's native receipt from its clean source, compiled SDK, helper and existing build evidence. The composer admits the complete app/provider graph and rejects a mixed SDK, helper, source, version, authority or partial profile. SDK hash verification is offline packaging work, not a device touch/boot/launch operation. The shared HID provider retains its source/version and loaded semantics while non-loaded debug/local-symbol metadata moves to a sidecar, recovering 107,412 bytes of store space. Allocated segments, required symbols and relocations are verified unchanged before full Runtime store admission.

The full 16 MiB USB image is flashed at 0x0 and overwrites NVS/appdata. Preserve data before flashing. Software validation and exact image extraction are recorded with the candidate. Physical radio, battery boot, sleep current, wake and display qualification remain separate.

## Contexts

The paper Contexts editor uses genuine FontAwesome icons, reader orientation, shared touch scrolling, Quick Controls and draft-safe Light resume. It displays RF model readiness and monitoring status, saved presets and explicit reload. Audio is unavailable on this hardware profile. Monitoring defaults OFF when no preference is saved. Enabling or reloading requests the existing RF owner to export its private canonical models and return to the same default Clock. Sparse minute wake never starts Contexts.

Only the RF-only service profile is installed. Default Clock and Waterfall declare at most 16 distinct requirements and 17 policy rows; Runtime still permits only16 simultaneously live app grants. Actual Clock/controller tests measured8 foreground,12 deep preparation and6 timer. Those are software measurements; physical RF inference and sleep timing remain unqualified.

This native build uses the source-bound Runtime 0.1.73 stream/cache/policy union. The generic stream bridge is present but no Serial transport or Serial application is installed. The existing automatic timestamped stage logging remains enabled; no perf command is required.

The native source is the canonical public Runtime commit `b587df55298e0bb8e676b3d59ca13679c0267bf7` (PR49). Its compact provisioning and retained-owner scheduler fixes preserve the existing app authority limits. The explicit X4 seed-extension callback recomputes startup, native options and stage-log proofs while the shared composer retains rollback, TLS, IQ and policy checks. All three original composition JSON receipts survive seed and private composition.

The static SPIFFS producer emits the unchanged 85-file store directly in sorted ASCII order, avoiding offline deleted-page churn. Independent decoding confirms every file; production SPIFFS mounting and streamed schema2 installation are separately tested. This changes packaging only, with no extra device boot/launch verification.

## Bluetooth and battery comparison

Runtime 0.1.73 replaces four fixed HCI receive slots with a bounded byte queue inside the same memory budget. The actual selected BLE/scanner providers preserve 640 reports over 20 startup/close cycles. Maximum ACL packets, real overflow, malformed input and failed-close retention remain covered. This is a software-qualified Bluetooth repair; the separate Wi-Fi investigation remains open.

This ordinary QIO/80 MHz cohort is the same-Runtime control and provisioning source for the separate 0.1.22 DIO battery-boot experiment. It preserves the qualified QIO bootloader contract. It does not claim to repair battery-only startup. The DIO diagnostic has a distinct full-image recipe and is not an ordinary OTA/provisioning payload.
