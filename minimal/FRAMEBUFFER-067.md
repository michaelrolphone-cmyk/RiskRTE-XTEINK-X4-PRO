# X4 0.1.67: cached software framebuffers

This private development image replaces Home with 0.4.4, Springboard with
1.7.30 and the selected display provider with `x4pro-uc8279-fast` 0.1.16.
It is a direct 16 MiB full image at 0x0. The native Runtime 0.2.3, latest
Lists 0.1.1, Contexts 0.4.0, scene-host 0.3.0 and the other qualified
0.1.66 modules are preserved. The concurrent Contexts recipe is retained; .67
uses its exact validated image as baseline to avoid reverting that app upgrade.

UI drawing now targets reusable software pixels. Springboard pages and its
selection tile, Quick Actions contents and the Home scene are cached. Dragging
composites translated pixels; Home transition-only frames reuse the prepared
Home image. A completed unsent ordinary frame can be replaced during display
BUSY, while submitted provider pixels remain immutable. Cache rebuilding still
uses bounded cooperative painting; command slabs and the working framebuffer no
longer allocate every frame. This preserves input/model/display separation.

Panel 0.1.16 is imported byte-for-byte from the repaired exact source checkpoint
`e2cca3d9660011c79c328efb82bd72d13cdf6160`. Its separate source selection records
the driver, manifest, README and matching tests. This is the absolute-A2 version
with four-frame localized and two-frame broad active refresh, retaining endpoint
settling. No waveform or electrical edits are applied here. The pinned GCC 8.4
linker aborts on this source at O2; the selected-panel builder uses O1 and verifies
relocations, exports/imports and final native admission.

The native firmware and boot chain are reused byte-for-byte from the qualified
0.1.65 image. Packaging validates all 49 ELFs, normal/sanitized full-cohort
admission, deterministic SPIFFS, two independent readbacks and paired bank CRCs
and SHA-256. Only the two apps, panel package and cohort identity may change.
The panel alone is compacted with allocated bytes and loader metadata preserved.

Qualification includes 1,728 complete-buffer compositor comparisons, retained
cache generations, immutable BUSY submissions, allocation-free warm compositor
frames, production touch gestures, Home endpoint parity, Quick control output
parity, 94 asynchronous Light sleep/retention cases, all focused panel scenarios
and real adapter/Runtime/panel cadence at 1/8/20/50-ms caller waits. Hardware
rendering latency, contrast and ghosting remain unverified for this image.

Build `components-066/apps/{default,springboard}` with the recorded selected app
commands, then `build_selected_panel_016.py` with the explicit Runtime and Reader
SDK roots. `create_framebuffer_067_spec.py` binds clean source identities, exact
inputs and qualifications; `package_framebuffer_067.py` assembles only those
inputs over the qualified Contexts .66 baseline. Native source custody still points to
the unchanged qualified native platform checkout rather than this newer product
tree. Contexts and its scene provider remain byte-identical to the qualified
.66 baseline; their capability policy is preserved and the combined cohort is
readmitted against the unchanged native runtime.
