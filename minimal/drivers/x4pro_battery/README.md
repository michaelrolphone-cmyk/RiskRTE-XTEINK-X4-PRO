# Ordinary X4 Pro read-only battery adapter

`x4pro-battery@0.1.3` adapts the preserved Reader `0.1.2` provider to ordinary,
device-scoped Runtime tables. Its public `board.battery@1` sample and API layouts
are unchanged. The original `Drivers/x4pro_battery/` files remain byte-identical
to Reader commit `34d8e694d89a1e72d8854403d8592c289fae3ddc`, repository tree
`e7dc2a45c3553f0102a7c2e2c2c9fe5bc51b00a2`.

## Explicit protocol and authority

The manifest accepts only `cellwise,cw2017-readonly-gauge`, revision
`unspecified`, with `peripheral.i2c` configuration version 1. This identity
names this CW2017 read-only protocol; it does not claim compatibility with
CW2015, another fuel gauge, or an arbitrary device answering at `0x63`.

The typed configuration must supply:

- A full I2C bus descriptor with nonzero instance ID, I2C kind, mode zero and
  reserved bytes zero. The loader must scope `i2c.bus@1` to that bus instance.
- Address `0x63`, `chip_id=0`, and zero reserved device fields. VERSION is a
  running-state check, not a fixed silicon ID to substitute into `chip_id`.
- `irq=21`, `irq_active_high=1`, `irq_pull_up=0`. The shared peripheral type's
  IRQ field supplies an ordinary polled charge-status input, not an interrupt
  subscription. `platform.gpio@1` receives only input/no-pull claims for GPIO21.

Exactly one each of `hardware.device@1`, `i2c.bus@1`, `platform.gpio@1`, and
`platform.sync@1` is required, in any order. Missing, duplicate, extra, null,
short, or incompatible dependencies fail before bus/GPIO I/O. The bus must
provide the existing tagged safety suffix: serialization, total transaction
deadline and retained release. The GPIO table is checked through its `release`
member; later optional GPIO suffixes are not required. The synchronization
contract is owner-task-only and nonblocking. No raw GPIO/MMIO, OS, task,
firmware-peripheral or provider-BSS atomic imports are used.

## Preserved sampling behavior

Startup and each sample perform at most four synchronous register-pointer /
repeated-START read transactions, in this order:

1. VERSION `0x00`: accept exactly `0x0d` or `0x0f`. Power-on `0xa0` is not ready.
2. CONFIG `0x08`: require exactly zero, including reserved low bits.
3. VCELL `0x02`: read two bytes; require an unsigned, nonzero 14-bit value that
   converts to nonzero millivolts using `(raw * 5 + 8) >> 4`.
4. SOC integer byte `0x04`: require `0..100` inclusive.

There are no register-data writes, profile/BATINFO initialization, reset, wake,
polling or retry loops. The requested total deadline is 20 ms per transaction.
This does not claim a hard 20/80 ms wall-time bound or physical timing proof.
GPIO21 is claimed only after the startup gauge values pass every check. Each
sample reads GPIO21 after gauge validation; HIGH means charging only. It does
not establish VBUS/USB presence, full charge, battery health, temperature,
current, profile correctness or calibrated SOC.

The caller's entire sample remains unchanged on any failure, including GPIO
read or synchronization release failure. Successful samples are committed only
after the final guard release. Reentry and calls outside the owner task fail
without I/O.

## Lifecycle

Repeated start cannot overwrite live or retained bus/GPIO/sync tokens. Failed
startup cleans up any acquired resources when possible. Failed bus or GPIO
release preserves the exact token, disables samples, and allows a later
`quiesce()` or `stop()` to retry. Successful partial releases are not repeated.
A failed sync destroy retains its unlocked token for retry. Cleanup is
idempotent after success. Failed sync unlock retains the provider and all
remaining dependencies fail-closed; it is not treated as safe to unload or
restart. No cleanup path changes gauge registers.

## Evidence and verification limits

The register and board-polarity rationale, datasheet links and original
reverse-engineering limitations are retained in the
[source provider's evidence notes](../../../Drivers/x4pro_battery/README.md).
This adapter preserves those constraints rather than adding hardware claims.
Physical verification is pending; no battery, gauge, charging state or timing
has been checked on an X4 by these host/target tests.

Run the real driver against scoped fakes with the canonical composed SDK:

```sh
RISCRTE_RUNTIME_ROOT=/path/to/RiscRTE \
RISCRTE_READER_ROOT=/path/to/pinned/Reader \
SANITIZE=1 ASAN_OPTIONS=detect_leaks=0 \
bash minimal/test/run_battery_test.sh
```

Six process-isolated scenarios exercise malformed dependencies/configuration,
all 256 VERSION, CONFIG and SOC byte values, all 65,536 raw voltage values,
read-only startup, active-high input/no-pull behavior, transfer/GPIO failures,
atomic sample output, owner fencing, reentry, repeated lifecycle, partial
cleanup/retry and retained unlock failures. `detect_leaks=0` permits sanitizer
execution in ptrace-based sandboxes; ASan and UBSan remain enabled.

To include the Xtensa ESP32-S3 ordinary-provider link and relocation/symbol
checks in the same command, set `NATIVE_DRIVER_CC` to the target GCC binary and
`PYTHON` to an interpreter with pyelftools. The test requires the sole function
export `t5_driver_get`, at most the libc import `strcmp`, valid relative pointer
targets, and no `s32c1i` instruction. No generated ELF is stored in source.
