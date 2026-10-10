# X4 0.1.44 held test image

The local image is frozen and independently checked. It has not been tested on hardware or delivered to the user.

## Included

- One resident default.elf Quick Actions renderer, shared by Home and nineteen selected foreground apps; capability-driven audio omission, working frontlight controls, USB entry, clean refresh and supported sleep actions.
- Refined Home dock, 3 × 4 horizontal Springboard, Points default desk clock, unpadded hours and retained full Points content. Interactive wake restores the prior light state; timer-only refresh remains dark.
- Storage-scaled Points catalog/editor, full labels and symbols, preserving existing KV permissions and adding a fresh bounded AppData namespace.
- Original GameBoy UI with timestamped selected-ROM loading stages, qualified GPIO read acceleration, and the separate stat-output-pointer repair for existing save/config files.
- Healthy-host app failure pages and available prior-reset evidence. No stack trace or prior-app identity is fabricated when the Runtime does not expose it.

## Validation

The complete 16 MiB image, seven partitions, erased gaps, native/source/options proofs, all 44 ELFs, nineteen foreground roles and grants, and all 21 application compactions passed. Independent review completed 3,429 checks with no findings. The accepted 0.1.43 DIO bootloader, partition table and initial AppData bytes are identical. The 408-byte Clock snapshot fits the compiled 512-byte retained capacity. Production metadata admission makes zero hardware or storage calls. GameBoy passed the sanitized Runtime 0.1.90 lifecycle journey, with separate clean legacy handoff/return coverage.

## Known limits

- Windows USB mass-storage mounting and Device Manager hangs remain unresolved. No USB success claim is made.
- The separately stopped Runtime 0.1.87 USB-exit/serial-restoration target remains unverified and is excluded.
- GameBoy uses a clean legacy handoff; the resident Quick Actions overlay is unavailable while it runs.
- Manual LOW POWER policy remains queued. There is no decorative tile or substituted sleep action.
- Panic/early-boot failures cannot safely draw through the current app-level display path. Prior-reset notices may repeat after a new host invocation because the reset API is read-only.
- No hardware performance, free-heap, USB-host VBUS or preservation-update claim is made. This is a first-install image; flashing it is destructive to internal device data. The SD card is not part of the image.

## Frozen identities

- Product source: 912417e970149b5c577fb7ae70a6bd668bbafaff
- Runtime source: 5ab1f4e9e3f17efb1153966868b52660bde53be0 (0.1.90)
- Native composition source: efd4c0097b0450289188ff03f71e2068944016fa
- Image: xteink-x4-pro-0.1.44-resident-shell-first-install.bin
- Image bytes: 16777216
- Image SHA256: 46bd3f7323a8964b56d16b05331ad7339fb9b7a952119552ef61129536f99841
- Full composition receipt SHA256: 1da54704611d367dafeca571cf45281912f79415b18885ce5a64b96d3c12643d
- Independent review SHA256: 42e0b4a01ea01cff573ddbbfd928b97161ef92bce2e2a8baebe2c290a0b14380

Source publication identities are recorded separately; built artifact identities remain unchanged. Some dependency source publication is still blocked independently of this product checkpoint.
