# Private X4 0.1.65 test build

This build corrects the resident Light-sleep handoff after asynchronous rendering:
the host waits for the Sleeping overlay and restored app image to complete,
and processes ordered touch/Home cancellation while waiting for panel settling.
The previous premature retention stopped before frontlight shutdown and required
RST to recover. The updated host passes 94 simulated sleep, wake, cancellation,
refusal and retained-custody cases, including normal and ASan/UBSan execution.

Home records compact glyphs, dial ticks, rounded shapes and dock bitmaps. A
representative scene drops from 4,413 span commands to 212 immutable operations;
96 scaled/highlight/row-slice comparisons are pixel-identical. Springboard
inhibits background resident policy while a contact, snap animation or launch
is active, preventing focus/storage work during the final snap. Both apps keep
input, model, software raster and panel progress independently scheduled.

The exact x4pro-uc8279-fast **0.1.12** provider source, manifest and README come
from `c081eebddad9ac61740b758e3fa74e5c92e325b1`. They are byte-identical to that
version. `panel-012-selection-065.json` records hashes; the selected builder
uses the current canonical SDK without changing the waveform. Normal/sanitized
provider and real idle-helper tests pass. The actual adapter + Runtime + panel
model completes final-target settling and power-off at all four tested polling
intervals; these deterministic BUSY/GPIO times are not hardware benchmarks.

Lists includes Productivity PR 19 at `0c9318dd649a50d61e26250f996cc9d738d13cf8`
and System PR 93 at `eec748ca081fe59346be03180600c65013cc7298`, including corrected
row icon insets and dialog button outlines. The selected versions are Lists
0.1.1, scene-host 0.2.1, Home 0.4.3 and Springboard 1.7.29. Contexts, SDR and the
qualified native Runtime 0.2.3 remain the upgraded versions delivered in .64.

`home-sleep-065-sources.json` maps local and public commits by exact Git trees.
`create_home_sleep_065_spec.py` binds the delivered .64 baseline, new component
receipts, compiled inputs, test evidence and reused native composition.
`package_home_sleep_065.py` verifies all 49 ELFs and the complete cohort in normal
and sanitized admission, independently reads SPIFFS through both implementations,
and verifies paired bank hashes/CRC and unchanged native/boot/AppData regions.
The two replaced providers use the existing `loader-metadata-v3` compactor,
with complete code/data, symbol, program-header and relocation equivalence
checked against their retained original ELFs. This preserves bootfs free space.

Deliver the full 16 MiB firmware directly as a `.bin` at offset `0x0`. It is a
private development test, with no ZIP delivery or release publication. Hardware
sleep/wake reliability, responsiveness and panel ghosting require device testing.
