# Ordinary X4 dual frontlight

`x4pro-frontlight@0.1.5` preserves the `display.frontlight@1` interface from the
source-preserving `Drivers/x4pro_frontlight@0.1.3` provider. The original source is
unchanged. This adapter is an ordinary ABI2 ELF using only scoped `platform.gpio`
and `platform.sync` plus one typed `hardware.device` record.

The `xteink,x4-pro-frontlight`, revision `unspecified`, `gpio.bank@1` record must
list GPIO8 (cool) and GPIO9 (warm), in that order, active-high, without pull-ups or
timing fields. Both outputs use the same source-compatible rounded 10-bit level:
zero is exact static LOW; maximum is exact static HIGH; intermediate values use
25 kHz PWM with a logical maximum of 1024. Nonzero sub-count requests remain at
least one count and only the requested maximum produces full HIGH. The Runtime
native PWM implementation must preserve this logical ratio at its physical
10-bit resolution; scaling by 1023 would incorrectly extinguish the first count.

A fresh claim stages LOW before enabling output or releasing an existing boot
hold. Only successfully established static LOW outputs are held. Moving from
held LOW to an illuminated level takes a fresh same-scope claim, which stages
LOW before releasing the CPU-held pad. Turning off never unholds a pad. I/O
failure attempts to darken both outputs and fences normal client operations
until cleanup; get-level never fabricates success.

The adapter size-checks the appended `retire_held_output` GPIO function. During
startup, normal zero-brightness calls, and quiescence, it transfers successfully
held LOW pads to CPU boot custody without unholding or releasing them. Idle
frontlight holds therefore do not fence Runtime provider storage or app handoff.
Failed retirement, safe writes, hold setup, or sync destruction retain their
exact resources for cleanup retry. Uncertain native claim, hold, or
unlock failures pin the provider and its dependencies. Stop performs no fallible
work after accepted quiescence. There are no MMIO, FreeRTOS, or provider-BSS atomic
imports.

## Checks

From the repository root, set `RISCRTE_RUNTIME_ROOT` and `RISCRTE_READER_ROOT` to
the matching Runtime and pinned Reader checkouts:

```sh
SANITIZE=1 bash minimal/test/run_frontlight_test.sh
SANITIZE=1 bash minimal/test/run_frontlight_cpu_port_test.sh
NATIVE_DRIVER_CC=/path/to/xtensa-esp32s3-elf-gcc \
  bash minimal/test/run_light_buttons_target_test.sh
```

The target check also requires a Python with `pyelftools` (`PYTHON` may select
it). Host checks cover malformed tables/configuration, endpoint and ratio
preservation, owner-task/reentry admission, safe hold ordering, restart, and
cleanup/uncertain-failure retention. The ratio test enumerates all requests for
maximum 65535 and selected smaller maxima. The real CpuPort regression verifies
successful startup/off admission, same-scope LOW-staged reacquisition, and
failed-retirement handoff fencing with cleanup retry. Runtime's global safety
barrier is unchanged. Target checks validate ordinary ELF
exports/imports, relative relocations, and absence of PSRAM-unsafe `s32c1i`.
When the executor runs under ptrace, use `ASAN_OPTIONS=detect_leaks=0`; address and
undefined-behavior checks remain enabled, while LeakSanitizer is unavailable.
Physical/device verification remains pending.
