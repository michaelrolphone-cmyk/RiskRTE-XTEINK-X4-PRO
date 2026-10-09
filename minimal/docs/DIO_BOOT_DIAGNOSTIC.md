# Recorded DIO diagnostic composition

`prepare_native_runtime.py prepare --boot-flash-dio` preserves the flash selection
used by the recovered X4 0.1.29 battery-startup diagnostic: DIO flash at 80 MHz,
16 MiB, octal PSRAM, 240 MHz CPU. Without that explicit switch the existing QIO
composition bytes are unchanged. This does not assert an electrical fix or a
successful battery-only startup.

The composition records `boot_flash_experiment: "dio-opi-80mhz"`, as the recovered
.29 composition did. Only the selected environment receives the two recorded
memory-type/flash-mode overrides. The pre-build hook checks actual BoardConfig
values, the pinned SDK config, SPI-flash archive and DIO bootloader ELF. External
flash/PSRAM option macros are rejected. Complete composed-source custody and the
compiled composition identity still apply. No board pin or PSRAM mode changes.

The stage and product consumer recompute a DIO proof from the actual ELF and
both image headers: DIO/80 MHz/16 MiB, the exact reviewed 14,032-byte rollback
bootloader, and `default_chip.read_mode == SPI_FLASH_DIO` at its pinned ESP-IDF
layout. QIO retains the existing shared bootloader admission. The proof is
retained as `x4-boot-flash-proof.json` and included in the native composition
summary. The shared Runtime source and its stager remain unchanged.

## Recovered evidence

The frozen files were reviewed directly; their SHA-256 identities are:

- `diagnostic-custody.json`: `fa99e971a9e86e4aa6809b4cabdd7d1d933b13de792455d4e9340544ccefc673`
- `reference-build-custody.json`: `20366376223d3b38c91b8b35968f9e3aa894ae9e39e642a596dfb94949e98f0e`
- `native-diagnostic/boot-flash-experiment.json`: `d63018cfa38c832f9ad4ddc33ffc19480dbce9fe0ee49e7a2f1e90b28a31298f`
- `native-diagnostic/x4-native-composition.json`: `058def218f4760eaf52b48bcf6c8f4e1e3db3cf6db4f702d756933fce64bb464`

The .29 receipt binds firmware SHA `ef870ccb90abd45ef333f7938e5bf04f057273da0174474e9ec1f6c23022e045`
and ELF SHA `df486594c1613213e4837cda3b7dfd0f4e182bbe98298b3a78f1d77121a2f82e`.
Its bootloader SHA is `1033730a6df733f53a7a347353c1c5450547f76e98e0746da633079310a563b9`.
The .28 reference is retained only as provenance; this composition does not
relabel old native bytes with a new product identity.

`minimal/test/native_flash_profile_test.py` exercises invalid selections,
board/SDK mismatches, and exact real .29 target proof plus mutations when
`X4_FROZEN_DIO_NATIVE` names that artifact directory. Composition tests prove the
upstream default is unchanged and a rehashed selection cannot authorize a
mismatched source overlay. Pinned Arduino early-start tests separately cover
both USB modes and every rail operation failure. Hardware remains unqualified.
