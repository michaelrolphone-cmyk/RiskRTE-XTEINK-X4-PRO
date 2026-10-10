# X4 0.1.62 renderer pipeline correction

Hardware feedback for 0.1.61: best touch response since decoupling started, but software UI rendering still substantially slower than the pre-decoupling firmware. 0.1.62 retains the rotated checkpoint correction and targets remaining renderer work.

## Changed behavior

- Software recording and rasterization can run while the preceding immutable display token is active. The adapter retains at most one software frame ahead of that token. It does not acquire or modify the provider surface until completion. Logical input continues between bounded raster steps; newer model changes remain dirty until the producer slot is available.
- Producer readiness and full pipeline idleness are distinct. Completion-sensitive Points and File Browser bookkeeping uses the idle predicate; existing logical Springboard input keeps its current identity. Ownership boundaries still drain outstanding work and preserve terminal custody.
- Rotated rectangular fills traverse native display rows. A full clear uses contiguous packed spans instead of 384,000 scattered bit writes. Other primitives retain logical row clipping.
- Paper circles and contiguous glyph ink use spans, preserving exact coverage. The frozen 0.1.61 text oracle, rotations, flips, partial rectangles, and padding are compared as full buffers.
- Points records copied semantic text, rounded shapes, strokes and fades. Its six exercised dense scenes now record 32–124 commands (plus the adapter clear), versus 2,244–20,838 rectangle calls. This avoids the 4,096-command synchronous compatibility fallback on the tested production screens. Font pointers refer only to immutable tables; text, clipping and case decisions are copied.
- The final provider copy services raw input every eight rows. It still completes synchronously while the provider lease is held; that lease never escapes into application/service code.

## Validation and limits

Production adapter tests cover two simultaneous pipeline stages with a 2,300 ms simulated display delay, all 60 touch cycles and 60 navigation press/release cycles, unchanged submitted pixels, bounded queue depth, and logical Springboard drag/tap processing before display completion. Dense Points screens compare immediate and sliced output, including mutation of clipping/case state after recording, in normal and ASan/UBSan builds. Terminal retention, allocation failure compatibility, clipped starts, text handoffs and Quick Actions restore tests remain required.

The circle oracle replaces 552,056 individual pixel calls with 4,376 spans across its test cases. This is an operation count, not a hardware FPS claim. Deterministic slow-CPU stress can take seconds to render while input still reaches the model in milliseconds; desktop elapsed times do not predict X4 frame latency.

Native firmware, panel and touch providers remain at the established baseline. All twenty portable applications are rebuilt and versioned. The emergency allocation/capacity fallback still preserves complete pixels synchronously; arbitrary callers that exceed recorder capacity are not promised asynchronous rendering. Hardware performance for 0.1.62 is not yet verified.
