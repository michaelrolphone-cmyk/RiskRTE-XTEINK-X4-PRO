# X4 0.1.57: Wi-Fi and completed UI decoupling

Full image: X4-0.1.57-Wifi-UI-full-0x0.bin
SHA256: 0e78e51b4d77e590f6f80035e54b5462e119f77115d55806c60d870d3783a9a7
Size: 16,777,216 bytes. Flash offset: 0x0.

**Full flashing overwrites internal settings, saved Wi-Fi profiles, Bluetooth bonds and AppData. Back up first. Removable SD contents are not included.**

This source package includes the complete recovered .55 source lineage, exact successor Git source bundles, build commands, immutable native composition source, target inputs and verification receipts. The post-recovery cache is used only for unchanged source-bound modules. All twenty portable applications select the qualified bounded raster snapshot renderer explicitly. GameBoy retains its independent presenter, and fast panel 0.1.13 remains byte-identical to .55.

The completed changes include asynchronous owner-safe Wi-Fi scan/join/cancel, shared masked password entry, multiple saved profiles, completed-token handling before an aged display deadline, safe service deferral, shared input/display decoupling and the connected-only USB polling improvement. Profile storage grows within actual storage capacity; the eight visible entries are a page, not a product ceiling. Saved credentials use the existing plaintext storage and Forget is logical deletion.

Native 0.2.2 preserves the full 64KiB IQ reservation. Its initial full X4 build caught a 1088-byte static-RAM collision. A single startup-owned internal allocation now holds the complete copied radio mailbox/result/stage buffers. Allocation failure leaves asynchronous radio unavailable without entering the vendor SDK. No capacity or diagnostic records were removed.

Verification: all 47 module ELFs, full production cohort admission normally and under ASan/UBSan, deterministic and independent SPIFFS readback, native startup/options/IQ proofs and paired-bank checks pass. The store has 97 files, with 46 changed and 51 preserved from .55. Native/provider tests pass 57 cases in each normal, ASan/UBSan, TSan and stage-off mode; actual app/provider/native seam passes 59 in each of the first three modes. Hardware has not been tested by this build process. Windows eject signaling and remaining minute-scale USB latency still require device evidence; the cadence change is not claimed to resolve them.

## Source pins

- X4 tooling/selection: public 8a3775bee2c262b3fb74543ce1cc061551cd74be, tree-equivalent to local assembly 28ada8dae97dcd2ba071ab43e2d92c8217e10f2c
- System: public 8a75862929e4e83c8f66b3d58cc1c36d44830c84, local bd98b2e14c8d4ac20e4075c03396b12d8480e8f4
- Utilities: public 35c2141c68668971dc524d98702109b00c740815, local 3cc0729f7a6f96460094f3918d6143943dcf2f6a
- Productivity: public 72f78ca8ea918c5c6d26acfa2e3935be4b2030dd, local 0dea3d6c96513559f8eed8634e19a2e6c1454633
- Wi-Fi provider: public a304597bf26491483bb0a567569f0ca0267cdf9c, local 7932bc5f452a8c958298cc380277357d5cc97f67
- Runtime: embedded local 0f17a435f99d02d60ca50df1d1a51fcef123db89 is fully preserved in its bundle. Public b25b1d467a557e8d693211cff2eff959eb9c79de has the identical complete tree 74a0284e1f72e0c568197ba0ed6b25be7ee3d58b. This later publication adds source custody; the delivered image retains its original embedded identity.

Local/public commit IDs differ because connector publication reconstructs commits; full source-tree equality was verified. Source-map.json records exact trees and standalone bundles. The immutable native platform is X4 6bb9eaed44971c6cd2748f09be028772ab7e0784; later app/helper metadata does not alter its compiled native source.

## Replay and rebuild

For an offline byte-for-byte packaging replay, run replay_assembly.py with an absent --work directory and the pinned GCC8.4.0 esp-2021r2-patch5 --compiler plus mkspiffs2.230.0 --mkspiffs. Python requires pyelftools; host cc/c++ and sanitizer runtimes must be available. Replay restores bundled Git sources, verifies every source/tool/input hash, repeats admission/readback and requires the delivered BIN hash. It deliberately uses the permitted source-bound target cache and never accesses a device.

For compilation, restore the source bundles to the paths recorded in recipe/assembly-spec.json or consistently rebase the recorded commands. recipe/qualify-raster-other-apps.sh builds Points, Timecard and Contexts. proof/renderer-target contains every explicit System/Utilities command. Source contains the text and Wi-Fi provider builders. proof/native-build records exact .2.2 preparation and build selection; use prepare_native_runtime.py with 17 requirement rows, 18 policy rows, app image cache, DIO boot, USB PHY, 512 retained-wake bytes, failure evidence and esp32s3-16mb-appdata-iq-stage. Set PLATFORMIO_SETTING_ENABLE_TELEMETRY=No. Ordinary official compiler/vendor SDK packages remain pinned build dependencies.

inherited-055 contains complete source, Git metadata, historical ABI headers and the full 47-component cold-build recipe for the unchanged lineage. Its archived notes describe that earlier recovery interval. It is retained for source completeness, not as a claim that .57 was another wholesale cold build.

Offline bundle replay passed and reproduced the delivered BIN byte-for-byte. If /tmp is a small tmpfs, set TMPDIR to ordinary disk for host admission intermediates. The initial replay ran out of tmpfs space only while writing the final comparison BIN; the shared-disk replay and all repeated checks completed.
