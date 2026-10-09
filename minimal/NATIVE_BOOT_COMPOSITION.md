# X4 native peripheral-rail startup

`minimal/native/X4EarlyBoot.cpp` is platform-owned native boot code. It supplies
a linker wrapper around the original Arduino `app_main`, a strong `initVariant`
milestone, and the optional generic Runtime startup status function. It only touches GPIO1. It stages HIGH and input/output configuration
before releasing a prior hold, re-engages hold, then checks actual pad HIGH.
Every failed operation records its exact stage; no error cleanup releases or
resets the pin. Runtime logs the failure and stops before bank preparation,
filesystem mounting, provisioning or driver/app activation. GPIO2 and GPIO5
remain exclusively owned by their ordinary touch and SD providers.

The peripheral rail is now established at entry to the original Arduino startup,
before its clock/PSRAM/NVS initialization. `initVariant` records the later boundary
before creation of the task that calls Runtime `setup`. The selected native
USB mode starts `Serial` inside `setup`; framework/ROM USB behavior can precede
this. This is an earlier software assertion, not proof of an electrical battery
latch deadline or successful physical RST behavior.

The GPIO1 mapping and HIGH convention come from the imported X4 pin map and
historical `X4BootPower.cpp`. The locally retained FreeInk/CrossPoint-derived
hardware notes identify GPIO2 touch power and GPIO5 SD power as active-low.
These sources do not establish the board's battery latch circuit, capacitor
timing, external-reset behavior, or a maximum reset-to-GPIO1 assertion time.

## Build a native composition

Use clean committed X4 and Runtime source. The default Runtime revision/version
comes from `minimal/sources.lock.json`; `--runtime-commit` accepts an explicit
40-character input for an isolated candidate, without changing that lock.

```sh
python3 minimal/scripts/prepare_native_runtime.py prepare \
  --runtime /path/to/pinned-riscrte --output /path/to/new-x4-native
pio run --project-dir /path/to/new-x4-native -e esp32s3-16mb-appdata-iq -j 1
python3 minimal/scripts/prepare_native_runtime.py stage \
  --runtime /path/to/pinned-riscrte --workspace /path/to/new-x4-native \
  --appdata /path/to/verified-initial-appdata --output /path/to/new-native-candidate
```

`--environment esp32s3-16mb-appdata-iq-perf` selects the existing diagnostic
environment at preparation time; use that exact environment for the build.
The composer defaults to `--app-policy-rows 16` and a disabled app image cache.
For a Runtime supporting both options, `--app-policy-rows 17 --app-image-cache`
explicitly selects seventeen immutable policy rows and the qualified image-cache
pressure paths. Live app grants and manifest requirements remain sixteen;
neither switch adds app authority. Options are independent of the selected
base, performance, or stage-log environment. The composition receipt records
both selections as `build_options`, including the default values. The pre-build
hook derives the native defines from that receipt and refuses external defines
or undefines for either option. Editing the receipt requires a new composition
digest, which must agree with the compiled firmware identity.

Preparation exports immutable Runtime Git bytes into a new directory, adds the
three explicitly listed X4 files and replaces the generated project's pre-build
identity-script entry. The Runtime checkout is never edited. Ordinary Runtime
and Watch builds contain no X4 hook. The older `scripts/prepare_runtime.py`
continues to serve the historical T5S3-Reader overlay and is a different path.

The pre-build script checks the complete composed source inventory. Both source
repositories' commit/tree IDs, the Runtime source epoch, source SHA-256 values,
generated config and composition digest are retained. Firmware retains the base
Runtime identity and a separate forced-linked `X4_NATIVE_COMPOSITION` marker.
The product stager reuses the pinned Runtime's ESP image, partition, rollback,
TLS-root, initial-appdata and IQ proofs, then checks strong X4 hook symbols, RTC no-init placement and the actual linked
IDF main_task → wrapper → original app_main → initArduino call edges.
The resulting `candidate.json` explicitly records `x4_native_composition`, and
hashes every frozen asset including the composition inventory and X4 ELF proof.
It must be consumed together with the matching product store and custody record.
`x4-runtime-options-proof.json` proves the actual selection: the exact policy-row
marker must be present without its opposite in both firmware and ELF, and must
match the retained Runtime symbol. Enabled caching requires the compiled owner
state, strong reclaim/create functions, and actual Xtensa calls from Runtime
execution to cache creation and through the pressure reclaim path. The cached
loader API alone does not prove activation. A cache-disabled candidate must
exclude the enabled state and implementation. The candidate retains the full
options proof and the exact build selection; its product consumer must recompute
that proof and require its intended options. These are source/linkage proofs,
not hardware qualification.

`native_proof` retains its shared format; X4 proof lives separately so consumers
can still recompute shared proofs. The full product builder must require the X4
composition marker and source evidence for a product that relies on early hold.
Changing only a product store cannot install or retrofit this native hook.

## Verification

```sh
X4_ARDUINO_FRAMEWORK=/path/to/framework-arduinoespressif32 \
  bash minimal/test/run_early_native_boot_test.sh
python3 minimal/test/native_composition_test.py
```

Host tests compile the actual boot source, the SHA-256-pinned Arduino main.cpp
and its exact initArduino body against an IDF shim. They cover unheld, held HIGH
and held LOW starts, every checked failure, input sensing, first-failure
preservation, no other GPIO writes, both framework USB modes, reset causes and
torn/corrupted RTC records. See BATTERY_BOOT_INVESTIGATION.md for reference
evidence and the remaining hardware distinction. Composition tests cover
clean/exact source inputs, unchanged upstream bytes, source/receipt mutation and
rejection of a different build environment. They also exercise default and
explicit options, invalid types and bounds, external flag overrides, compiled
policy mismatches, and cache implementation mismatches. They are not hardware qualification.
