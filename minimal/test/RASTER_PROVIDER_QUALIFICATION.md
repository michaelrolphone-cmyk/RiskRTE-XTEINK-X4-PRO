# Opt-in snapshot owner-task/provider qualification

These are host-only test adaptations. No production provider or System/Runtime
source is modified by this test commit. The snapshot adapter is selected only
when the compiler defines `PORTABLE_RASTER_SNAPSHOT`.

## Test changes

- Wait for either `raster_sealed` or `paper_token` to clear.
- Observe software-phase Runtime yields/touch samples separately from provider
  BUSY yields/touch samples, and report application-loop return opportunities.
- Wrap provider frame acquisition, release and submission. Normal replay must
  finish its command cursor before its sole acquisition, then submit once; no
  writable provider lease crosses a measured Runtime yield or app-loop return.
- Stage the supplied Runtime's canonical `RiscKeyValueV1.h` along with Runtime
  and realtime headers for its current SDK contract.
- Supply the missing provider-pending test query to the fast-provider bridge.
- Keep the provider-pending scheduler request at exactly 1 ms. Software replay
  and idle scheduler requests may use the adapter's existing 4 ms cap. Preserve
  the original provider byte/slice budgets and resident settling assertions.

The fast provider remains `x4pro-uc8279-fast` version 0.1.13, with native SPI.
The normal UC branch of `x4pro-panel` is a distinct provider and is reported as
an additional regression; it is never identified as fast.13.

## Reproduction

Use a compiler wrapper containing `exec cc -DPORTABLE_RASTER_SNAPSHOT "$@"`, then:

    CC=/path/to/cc-raster python3 minimal/test/run_raster_provider_qualification.py \
      --runtime /path/to/runtime --reader /path/to/reader \
      --system /path/to/system --output /path/to/new-evidence

The runner tests normal and ASan/UBSan builds, using the existing fixtures:

- SSD1677: refresh latency 0, 17 and 2300 ms, each at waits 1/8/20/50 ms and
  GPIO costs 0/1000/500 writes per simulated ms (72 scenarios total).
- Ordinary UC8279: 17 ms at the same 12 combinations (24 scenarios).
- Actual fast.13: its existing 20 MHz native-SPI / 20 ms BUSY model, at all four
  waits, both default and PAPER_TRANSITIONS modes (16 scenarios). This also
  exercises resident settling and 30 seconds of quiet maintenance per scenario.

## Limits and interpretation

Timing is deterministic model time. The ordinary provider charges modeled GPIO
writes; the native-SPI provider charges payload transmission. Raster CPU,
allocator, SDK execution, interrupts and hardware delays are not measured.
No device, target artifact, product composition, flashing or deployment is
qualified by these tests. The workload is a paper text frame and one changed
circle, not every application's state machine or every renderer primitive.

A `software_phase_entry_steps` count includes a call that enters with software
work and returns after starting the provider. `software_pending_model_returns`
counts only returns with software still pending. At long requested waits the
latter can be zero. Likewise a fast partial BUSY pulse can complete inside one
50 ms input wait, yielding zero `provider_pending_model_returns` while Runtime
polls and touch sampling still progress. These are distinct observations.

The initial fast baseline incorrectly required at least one BUSY-pending
application-loop return for every frame; that fixture-only assumption failed
for this legitimate 50 ms case. The final assertion requires BUSY Runtime
yields and touch samples while reporting loop-return counts without a false
positive requirement. All prior Runtime/provider budget checks are retained,
with software/idle 4 ms waits separated from provider-pending 1 ms waits.

This normal offscreen path does not exercise allocation fallback, cancellation,
retained-custody failure, or launch-transition boundaries. Separate System
fixtures must qualify those behaviors. Sanitizers run with leak detection
disabled, as in the source fixture, and undefined-behavior recovery disabled.
