# X4 platform migration

## Source custody

Imported from `michaelrolphone-cmyk/T5S3-Reader` commit `34d8e694d89a1e72d8854403d8592c289fae3ddc`, tree `e7dc2a45c3553f0102a7c2e2c2c9fe5bc51b00a2`. `migration-source.json` records original blob hashes and file modes for each imported file. Files have not been rewritten to disguise package version changes. Imported driver source and manifests are byte-identical; therefore their stable IDs, numeric versions and ABIs are preserved.

The source branch and PR #350 are not modified by this extraction. The pinned source remains the shared runtime dependency. This establishes X4 maintenance and build ownership here without destructively removing sources required by the paused runtime work.

## Shared boundaries

The following stay upstream even where names contain X4:

- `build_x4pro_drivers.py`: also builds T5 SD/frontlight and PCA/TPS providers.
- `stage_x4pro_packages.py`: its generic stage function is used by T5.
- `build_x4_module_store.py`: shared SD deployment builder.
- `install_x4_provider_toolchain.sh`: shared pinned compiler installer.
- `Drivers/x4pro_i2c/os_cpu_v1.h`: used by T5 SD.
- `Drivers/x4pro_board/x4pro_mmio.h`: generic ESP32-S3 GPIO helpers used by T5 frontlight, alongside historical X4 helpers.
- `platform_clock_v1`, `storage_fatfs`, shared RTC helpers, SDK, loader, generic runtime and mixed-platform integration tests.

Device files keep original relative paths in the composed tree to preserve shared includes. `platformio.x4.ini` owns only the X4 environment; the locked runtime owns base toolchain, SDK and ESP32-S3 board definitions. Root `.github` workflows from the runtime are not copied into the build tree.

## Provisioning and provenance

The profile includes all eight X4 providers plus platform clock. Battery and RTC remain explicit boot selections. Staging uses the original ordinary package metadata/ZIP validator, requires source manifests to match built manifests, verifies ELF length and SHA-256, and records both source repositories with staged output.

The historical hardware manifest and deployment scripts assume a single repository HEAD. Do not use their `source_sha` alone as proof of this composed build. Use `build-origin.json` alongside artifact digests; migration CI preserves both platform and runtime identity.

## Hardware limits

Host and target builds cannot establish battery/RTC telemetry on hardware. Existing reported telemetry issues remain unqualified by this source migration. Historical device-controller scripts and hardware logs remain upstream and are not republished here. This task does not reconfigure a device scheduler or operate hardware. `X4BootScreen` is historical direct-panel diagnostics and is not activated by the migration.

Historical board documentation remains at the pinned upstream source because it mixes earlier assumptions with machine-specific test records. Current driver/board source and focused tests govern the imported implementation.
