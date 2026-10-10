# X4 0.1.52 Quick Controls test increment

Build from immutable 0.1.51 with `minimal/scripts/package_quick_controls_052.py`.
Exact source/target pins are in `minimal/apps/quick-controls-052.json`. The new
product descends from the qualified provider source, not a stale display tree.

The only changed packages are Home 0.3.25, frontlight 0.1.6 and fast display
0.1.10. Home adds the matching USB Transfer grid tile and the optional generic
cool-to-warm slider. It also fences explicit clean-refresh refusal before any
further diagnostic calls. The single resident host remains the controls owner.
No new grants, providers or app routes are added; boot.json is unchanged.

Fast display 0.1.10 is based on the exact shipped 0.1.9 source and independently
reproduced deployed ELF. The earlier stale 0.1.8 candidate is not selected. The
source reversal and baseline/candidate behavior comparisons are explicit input
proofs. Neutral tone retains legacy output; the ratio is dimensionless, not a
calibrated Kelvin or constant-luminance control. Physical warmth/brightness,
electrical limits and flicker have not been measured here.

Native Runtime 0.1.100, SDMMC, shared Points/BLE keyboard, all 20 other apps and
all 24 other providers are byte-identical to .51. The full image changes only
Home and two provider ELF/manifest pairs, cohort identity and paired store hash.
Bootloader, partitions, NVS, AppData, native firmware and inactive bank remain
byte-identical. There are still 21 apps, 26 providers, 47 ELFs and 97 files.

Validation includes exact native ELF/BIN export agreement, strict production
admission for every ELF, normal/sanitized whole-graph admission, production loader
host-hook tests, source/SDK pins, semantic compaction, two SPIFFS readbacks,
deterministic generation and firmware/store paired SHA/CRC. Independent review
is required before delivery. Source tests and host rasters are not hardware
qualification. No hardware access, flashing or source publication is performed.

Inherited limits remain: no physical keyboard input provider, unmigrated editors,
Windows MSC mounting not established as fixed, and no physical USB/SDMMC/sleep,
touch/display or live memory-headroom qualification. The serial-restoration
native candidate is preserved unchanged.

Warning: full 16 MiB image at 0x0 overwrites internal settings, Bluetooth bonds
and AppData. Back up first. Removable SD contents are not included.
