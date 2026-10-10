# X4 0.1.59 panel-only rollback

Requested comparison build over exact delivered 0.1.58. Restore the exact delivered 0.1.57 fast panel 0.1.13 ELF and manifest. Preserve all46 other module ELFs/manifests, including GT9110.1.10 and Buttons0.1.22, the native0.2.2 firmware, boot graph, grants and all unrelated flash regions. Only panel ELF/manifest and product/cohort identity change.

The original panel target comes from the post-recovery clean build, with its full source-backed producer ledger verified. After compaction, the assembler requires byte equality with the actual delivered0.1.57 panel. All47 strict ELFs, normal/sanitized cohort admission, deterministic dual readback and bank SHA/CRC are checked again. This isolates the user's touch-latency comparison without attributing the symptom to a driver before evidence.

Full16MiB image at0x0 overwrites internal settings, Wi-Fi profiles, Bluetooth bonds and AppData; back up first. SD contents are excluded. No hardware testing is claimed.
