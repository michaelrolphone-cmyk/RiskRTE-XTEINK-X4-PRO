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
