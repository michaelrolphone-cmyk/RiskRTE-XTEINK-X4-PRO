# X4 0.1.72: original panel 0.1.12 and Reader boot animation

The user explicitly rejected the 0.1.18/0.1.19 display branch. This image
restores the unmodified x4pro-uc8279-fast **0.1.12** source from
c081eebddad9ac61740b758e3fa74e5c92e325b1. Source hashes are pinned in
panel-012-selection-065.json. Normal automatic power-off and panel sleep return
with that original implementation; no .18 or .19 driver changes are included.

Contexts 0.4.1, RF service 0.3.2 and scene-host 0.3.1 retain their exact .70/.71
bytes, including learning progress and completion fixes. Springboard 1.7.30,
Lists 0.1.1, native Runtime 0.2.3 and the user-approved Home settling fixes remain.

Home 0.4.7 recovers T5S3-Reader's original ink sweep, forming blocks, unfolding
wordmark and loading dots. The owning System Apps repository records the
original source and its SHA-256. It emits immutable time samples through the
existing sliced software renderer, using row spans instead of allocated pixel
commands. The display retains custody of submitted frames. Cold boot and reset
play the animation; returning Home and retained reloads do not replay it.
Touch or navigation can skip it. Initial and final display waits continue
ordinary input/provider polling and do not wait for the driver's quiet settling.

## Qualification

- Original .12 normal and ASan/UBSan panel matrix and optimized profile pass.
- Actual adapter/Runtime/panel cadence passes at 1/8/20/50 ms caller intervals
  with 25 ms modeled foreground work, exact endpoint settling, continuing touch
  polling and 30 seconds without extra refresh. Actual product Light helper
  checks pass with both pending and completed settling, normally and sanitized.
- 288 complete raster comparisons match the original Reader C++ source,
  across animation times, opacity levels and replay band sizes.
- Actual Home/controller/renderer startup tests pass normally and sanitized:
  cold/reset, touch cancellation, reload isolation and equal final Home pixels.
  A 140 ms asynchronous display model verifies submitted-buffer immutability.
  The animation handoff originally had a 209 ms polling gap; servicing input
  while waiting reduces the maximum modeled gap to 4 ms. These are host model
  measurements, not physical touch latency or panel frame rate.
- Existing Springboard-to-Home endpoint checks pass with and without completed
  display snapshots, normally and sanitized, using the actual Springboard image.
- Home and panel Xtensa builds pass structural/import/export checks. The
  packager requires all 49 final ELF admissions and normal/sanitized cohort
  admission, deterministic SPIFFS with two independent readbacks, firmware/store
  SHA/CRC pairing and unchanged boot/native/AppData flash regions.

Build the panel with build_selected_panel_012.py and Home with the existing .69
profile plus --boot-animation and --relocation-tools pointing to the pinned
Reader scripts. create_boot_animation_072_spec.py records the clean source
commits, exact compiler, commands, dependencies and test evidence.
package_boot_animation_072.py composes only default.elf, default.json,
panel/driver.elf, panel/manifest.json and cohort.json over the exact .71 image.

Deliverable: X4-0.1.72-Contexts-Boot-Animation-Panel-0.1.12-full-0x0.bin,
16 MiB at offset 0x0. Full flashing replaces internal settings, bonds and
AppData. Removable SD contents are outside this image. Physical device behavior
remains to be tested; no device flashing, merge or release was performed.
