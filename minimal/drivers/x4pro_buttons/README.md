# Ordinary X4 physical buttons

`x4pro-buttons@0.1.6` preserves source `0.1.5` navigation behavior and the
`input.navigation@1` API, including the physical-page-pair trait. The original
`Drivers/x4pro_buttons` source is unchanged. This ordinary ABI2 adapter requires
scoped `platform.gpio`, `platform.sync`, and a typed `hardware.device`.

The `xteink,x4-pro-buttons`, revision `unspecified`, `gpio.bank@1` record lists
GPIO0 LEFT, GPIO7 RIGHT, and GPIO3 CONFIRM, in that exact order. All are active-low
inputs with pull-ups. Timing fields must be zero: the original algorithm uses
poll counts rather than a time-based debounce. A changed sample begins at count
zero, and three further unchanged samples accept its press/release edges. Reset
discards previous navigation history. If a button is held at reset/start, every
output remains neutral until three consecutive neutral samples rearm input.
Foreground claims remain a no-op for this nonoverlapping physical source.

The GPIO and sync tables control ownership without firmware UI, MMIO, RTOS
imports, or provider-BSS atomics. Calls reject reentry/non-owner access. Failed
reads publish no partial frame and advance no debounce state. Cleanup retries
retain the exact failed GPIO claims or sync token. Uncertain native claim or
unlock failure pins the provider/dependencies, and accepted quiescence leaves
stop with no fallible cleanup.

## Checks

Set `RISCRTE_RUNTIME_ROOT` and `RISCRTE_READER_ROOT` to the matching Runtime and
pinned Reader checkouts, then run:

```sh
SANITIZE=1 bash minimal/test/run_buttons_test.sh
NATIVE_DRIVER_CC=/path/to/xtensa-esp32s3-elf-gcc \
  bash minimal/test/run_light_buttons_target_test.sh
```

The target check requires Python with `pyelftools` (`PYTHON` may select it). Host
checks cover exact individual/chord mapping and edge counts, held-start/reset
neutral rearming, 20,000 source-equivalence frames, read failure, owner/reentry
admission, cleanup retry, and retained failures. Target checks validate ordinary
ELF exports/imports, relative relocations, and absence of `s32c1i`. Under a ptrace
executor, `ASAN_OPTIONS=detect_leaks=0` disables the unsupported LeakSanitizer
without disabling address/undefined-behavior checks. No hardware was accessed;
physical verification remains pending.
