# X4 0.1.51 shared plaintext keyboard composition

This local product advances the immutable 0.1.50 image. Its explicit selected
inputs and byte pins are in `minimal/apps/shared-keyboard-051.json`. Nothing here
publishes a release or operates hardware.

## Inventory and preservation

- Runtime 0.1.100 uses the qualified no-telemetry native build, with hardware
  SDMMC, USB serial restoration, 26 providers and 42 shared graph grant slots.
  DIO/80 MHz, octal PSRAM, IQ/stage, 17 app-policy rows, 512 retained wake bytes,
  image cache, failure recorder, USB PHY and early boot remain selected.
- Points 0.6.12 and BLE Scanner 0.2.19 preserve their complete installed resident
  flags, native time, tagged alarm API2, diagnostics, touch behavior and grants.
  Each adds exactly one `ui.text-input@1` requirement and instance-zero grant.
- Three providers are added: text-input-host 0.1.0, scene-host 0.1.1 and
  scene-profile-portrait-monochrome 0.1.1. The corrected portrait profile uses
  display rotation 270 and touch rotation zero, matching installed X4 geometry.
- All 19 other apps, including Home 0.3.22, and all 23 existing providers are
  byte-identical to 0.1.50. There are 21 apps, 26 providers, 47 ELFs and 97 files.
- Bootloader, partition table, empty AppData image and inactive bank bytes remain
  identical. The active native firmware, module store and paired hash/CRC record
  advance together.

## User-visible scope

Points custom-type names and BLE per-address sensor aliases use one external
plaintext keyboard. Neither converted client embeds a local keyboard fallback.
Points names retain their 31-character limit; BLE aliases retain their
24-character limit and an empty name clears an alias. The host accepts bounded
printable ASCII; this is not a password or arbitrary Unicode editor.

The shared adapter silently retains an invocation when Runtime or text-session
custody becomes terminal. It must not emit Runtime stage diagnostics after
liveness is lost. Exact installed-profile regressions retain stage logs and
assert no subsequent provider, health, diagnostic or allocation calls.

## Validation boundaries

The packaging helper verifies clean exact sources, all compiled input hashes,
raw target identity, preserved flags/grants and semantic ELF compaction. It runs
strict production role/import checks against the exact native ELF, normal and
ASan/UBSan whole-cohort metadata admission, deterministic store generation,
independent Python/C readback and paired SHA/CRC verification. All final ELFs
also pass the production loader host-hook uncached/cache-miss/cache-hit paths.

Per-app fixtures replay actual `app_main`, cleanup and repeated invocation with
the exact installed build flags. Service fixtures check layout, touch/raster
agreement, host lifecycle and retention. A separately identified Runtime/Graph
fixture uses synthetic peripheral providers and controller entrypoints; it is
not the production hardware graph or target instruction execution.

## Requirements still open

Physical hardware-keyboard preference is not delivered: this cohort has no real
`usb.hid.keyboard` input provider plus host/controller closure. Existing outbound
BLE HID and USB mass storage are not input keyboards. The selected text host
therefore depends only on the shared scene service and uses its onscreen UI.

Other editors (legacy Points, Timecard, Wi-Fi, filenames and LoRa/audio/RF labels)
remain unmigrated. Generic consumer leases for abandoned text sessions are not
included. Physical boot, keyboard/touch pixels, SDMMC, sleep, USB restoration,
RF behavior and live memory headroom remain untested. Native static layout costs
and synthetic graph peaks do not establish hardware memory safety.

The full 16 MiB image at offset 0x0 overwrites internal settings, Bluetooth bonds
and AppData. Back up first. Removable SD contents are not included in the image.
