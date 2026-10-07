# X4 ordinary-provider port for the minimal Runtime

This directory is the in-progress adaptation from the source-preserving Reader
migration to the shared headless RiscRTE architecture used by Watch. The original
`Drivers/`, board and Reader composition remain unchanged while each adapter is
connected and tested.

## Board power

`x4pro-board-power@0.1.0` owns the GPIO1 peripheral-enable rail through the
Runtime's existing device-scoped `platform.gpio@1`. Its typed `gpio.bank` record
contains only GPIO1, active-high, without pull-up or timing reinterpretation.
The provider asserts the output latch high before enabling the pad, verifies
readback, and publishes `board.power.ready@1` for dependency-ordered consumers.
It performs no MMIO or privileged imports. Failed release retains ownership;
uncertain failed claims keep the provider pinned instead of fabricating cleanup.

The candidate will need explicit board, driver and application selections before
it can replace a Reader-based deployment. This provider alone is not a complete
boot bundle or a hardware qualification. Deep-sleep policy and first-install
layout remain integration work.

## Checks

```sh
RISCRTE_RUNTIME_ROOT=../RiscRTE SANITIZE=1 bash minimal/test/run_board_power_test.sh
```

The four host scenarios cover malformed dependencies/configuration, normal and
repeated lifecycle, readback failure, cleanup retry and retained claim failure.
AddressSanitizer/UBSan and an Xtensa ordinary-provider link were checked. The ELF
exports only `t5_driver_get`; its only libc import is `strcmp`.

## Typed I2C adapter

`x4pro-i2c@0.1.5` preserves the API1 prefix and tagged serialized/deadline/retained-release suffix. It uses the ordinary `platform.i2c.controller`, `platform.clock` and `platform.sync` tables plus explicit `board.power.ready` dependency. The source migration's legacy0.1.4 provider remains unchanged outside this directory.

One owner-task, nonrecursive attempt admits each transaction. Admission time is deducted from the physical transfer budget, and an expired completion never returns success. Address claims are exclusive and generation-safe; failed release retains the exact claim. Failed controller close or sync cleanup retains the dependency state. There are eight claim slots,256 bytes per transfer phase and a maximum1000ms total request budget. No FreeRTOS imports or provider-BSS atomics are used.

```sh
RISCRTE_RUNTIME_ROOT=../RiscRTE RISCRTE_READER_ROOT=../T5S3-Reader \
  SANITIZE=1 bash minimal/test/run_i2c_test.sh
```

The Runtime and Reader SDKs share byte-identical common headers. `minimal/scripts/prepare_sdk.py` composes one include directory and rejects any overlapping filename with divergent bytes, preventing duplicate pragma-once type definitions or silent ABI drift. Generated SDK copies stay in build scratch; shared headers remain maintained upstream.
