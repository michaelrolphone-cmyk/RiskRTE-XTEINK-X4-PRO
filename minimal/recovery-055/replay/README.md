# X4 0.1.55 clean source build

The frozen executable plan builds all 47 modules, native firmware, bootloader, partitions and empty AppData from recovered or reconstructed source. The selected compile source is X4 f4ef23bc; fd43f2c8 adds only a JSON-copy timestamp guard correction and legacy SD test opt-in. No earlier product ELF, BIN or object is an input.

Use the companion complete source archive and source-map.json to restore the exact checkout/snapshot paths. Git source identities are pinned in executable-plan.json; obtain their published commits with ordinary read-only Git fetch when reconstructing repository metadata. Preserve pinned historical ABI objects: Runtime 30dcec5ce6ce33223f2b203a2399283e1f758567, Utilities637e13b0bce62ad49b756bec2468a6271d163fc7 and e80172353fcb9e6519ca9d9b2a4d43c69c1bc91d. These supply header source, never executable artifacts.

The source archive carries the complete alarm and Contexts wrapper layouts with their pure header/source dependencies. Copies here preserve the executable wrapper code and immutable upstream file maps. GameBoy, Points/Timecard and shared-text builders are included in their pinned published source trees.

Run build_clean_recovery_055.py with the frozen plan and a new absent output directory, then assemble_clean_recovery_055.py with its completed ledger and another absent output. Set PLATFORMIO_SETTING_ENABLE_TELEMETRY=No. Set NATIVE_DRIVER_CC and NATIVE_APP_CC to the official pinned GCC8.4.0 compiler, and PLATFORMIO_CORE_DIR to the restored official package cache. The explicit PyPI esptool4.11.0 configuration is preserved in source inputs.

The recorded build had a source-JSON timestamp false positive and one missing historical SDK Git object. Its two one-run continuation scripts retained all same-run artifacts, checked hashes and source state, and never imported a prior product output. The ledger preserves failed attempts and reconciliation. These continuation scripts are audit records, not a requirement for a new build using the corrected runner.

Full0x0 flashing overwrites internal settings, bonds and AppData. Back up first. No device or hardware test was performed.
