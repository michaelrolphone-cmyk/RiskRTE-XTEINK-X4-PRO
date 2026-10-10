# X4 0.1.61 rotated checkpoint correction

The user reports X4 0.1.60 takes approximately ten seconds to open Springboard and cannot reliably recognize horizontal paging or Quick Actions swipes. That build is not qualified for touch performance.

## Confirmed additional regression

The packed MONO1 fill added in 0.1.60 called raster_checkpoint after each physical row. X4's 90-degree rotation maps a one-logical-row replay band to one-pixel-wide physical rows. Replaying a full 480x800 clear therefore made 384,000 runtime health queries. The native health callback also obtains free heap, partition information, hardware MAC, and a target string; host mocks only assigned a timestamp. Context capture callbacks were similarly over-invoked in selected clients.

0.1.61 charges the checkpoint budget by pixels and services it at a 512-pixel boundary (or once per longer physical row), matching the prior pixel renderer's cadence. A full rotated screen now makes 750 health queries. The regression test counts calls and compares original pixels, so a fast desktop cannot hide this cost again. All portable app versions advance and are rebuilt. This correction does not change panel or touch providers.

## Remaining architectural findings

- frame_ready still rejects drawing while paper_token is pending. Software preparation and physical refresh are serialized. A future pipeline needs separately owned prepared and submitted frames and matching frame-generation metadata; changing this Boolean alone would misidentify displayed content.
- The recorder caps at 4,096 commands and synchronously materializes on capacity or allocation failure. A production Points catalog probe emitted 20,838 fill calls for its list, 14,639 for edit, 7,993 for time, and 13,835 for types. An instrumented selected adapter confirmed capacity fallback on those pages. Merely increasing this cap retains the excessive per-command memory cost.
- Raster checkpoints collect raw input; application input reduction waits for the draw call to return. The GT911 subscriber ring has 32 events; overflow clears the ring and cancels the uncertain gesture. Prolonged synchronous drawing can therefore lose complete swipes as well as delay them.
- The final offscreen copy and some history copies are outside the incremental replay budget. Injecting 2 ms per 4096 copied bytes produced 22 ms touch-to-model delay in the X4-sized copy test. This is a deterministic stress result, not a hardware timing measurement.
- The normal Points catalog renderer tests do not enable PORTABLE_RASTER_SNAPSHOT. Generic latency tests used small command lists and cheap health mocks. Exceeding the command cap with the existing deterministic pixel cost fails the input-dispatch bound.

The general correction remains compact semantic drawing commands, bounded preparation/raster/copy work, input servicing and ordered reduction between work slices, and independently owned display submissions. Idle panel settling must remain cancellable by fresh content. Hardware results for 0.1.61 are pending; do not describe the full decoupling goal as complete.
