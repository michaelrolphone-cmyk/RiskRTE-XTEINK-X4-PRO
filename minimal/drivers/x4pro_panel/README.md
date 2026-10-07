# X4 panel ordinary-provider adapter

`x4pro-panel@0.1.20` ports the source-preserved panel implementation to ordinary
`hardware.device`, device-scoped `platform.gpio`, `platform.clock`,
`platform.sync` and `board.power.ready` dependencies. It imports no privileged
CPU entry points and performs no MMIO. The original `Drivers/x4pro_panel` is
unchanged.

UC8279 now advertises asynchronous presentation and uses the ordinary provider
poll suffix. Each owner-task callback sends at most 512 bytes, checks time every
eight bytes, and respects the supplied maximum eight-millisecond scheduling
budget. BUSY and power-on waits return to the Runtime instead of sleeping inside
the frame transfer. A fixed ten-second operation deadline still fails closed.
The existing synchronous wait entry remains compatible; SSD1677 retains its
original path. No RTOS task, interrupt, or privileged import is added. Full and
partial UC frames emit the same GPIO-edge stream as the synchronous reference.
This does not change the controller waveform or claim a measured hardware speed.

## Explicit controller selection

Two `display.spi@1` configurations are admitted. Both use native 800×480 MONO1,
MSB-first black bits and a 100-byte stride, GPIO12 SCLK, GPIO11 bidirectional MOSI,
GPIO13 CS, GPIO18 DC, GPIO14 active-low RESET and GPIO6 BUSY. No backlight,
panel-power, touch or SD pad is claimed.

- `solomon-systech,ssd1677`: BUSY active-high, offsets 0/0, reset assertion and
  recovery 10/10 ms.
- `ultrachip,uc8279`: BUSY active-low, offsets 0/120, reset assertion and recovery
  50/50 ms. Controller RAM remains 800×600; the first 120 gates are white.

Revision is `unspecified`. The separate preserved probe reset is 1/30 ms.
UC requires stable repeated flag/version reads, version byte 0x68 and idle BUSY.
Floating reads retain the original SSD-assumed verdict, but cannot select an SSD
record automatically. A probe inconsistent with the explicitly selected record
fails closed. The recovered sources support both variants and do not establish
which is installed in a particular device; no default silicon is guessed.

The SPI record describes the wiring and protocol. These transfers remain
GPIO-driven: no hardware SPI frequency is programmed and no measured timing or
waveform-quality qualification is implied by its frequency metadata.

## Ownership, deadlines and sleep

One nonrecursive Runtime guard confines mutable state to the owner task.
Acquired frames, queued work and active transfers prevent quiescence. Damaged
updates require an explicit bounded copy of the previous image for that frame;
otherwise full refresh is used. Source plane inversion, SSD descending gate
windows, UC 120-gate offset, full/differential waveform registers, PON ordering,
POF observation retries and one-shot DSLP are preserved.

Transfer work yields cooperatively and is checked against the total wait
budget, including admission time. GPIO errors or uncertain claims retain the
provider instead of reporting safe cleanup. Failed POF only retries observation.
Failed ordinary GPIO release retains its exact token for cleanup retry.

RESET must remain HIGH under a hardware hold after controller sleep. Ordinary
Garden `release` correctly refuses held tokens. The adapter optionally uses the
append-only `retire_held_output` suffix to transfer this held static output to
Runtime custody before releasing the other pins. Without that suffix it stays
pinned and quiescence returns false. Failed retirement, release or lock cleanup
never fabricates successful unload, and retries do not repeat POF/DSLP.

Runtime must also authorize the exclusive GPIO-driven SPI signal pins for this
record. The older `display.spi` scope exposes only sideband pads and therefore
cannot start this adapter. The scoped GPIO model tests that denial as a retained
failure; it does not replace Runtime authority checks.

## Verification

```sh
ASAN_OPTIONS=detect_leaks=0 \
RISCRTE_RUNTIME_ROOT=/path/to/RiscRTE \
RISCRTE_READER_ROOT=/path/to/pinned/Reader \
SANITIZE=1 bash minimal/test/run_panel_test.sh
```

The GPIO-token model executes the complete provider and actual command-bit
stream, including real bidirectional probe data. It checks both controller
variants, frame/previous-image ownership, alignment, old/new pixels, command
bytes, timeouts, invalid config/dependencies, owner/reentrancy, scope denial,
GPIO failures, and retained shutdown. Retirement-specific success/retry cases
run only when the canonical SDK contains that suffix. Leak detection may need
to be disabled under a ptrace-based executor; AddressSanitizer and UBSan stay on.

