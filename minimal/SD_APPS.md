# X4 0.1.73: first SD application cohort

The directly flashable BIN retains ten core applications and all 27 providers.
The separate SD bundle supplies fifteen optional application ELFs and Reader
fonts. Copy its `Apps/` and `fonts/` directories to the existing card root,
merging directories. Do not format the card or replace books, ROMs, models or
`System/State`. The full BIN is a 16 MiB image at offset zero and overwrites
internal settings/AppData. The SD card is independent of that image.

Core: Home, Springboard, Settings, Wi-Fi Settings, File Browser, USB SD Transfer,
OTA Update, App Store, Alarms and Battery. Optional apps include the current
Contexts, Spectrogram, Lists, Model Viewer, Hollow Trail and Reader. The stable
panel 0.1.12 executable is unchanged from 0.1.72. Apps request capabilities;
only platform selection chooses that driver and version.

Runtime 0.2.5 resolves each external executable through `storage.volume@1`.
It reads a fresh private snapshot, closes the source, then performs ordinary
ELF validation/relocation. No SD handle or raw source pointer remains in an app
mapping. A missing card does not prevent core boot. Absent/corrupt app files
return through the existing failure UI; uncertain close/release retains custody.
Model Viewer and Hollow Trail use explicitly registered destructive handoff,
as their recovered binaries do not carry resident foreground descriptors.

This first stage keeps app identities, grants and launcher entries in firmware.
Adding a new identity/grant still needs a platform policy update. Flash-only
App Store updates deliberately reject external apps; replace their SD files.
SD executables are never placed in the immutable internal image cache.

`sd-apps-073-qualification.json` records exact artifacts, source-tree publication
mappings, capacity and gate results. Tests cover normal/sanitized runtime
launch/return, image replacement, absent/busy/removed media, positive short
reads, corrupt/truncated images, allocation failure and checked-close retention.
All 52 app/provider images pass native admission; 37 internal images also pass
full cohort admission in both test modes without hardware/storage calls.
Reader engine, scene component and production SD filesystem tests pass.
Physical device testing remains pending.

Build with the recorded Runtime/System/Drivers/Productivity trees. Preserve
the qualified 0.1.72 store as baseline. Build the Home and Springboard using
`sd-app-catalog.json`, retaining the existing raster snapshots, boot animation,
resident, idle-sleep and settled-display flags. Build Reader with Productivity's
`scripts/build_reader.py`, shared scene services with System's
`scripts/build_scene_services.py`, and the SD provider with
`scripts/build_storage_fs.py`. The recovered Canvas binaries and their receipts
are carried forward unchanged. Stage an immutable single-job native Runtime
build with the existing policy24, requirement24, image cache, USB PHY,
retained-wake512 and failure-evidence options. `scripts/package_sd_apps.py`
accepts those components, the staged native candidate, its exact platform source,
the baseline BIN and the existing packaging tools. It admits both internal and
external images, checks the internal cohort, round-trips SPIFFS twice, verifies
the paired bank SHA/CRC, and emits the BIN plus deterministic SD ZIP.
