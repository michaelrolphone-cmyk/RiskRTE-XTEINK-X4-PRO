# Typed minimal-runtime X4 integration

This directory is a separate product adaptation of the source-preserving
migration. Its ordinary providers run on the shared headless RiscRTE through
exact typed capability tables. Original migrated `Drivers/` remain unchanged.

`sources.lock.json` pins the provisioning/sync/GPIO Runtime candidate and the
one-file shared FatFs helper candidate. Nine base providers are implemented:
board power, I2C, panel, frontlight, buttons, RTC, SD, battery and GT911. Two
explicit panel variants have real JSON/graph admission coverage. The offline test-image builder composes an explicit completed app/service
cohort and verifies it with the production Runtime admission harness. The user
reported a rendered frame from an earlier candidate; new controls, sleep and
performance changes still require physical verification.

The board-power provider establishes the peripheral rail; panel candidates
explicitly select SSD1677 or UC8279 protocol and matching reset/BUSY/offset
configuration. No physical controller identity is guessed. SD remains native
one-bit CLK/CMD/DAT0, not SPI. Battery and RTC are included and versioned in this
integration rather than silently omitted from the package selection.

## Build and host checks

Check out the immutable Runtime and shared-source commits from the lock, then:

```sh
export RISCRTE_RUNTIME_ROOT=/path/to/RiscRTE
export RISCRTE_READER_ROOT=/path/to/T5S3-Reader
for d in board_power i2c panel frontlight buttons rtc sd battery gt911; do
  SANITIZE=1 bash minimal/test/run_${d}_test.sh
done
python3 minimal/scripts/build_drivers.py \
  --runtime "$RISCRTE_RUNTIME_ROOT" --reader "$RISCRTE_READER_ROOT" \
  --cc /path/to/xtensa-esp32s3-elf-gcc --output build/minimal-drivers
```

Use Python with pyelftools0.32 and the pinned Xtensa8.4.0 compiler. The builder
composes canonical shared headers (divergent duplicates fail closed), validates
relative relocation targets, rejects privileged imports and PSRAM compare-and-set,
and records provider versions, source hashes and ELF hashes in products.json.
It includes shared FatFs/RTC sources directly; no SDK or loader source is moved.
The generated output is a driver test artifact, not a flash image.

Host wire simulation and target linking cannot verify electrical timing,
controller identity, power retention, battery/RTC telemetry or real media.
All physical execution is unrun for these new provider versions.

The native GPIO readback regression compiles the production board-power provider,
JSON materializer, scoped CPU GPIO table and `NativeSleep::openPin` against an
SDK shim that returns zero when a pad's input path is disabled:

```sh
bash minimal/test/run_native_gpio_readback_test.sh
# Reproduce the original failure against Runtime 5bb6da5:
bash minimal/test/run_native_gpio_readback_test.sh expect-broken
```

It checks both panel profiles, GPIO1 readiness, forced-low pad detection, SD CLK
GPIO41 low/high/low readback, release and stale-token rejection. This exercises
the SD clock GPIO contract, not a complete SD card protocol or physical hardware.
The ESP-IDF 4.4 GPIO API documents that `gpio_get_level()` always returns zero
unless the pad is configured for input or input/output; output-latch-only mocks
cannot detect this boot failure.


## Completed product candidate

Product0.1.4 selects manual crown light sleep with the explicit GPIO3 power
provider and split navigation, Runtime0.1.45 scoped GPIO acceleration and
cooperative UC8279 transfer. Center Home returns to the clock. A completed
short top-right press returns to the clock from an app; on the clock it requests
light sleep, and GPIO3 wakes it. An open QuickActions sheet consumes the first
press as dismissal. Top-edge pull-down opens QuickActions; other clock swipes
open Springboard. Deep/hybrid/idle/touch wake remains unimplemented.

The10 executable apps are Clock, Springboard, File Browser, sensor-backed
Bluetooth Scanner, Points in Time, Settings, Calculator, Stopwatch, Countdown
and Timecard. The explicit eight-entry launcher catalog is apps/catalog.json.
Source pins are in apps/sources.json; individual build receipts and Font Awesome
licenses accompany the image. Radio toggles in QuickActions remain unavailable
until a Wi-Fi provider is selected. Alarms are visual-only. Serial Monitor,
RF Spectrogram and remaining supported Watch apps are not included yet.

Use build_drivers.py --sleep, build the pinned apps with their paper/control
flags, and call build_test_bundle.py --sleep with the JSON map of artifact
directories. It emits a full16MiB BIN at offset0x0. This is a new-device test
layout and overwrites partition table, NVS and app data; it is not a preserving
update. The explicit --panel uc8279 selection must match the controller.
