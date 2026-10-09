# USB SD Transfer development selection

This opt-in addition preserves the existing 17 catalog entries and adds USB
Transfer as the eighteenth entry, on Springboard page 2. Rebuild only the
selected Springboard with `--quick-usb-transfer` (version 1.7.17) and install the
new `usb_sd_transfer.elf` 0.1.0. The unchanged Clock and Settings binaries do not
have the new Quick Actions row. Springboard's own sheet has it. The dedicated
app also remains reachable from its page-2 icon.

The computer is the USB host. X4 exports its SD through the shared
`usb.device.msc@1` provider. `usb-transfer.json` records the exact four grants:
display 3, raw touch 4, navigation 6, and unique USB MSC 0. Springboard and
Clock do not gain USB or raw block authority. No sleep authority reaches the
transfer app. The original `.30` files are not modified by this source change.

The bundle assembler can call
`usb_transfer_app.validate(manifest, elf_bytes, build_record, system_commit,
msc_header_bytes)` to validate the new dedicated app and obtain its grants.
This helper does not change the existing generic APPS inventory or rebuild a
cohort. The selected native firmware and providers must separately admit the
USB PHY owner and SD export custody.

Connect a data cable, open USB Transfer, and tap Start. Copy `x4-boot.log` and
`x4-boot.previous.log` from the mounted drive's root. Eject the drive on the
computer when finished. The app reports safe eject/local media readiness from
the provider, consumes the session, and releases its grant before Home/Back.
USB suspend remains exported; no SOF or suspend event is treated as unplug.
Without a verified VBUS detector, Stop after configuration offers a separate
"Cable removed" confirmation if host eject has not completed. Never confirm
that while the cable remains attached. Retained cleanup keeps navigation and
sleep blocked and permits an explicit cleanup retry.

Host callbacks and target linking are qualification evidence, not evidence
of physical host enumeration, cable behavior, remount on real media, or power.
