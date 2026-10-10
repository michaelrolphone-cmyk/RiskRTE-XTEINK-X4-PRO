# UC8279 transfer cadence and diagnostic boundary

The real UC8279 provider transmits two 60,000-byte planes for both full and partial
refreshes: 800 × 600 controller RAM, including 120 white rows before the visible
800 × 480 image. Each software-SPI payload byte calls scoped GPIO write 24 times.
The payload alone therefore costs 2,880,000 calls. The tested full frame uses
2,880,357 calls including commands/sidebands; a 16 × 5 partial region uses
2,880,789. Partial waveform/window selection does not reduce RAM transfer volume.

`GraphV2::poll` supplies at most 8 ms per provider within its 10 ms graph budget
and four-call fairness cap. The panel checks time every eight bytes and caps each
poll at 512 bytes. At zero modeled GPIO time, 120,000 bytes require 235 payload
polls. `Runtime::yield` polls once and then requests the app's clamped 1–50 ms
scheduler delay. A caller that sleeps 20 ms for every chunk can add roughly
4.7 seconds; 50 ms can exceed the existing ten-second presentation deadline.

That is a potential regression when making presentation asynchronous, not the
established cause of the owner's earlier 10–15 second page report. The immutable
System baseline `c1ff014792c9ecbc195c402e9b4c3e2c3c9d34d3` already requests 1 ms
inside blocking `present()`. It samples touch there, but cannot return the input
to the app controller until the display completes. System change
`4b72a54e18c114e23774d8579675efd6048de41e` releases that controller block while
maintaining the same 1 ms provider cadence inside the caller's remaining wait.
Its independent iteration bound also handles a stalled clock.

## Production fixture

`minimal/test/run_panel_cadence_test.py` compiles the supplied, unmodified System
adapter with its existing input fixtures, the actual Runtime/Graph/Module loader,
and the complete production X4 provider over its existing scoped-GPIO model.
A mapped app executes the adapter; a mapped provider forwards the actual graph
poll callback into the panel. No scheduler, transfer loop or adapter cadence
algorithm is copied into the fixture. GPIO and BUSY time remain explicit model
inputs, not physical hardware measurements. The fixture checks the optional
presentation snapshot against independently counted GPIO activity.

The model uses 3 ms PON/BUSY phases and three GPIO costs: zero, 1 µs/write and
2 µs/write. Baseline/current runs cover caller intervals 1, 8, 20 and 50 ms, each
with a full frame followed by a small partial frame. All matching runs assert
identical GPIO wire hashes, bytes, geometry, waveform mode, payload-poll count
and transfer duration. Each frame still uses 1 ms scheduler requests. An idle
controller retains one original unsplit wait. The current controller's maximum
gap is bounded by the requested interval plus one 8 ms provider slice.

| Modeled GPIO cost | Payload polls | Transfer phase | Baseline full completion | Current full completion, 20 ms caller | Controller returns during frame, baseline/current |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zero | 235 | 234 ms | 241 ms | 241 ms | 0 / 13 |
| 1 µs/write | 361 | 3,240 ms | 3,248 ms | 3,262 ms | 0 / 122 |
| 2 µs/write | 721 | 6,480 ms | 6,488 ms | 6,502 ms | 0 / 242 |

With 1 µs/write, maximum current controller gaps for requested 1/8/20/50 ms are
9/9/27/54 ms. The old adapter already samples touch about every 18 ms in this
case, but still returns to the controller only after the full multi-second
presentation. Small partial updates have the same transport cost. Actual GPIO
cost, controller waveform time and paper-visible timing require device traces.

Reproduce with clean pinned checkouts:

```sh
python3 minimal/test/run_panel_cadence_test.py \
  --runtime /path/to/runtime-0.1.55-or-0.1.56 \
  --reader /path/to/x4-desk-clock-sdk \
  --system /path/to/system-at-4b72a54 \
  --baseline-system /path/to/system-at-c1ff014 \
  --output build/panel-cadence
```

Add `--sanitize` for ASan/UBSan. Leak inspection is disabled under this executor;
the fixture still runs both memory/undefined-behavior instrumentations. Receipts
contain actual observations and source SHA-256 values. This is test-only work;
it does not introduce Runtime scheduling or GPIO-scope changes.

## Next diagnostic image

Panel 0.1.22 adds the tagged `RiscDisplayOutputMetricsV1.h` suffix, preserving the
exact existing power/history prefixes. It exposes actual per-token queued,
transfer, DRF and BUSY times plus write/byte/geometry counts to a diagnostic app.
The getter performs no hardware or clock I/O and advances nothing. UC DRF time
is explicitly separate from PON. Millisecond precision and cooperative polling
bound the observations; BUSY-done is not a paper-visible timestamp. GPIO-call
counting itself adds instructions whose device cost has not been measured.

Native SPI remains a separate change. Existing `spi.bus` would avoid millions
of scoped per-edge calls, but the current display GPIO scope intentionally
withholds SCLK/MOSI/CS probe access when native SPI is selected. A future change
must explicitly qualify exclusive bidirectional probe, pin release, native claim
handoff, transfer failure and cleanup. No scope relaxation or guessed UC8279
window/command sequence is included here.
