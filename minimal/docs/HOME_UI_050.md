# X4 0.1.50 Home integration

This recipe updates only `default.elf`, `default.json` and `cohort.json` in the
immutable delivered 0.1.49 store. Runtime 0.1.99, SD 0.2.13, panel 0.1.9, all 20
non-Home apps, all 23 providers, boot grants, bootloader, partitions and initial
AppData are copied byte for byte. It is not a native/provider source rebuild.
The frozen binary and original source archive remain the provenance for those
components; older provider sources elsewhere in this checkout are not build
inputs to this recipe. Original native source is 11b16cf60c5d3bd9b6a202f25b7b86a2ff05ca21,
tree fc135aef0b12314df77a793ab6f94ca95b84ed02.

Home 0.3.22 is rebuilt directly against the separate .99 SDK repair e66c5f1,
using System 9d32d82 and the helper originally qualified at product 3ac5d91.
The SDK repair restores source compatibility only. Native .99 bytes are frozen.

Home idle enters desk-clock Deep. Home time taps do not open Springboard.
Other Home swipes open Springboard, except downward Quick Actions; Points taps
launch on release and motion cancels tap. This is Home's Points region, not a
new Points application build.

The centered Sleeping card belongs to Home's resident host. Existing foreground
clients already support its policy checkpoint, so no client rebuild is needed.
All 19 foreground apps route through Home. USB SD Transfer always inhibits idle;
GameBoy uses legacy handoff and does not receive this card. Capture-active
Contexts/Waterfall, busy Wi-Fi/update operations, modals and incomplete display
work defer idle. Confirmed cleanup restores the prior child frame; uncertain
retained custody allows no repaint.

Original .99 firmware.elf was not delivered. Native ELF-symbol verification is
therefore explicitly unrun. `native_binary_exports.py` instead validates the
exact firmware's ESP checksum and appended SHA256, resolves complete compiled
public export pointer tables, and checks every ordered name, address and terminal
record. Its method is cross-checked against all export names and addresses in
the known .98 BIN and ELF. Resolver source is byte-identical. Production Runtime
cohort admission uses those actual .99 BIN export names, not .98 addresses.
Target instructions and the target resolver are never executed on the host.

Use `package_home_ui.py --help` for the frozen-input recipe. It requires clean
pinned sources, exact target receipts, production normal/sanitized admission,
independent Python/C SPIFFS readback, paired hashes/CRC and preserved-region
comparisons. Final loader tests and independent artifact review are additional
gates. Physical boot, power, SDMMC sleep/resume and e-ink timing remain untested.

This is a full 0x0 first-install image. Flashing overwrites internal settings,
bonds and AppData; back up first. Removable SD contents are not included. This
work does not flash a device, publish a release or change any source remote.

The matching .47 packaging-tool checkpoint 5b9fd57 models USB PHY and diagnostic
source capabilities. For frozen .49 admission, those same native selections are
explicit fixture inputs from the archive contract, additionally checked for
capability strings in the exact BIN. Original ELF-based selection-marker lookup
is unrun. The adapted harness source is saved and hashed in each admission
result; the production Runtime, CpuPort, Graph and ELF-admission function are
unchanged. Diagnostic and hardware callbacks are inert and counted.
