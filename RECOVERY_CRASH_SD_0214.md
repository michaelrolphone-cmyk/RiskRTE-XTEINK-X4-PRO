# SD 0.2.14 recovery working copy

Fresh qualification pending. Do not deploy this reconstruction based on old test results.

Recovered from the exact saved X4 .53 SD 0.2.13 files and the available task work history after the executor reset. This branch contains actual production-source changes, not only a patch/archive. The unavailable former local commit was c13a93b5561c06efa7e08722d21b3b90e2c2241d. Its Git identity and old target qualification are not claimed for this new commit.

The following reconstructed production files match the SHA256 recorded before reset:

- minimal/drivers/x4pro_sd/driver.c: 57b50acf86d413929fb4b53edaa970d366f907495ca83b1d707870142c33673d
- minimal/drivers/x4pro_sd/BootLog.h: be49f4c78f5c6054f79cabe82c951b95d984b28e8740e61f71896541c93fc6a0
- minimal/interfaces/RiscStorageVolumeStateV1.h: 1f2f7dbfe9ec7ac27a57149ac720cf7f3a4801d6d34b5532f77c77db252b87a1

The append-only tagged state observer reports clean local readiness, clean unavailable media, or retained custody. It reads fixed RAM latches/slots only, without locks, callbacks, filesystem scans, logging or hardware calls. Existing volume, power, sleep, export and preparation prefix behavior is preserved. Provider authority is unchanged.

The existing SD .53 regression fixture is included. Recovery of the extra state tests, rebuilding and fresh normal/sanitized qualification will follow. Native-first product deployment, hardware testing and power-cut qualification remain separate. No PR, merge, release or device action is implied by this working-copy checkpoint.
