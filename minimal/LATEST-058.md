# X4 0.1.58: latest qualified updates

This full-flash successor preserves the complete delivered 0.1.57 cohort. It selects fast panel 0.1.14 from public 879ebef75b47f723655253ec04428f7a48e487b2, GT911 0.1.10 from public 6590cbdcd7203ac1c2e58c84225e34d8df8602a3, and BLE Buttons 0.1.22 from Utilities e620160c48c11fb074378cd31a20ff8362a9659b. Native 0.2.2, all other 44 modules, boot graph, grants, settings defaults and all unmodified flash regions remain exact 0.1.57.

Run minimal/scripts/package_latest_058.py with the preserved source-bound assembly-spec.json, pinned compiler, mkspiffs and absent output directory. Every changed target is bound to source, SDK and qualification receipts. The assembler checks all 47 ELFs and full cohort admission in normal and sanitized modes, compacts only the three replacements, verifies deterministic store generation with two independent readbacks and checks paired-bank SHA/CRC.

Panel 0.1.14 requires no native, API or build-option change. Its fresh target matches the public green CI artifact. Focused panel 46 normal +46 sanitized, fallback69, SDK3 and Light-idle4+4 checks pass. An optional 8 ms cadence idle-wait fixture assertion fails identically with baseline 0.1.13; the 1 ms integration case passes. This inherited limitation is not counted as a pass. Physical display, touch, USB and Bluetooth behavior still requires user testing.

Incomplete Files sharing and Watch-only work are excluded. Full 16 MiB flashing at offset 0x0 overwrites internal settings, Bluetooth bonds and AppData. Back up those data first. Removable SD contents are not included.
