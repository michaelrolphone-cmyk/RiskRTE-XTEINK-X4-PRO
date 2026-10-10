# X4 0.1.58 source and proof delta

This package extends the complete 0.1.57 source archive already delivered. Keep both packages. It includes complete standalone Git bundles for the new X4 composition, selected external panel, selected GT911 and BLE Buttons source, plus every new target input, SDK/source hash, qualification receipt and replay script. Unchanged Runtime, System, native source and all other 0.1.57 source remain in the baseline archive; no source recovery is omitted.

Required baseline: X4-0.1.57-Wifi-UI-source-and-proof.tar.gz, SHA256 7d906c98560b5825ea46a4932a43866312c5976b8b495a99f3f56bd88b8c5619. Its twelve split parts reassemble to that archive. Extract it to obtain x4-wifi-ui-delivery-057.

Selected successors: fast panel 0.1.14, GT911 0.1.10 and BLE Buttons 0.1.22. All 44 other modules and Runtime 0.2.2 remain byte-identical to delivered 0.1.57. The full 47-module graph, grants and features remain intact. Unfinished Files sharing and Watch-only changes are not selected.

Output: X4-0.1.58-Panel-0.1.14-Latest-full-0x0.bin
SHA256: 11395fdcbe81c4f5b97d1e82c17ffbee2d314475804432a639362ffa22be506e
Size: 16,777,216 bytes; flash offset 0x0.
Full flashing overwrites internal settings, saved Wi-Fi networks, Bluetooth bonds and AppData. Back up first. Removable SD contents are not included.

Run replay_assembly.py using Python with pyelftools, --baseline-source /path/to/x4-wifi-ui-delivery-057, an absent --work directory, --compiler pointing to official Xtensa ESP32-S3 GCC8.4.0 esp-2021r2-patch5, and --mkspiffs pointing to version2.230.0. The script restores exact Git commits, verifies all source/input/tool hashes, repeats all47 strict ELF checks and normal/sanitized cohort admission, independently reads back the deterministic store and requires the delivered image hash. It uses the permitted post-recovery source-bound cache. No network or device access is needed.

For target recompilation, panel and Buttons receipts contain exact compiler commands and source dependencies. GT911's rebuild-target.sh is in proof/gt911. Restore source bundles and rebase the recorded paths consistently; the baseline package supplies canonical native sources and full inherited cold-build recipes. Set PLATFORMIO_SETTING_ENABLE_TELEMETRY=No before PlatformIO use. Public source mappings are recorded in delivery.json. Local build commit identity is preserved; tree-equivalent GitHub publication does not change the delivered image.

Checks passed:47 strict ELFs, cohort admission normal and ASan/UBSan, two independent SPIFFS readbacks, deterministic image generation, paired-bank CRC/SHA and frozen-region comparison. Only seven store files change;90 remain exact. Panel .14 needs no native/API/build-option change and its fresh target matches public green CI. Optional 8ms cadence idle-wait fixture fails identically on .13 and .14; it is explicitly retained as inherited evidence, not counted as passing. Physical hardware testing is pending.

Offline replay passed and reproduced the image byte-for-byte. The first replay attempt lacked the explicit objdump environment binding; replay_assembly.py now derives it from the pinned compiler. No product source or delivered image changed.

X4 source checkpoint: https://github.com/michaelrolphone-cmyk/RiskRTE-XTEINK-X4-PRO/commit/87d6c00d5225592bac5b27064d99a229a3646622 (tree-equivalent to embedded local4290d89071b2b61064efe7d542e18791ae00ba0c).
