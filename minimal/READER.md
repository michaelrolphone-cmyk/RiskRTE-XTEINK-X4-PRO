# Reader composition

The new e-ink application comes from RiscRTE-Productivity `reader/`, derived
from the pinned CrossPoint source and using shared NOVA components. This branch
adds the X4 selection tooling and upgrades the SD provider to the canonical
Drivers `StorageFatFs` implementation. The old T5S3 project stays read-only.

Build the matching Runtime, System page presenter, shared storage provider and
Reader together. `build_storage_fs.py` deliberately names `--drivers` as the
filesystem source. `--reference-reader` supplies unchanged legacy interface and
wire-test fixtures only; it never supplies the new volume implementation.

```sh
python3 minimal/scripts/build_storage_fs.py --runtime ../RiscRTE \
  --drivers ../RiscRTE-Drivers --reference-reader ../T5S3-Reader \
  --output build/reader-sd
python3 minimal/scripts/test_storage_fs.py --runtime ../RiscRTE \
  --drivers ../RiscRTE-Drivers --reference-reader ../T5S3-Reader \
  --output build/reader-sd-tests
python3 minimal/scripts/stage_reader.py --baseline-store /path/to/verified/store \
  --reader-package /path/to/ebook-reader --scene-provider /path/to/scene-host \
  --storage-provider build/reader-sd --output build/reader-cohort
```

The reference fixture revision is
`45cf61ac013fb618e8d7fed63217a35350484d52`. Its imported shared storage ancestry is
recorded in Drivers `lib/StorageFatFs/UPSTREAM.json`. Existing historical build
scripts remain historical; do not select their old volume source for this app.

The staging script adds the Reader manifest, resident foreground entry and
explicit `ui.scene`, `storage.volume`, `memory.heap`, `random.bytes` and
`file.open` grants. It preserves all existing apps and removes the copied old
cohort identity. A product assembler must create a new identity for an admitted,
capacity-checked full cohort before creating any flash image.

## Current deployment constraint

X4 `.65` already contains approximately 4.7 MB of bootstrap files in its
`0x510000` (5,308,416-byte) store partition. The initial reader embeds CrossPoint's
Noto families and exceeds that remaining space. `stage_reader.py` reports exact
payload size before filesystem overhead. A fitting product choice is still
needed: a different app selection, a planned asset/storage arrangement, or a
different partition layout. None is silently selected here.

Runtime 0.2.4 is required for the shared heap/random capabilities, public C++
helpers and zero-relocation handling. An app-only update over `.65` is not a
complete deployment. Host and ELF tests are development evidence; no hardware
flash, release, physical refresh, battery or power-loss qualification is claimed.
