# Ordinary X4 dual frontlight

`x4pro-frontlight@0.1.6` preserves the `display.frontlight@1` prefix from the
source-preserving `Drivers/x4pro_frontlight@0.1.3` provider. The original source is
unchanged. This adapter is an ordinary ABI2 ELF using only scoped `platform.gpio`
and `platform.sync` plus one typed `hardware.device` record.

The `xteink,x4-pro-frontlight`, revision `unspecified`, `gpio.bank@1` record must
list GPIO8 (cool) and GPIO9 (warm), in that order, active-high, without pull-ups or
timing fields. At neutral tone both outputs use the same source-compatible rounded 10-bit level:
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

## Optional tone suffix

The size- and tag-checked `RiscFrontlightToneV1.h` suffix adds `set_tone` and
`get_tone`. Tone is an exact dimensionless warm/maximum ratio: zero is cool,
maximum is warm, and one half is neutral. Startup defaults to 1/2. The selected
ratio is returned unchanged and retained across ordinary level changes,
including OFF and restore. Successful quiescence resets the next lifecycle to
neutral. Invalid ratios, null/aliased output pointers, foreign owners, reentry,
unstarted/closing and retained state fail without reporting a fabricated value.

The existing rounded logical base level remains authoritative and is what
`get_level` returns. With base duty B, the cool channel is
`round(B * min(1, 2*(maximum-warm)/maximum))`; warm is
`round(B * min(1, 2*warm/maximum))`. Each channel is bounded by B; the favored
channel stays at B and only the opposite channel is reduced. At exactly 50%
both remain B, preserving the legacy neutral output. Reduced values may round
to zero at low base levels. Zero channels are safely driven LOW, held and
retired into CPU custody; full channels remain static HIGH. A tone selection
while the base level is OFF performs no GPIO I/O and stays dark. Partial-output
failures use the existing fail-dark and cleanup-only behavior.

GPIO8=cool/white, GPIO9=warm, active-high, 25 kHz and 10-bit configuration are
supported by the pinned FreeInk
[X4 hardware notes](https://github.com/Free-Ink/freeink-sdk/blob/111fdcc7f0176c3ee38391a160ee296bf492dbd8/docs/xteink-x4pro-support.md#L314-L332)
and its BoardConfig profile. This is source verification, not a new hardware
measurement. The mix does not establish constant or calibrated luminance,
Kelvin values, electrical current limits, thermal limits or measured flicker.
The Runtime PWM period remains the existing logical 1024, without importing
FreeInk's separate total-duty splitting or optional sleep-clock configuration.

## Checks

From the repository root, set `RISCRTE_RUNTIME_ROOT` and `RISCRTE_READER_ROOT` to
the matching Runtime and pinned Reader checkouts:

```sh
SANITIZE=1 bash minimal/test/run_frontlight_test.sh
SANITIZE=1 bash minimal/test/run_frontlight_tone_discovery_test.sh
SANITIZE=1 bash minimal/test/run_frontlight_cpu_port_test.sh
NATIVE_DRIVER_CC=/path/to/xtensa-esp32s3-elf-gcc \
  bash minimal/test/run_light_buttons_target_test.sh
```

The target check also requires a Python with `pyelftools` (`PYTHON` may select
it). Host checks cover malformed tables/configuration, endpoint and ratio
preservation, tone/base independence, off/restore, repeated endpoints,
size-bounded suffix discovery, owner-task/reentry admission, safe hold ordering, restart, and
cleanup/uncertain-failure retention. The ratio test enumerates all requests for
maximum 65535 and selected smaller maxima, plus every tone numerator through
65535 for base levels 1, 341, 1023 and 1024. The real CpuPort regression verifies
successful startup/off admission, same-scope LOW-staged reacquisition, and
failed-retirement handoff fencing with cleanup retry. Runtime's global safety
barrier is unchanged. Target checks validate ordinary ELF
exports/imports, relative relocations, and absence of PSRAM-unsafe `s32c1i`.
When the executor runs under ptrace, use `ASAN_OPTIONS=detect_leaks=0`; address and
undefined-behavior checks remain enabled, while LeakSanitizer is unavailable.
Physical/device verification remains pending.
