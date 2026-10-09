# X4 update applications and local OTA artifacts

The native-time product now selects OTA Update 1.2.0 and App Store 1.2.0,
plus software-update-firmware/apps 0.1.4. The System source is pinned in
`minimal/apps/sources.json`. Add `update_firmware` and `update_apps` input
folders to the bundle inputs, pointing to the two corresponding System build
outputs. Add both application folders under `apps`. Rebuild Springboard against
`minimal/apps/catalog.json` so both installed applications have launch entries.

The complete build requires `--sparse-clock --desk-clock --sleep`, matching the
existing native UTC/API2 cohort. The app grants bind saved Wi-Fi to namespace 6,
preferences to 1, realtime/alarm/update service to 0, display to 3, touch to 4,
navigation to 6, battery to 7, Wi-Fi to 15 and optional Quick Bluetooth to 16.
Only the selected update provider receives native HTTP/bank-store/clock tables.
App grants cannot select the other update action or raw native update tables.

Build the shared updater with `--product x4 --time-profile x4-native-time
--native-time-runtime-repo RUNTIME --tagged-alarm-utilities UTILITIES
--alarm-client --quick-actions --quick-radios --navigation
--display-rotation 90 --wifi-instance 15 --home-app default.elf`.
The build records must be clean and exactly match the pinned source, ELF,
manifest, native SDK and disabled X4 feed policy. Production store admission
still checks all selected ELFs, graph bindings and app grants.

After successful full admission, the bundle composer also writes `updates/`:

- `xteink-x4-pro-cohort-VERSION.bin`: native firmware immediately followed by
  the complete immutable 0x510000-byte store. No partition table, NVS, app-data,
  bootloader, otadata or bank journal is included.
- `xteink-x4-pro-launcher-VERSION.bin`: the separate 16 MiB USB first-install
  image, explicitly represented as USB information rather than OTA payload.
- Per-app ELFs and `release-index.local.json`, with exact manifests and digests.
- `update-artifacts.json`, binding all files to the admitted cohort and source.

`--skip-extended-checks` creates no update artifact directory. A full USB image,
wrong product/layout, changed firmware, mismatched store cohort or mismatched
USB backing bytes fails the packager. Native activation independently verifies
incoming firmware/store digests and full admission; boot consumes the committed
bank record without introducing image rehashes.

The catalog's GitHub URLs describe a potential future release naming contract.
No such release is claimed or created. No X4 feed is configured: both apps
truthfully report no available update and Check does not open the network.
App Store updates existing admitted apps; adding apps/providers/authority requires
a complete product cohort. A future feed and hardware OTA qualification remain
separate work. This isolated development fixture does not reserve a new product
version and must not be published as a replacement 0.1.16 release.

Focused tests: `update_artifacts_test.py`, `native_time_cohort_test.py`, and
`app_bundle_grants_test.py`. Shared System X4 tests cover product/catalog mutation,
UTC and tagged alarms, cancellation, Home, and retained cleanup. Generic Runtime
ABI2 suites cover wrong-cohort rejection, interrupted staging, rollback, selection
uncertainty and committed-boot operation counts. Host tests do not qualify
physical network, radio, display or flash/power-loss behavior.