Target validation is reproducible with `minimal/test/run_panel_target_test.sh`
using the same two checkout variables plus `NATIVE_DRIVER_CC` and a `PYTHON`
with pyelftools installed. It uses the shared Reader relocation normalizer and
relative pointer checker. Build with `-O2 -fno-ivopts -fPIC -mtext-section-literals
-mlongcalls -fvisibility=hidden -fno-builtin -nostdlib -nostartfiles -shared`,
`--hash-style=sysv --exclude-libs,ALL --no-relax`, and `-lgcc`. The ordinary ELF
exports only `t5_driver_get` and imports only `strcmp` and `memset`.

No device was accessed, flashed, probed or physically qualified by this port.

The display brightness method delegates to its explicitly bound
`display.frontlight@1` provider; it never claims that provider's pins. Failed
frontlight writes propagate failure. Unused panel-power ABI slots are canonical
zeroes from Runtime's materializer, not handwritten -1 sentinels.
The display-info brightness flag is advertised only with the bound frontlight
method present, so capability-selected controls can truthfully enable its slider.

## Reversible panel preparation (0.1.20)

The canonical optional `RiscDisplayOutputPowerV1.h` SDK suffix follows the exact
existing history prefix in `display.output@1`. Discover it with
`risc_display_output_power(display)`, then call `prepare(context, timeout_ms)`
or `resume(context, timeout_ms)` on the serialized owner. This does not export a
second capability and does not change renderer or Watch ABI prefixes.

Both methods cap total admission/readiness budget at 1500 ms. Zero is a
non-mutating state poll. Readiness has an independent 150-iteration bound with
real ten-ms sleeps, including under a stalled clock. A command finishes before
checking its deadline; the maximum unchecked command group is six bytes
(162 GPIO writes), so the deadline is cooperative rather than hard real-time.
Reset assertion/recovery use 10/10 ms for SSD1677 and 50/50 ms for UC8279.
POF settling is 200 ms for SSD1677 and at least 1 ms for UC8279. The six owned
pins, synchronization guard, mapped provider and dependencies remain live.

`prepare` refuses acquired/queued/active frames. It observes POF, sends DSLP
once, and holds RESET high without retiring or releasing anything. Retries only
observe incomplete POF or retry a known failed hold; they do not replay POF or
DSLP. Partial or complete preparation rejects frame acquisition, seeding,
submission, presentation status/wait, brightness and polling. `get_info` remains
an informational query. Preparation OK means only that this panel is prepared;
it is not evidence that the CPU entered sleep or other devices are ready.

Any ordinary error may require `resume` before use. Resume unholds only the
instance's owned RESET, repeats the controller's reset/register initialization
without probe, frame clear, PON or visible refresh, and invalidates prior image
seeding and old presentation tokens. RAM preparation on SSD does not refresh
the physical paper. Resume is idempotent; a timed-out resume remains blocked
until a later resume succeeds. No claim/release occurs on this reversible path.
A failed GPIO operation, hold rollback, unhold or mutex unlock returns sticky
`RETAINED`: retain the complete provider graph rather than assert safe recovery.
Clock/known hold refusal returns `PLATFORM`; elapsed bounds return `TIMEOUT`.

Terminal quiesce after successful preparation uses the existing explicit
RESET-retirement suffix and releases other pins without another POF/DSLP.
If resume was interrupted, terminal quiesce refuses until resume succeeds.
Retirement/release refusal keeps its exact outstanding ownership for retry;
a retired instance cannot be resumed.

A real MCU deep-sleep wake is a fresh boot: none of these C globals, addresses,
frame IDs or tokens survives. Runtime must restore its separately versioned
retained transaction record, reload/rebind the provider, stage RESET high before
unholding, and let normal start probe/init run without a visible refresh. The
clock application must reconstruct the previous visible image into an acquired
frame, seed it with `seed_previous`, then draw and submit the current image. If
previous pixels cannot be reconstructed truthfully, use an ordinary full clean
refresh. The provider does not persist framebuffer history or invent old pixels.

This slice excludes the desk-clock application transaction, Runtime retained
records, CPU sleep entry, touch/SD/rail lifecycle and hardware qualification.
The existing frozen X4 0.1.6 firmware and raw `Drivers/` custody are unchanged.
