# Typed minimal-runtime X4 integration

This directory is a separate product adaptation of the source-preserving
migration. Its ordinary providers run on the shared headless RiscRTE through
exact typed capability tables. Original migrated `Drivers/` remain unchanged.

`sources.lock.json` pins the provisioning/sync/GPIO Runtime candidate and the
one-file shared FatFs helper candidate. Nine providers are implemented:
board power, I2C, panel, frontlight, buttons, RTC, SD, battery and GT911. Two
explicit panel variants have real JSON/graph admission coverage. The complete
application/service bundle and provisioning integration remain pending.
Do not treat this checkpoint as a bootable or hardware-qualified product.

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
