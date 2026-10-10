# X4 Minimal 0.1.10 diagnostic checkpoint

This additional UC8279 test image instruments the existing 0.1.9 application
cohort. Clock, Springboard and Settings are rebuilt with shared touch/gesture,
controller, drawing, presentation and display-provider metrics. The other thirteen
apps remain the exact 0.1.9 app packages. Runtime and provider/loader tracing cover
all application launches. This is not a claim that the outstanding visual,
transition, battery-only boot or hardware latency issues are resolved.

Use USB serial at 115200. After boot, send the exact command `perf` followed by
Enter and save the complete `RTE_PERF begin` through `RTE_PERF end` response.
Then perform one operation and request another snapshot: an app-icon tap, a
Clock-to-Springboard swipe, a Quick Controls swipe, or Settings timezone Next.
Keep the whole response, including counts, lost records and overhead fields.
A dump is bounded and nonblocking; it may take several seconds. It does not run
in the recording call or wait for a USB host to be present.

First touch means the first accepted software sample, not the physical finger
contact time. Interaction IDs continue through an app launch. Each paired span
is inclusive: nested durations must not be added as exclusive time. An incomplete
or superseded interaction has no proved completion duration. The provider reports
submitted/effective rectangles, waveform mode, bytes, GPIO calls and actual
transfer/refresh/BUSY timestamps. Missing timestamps are omitted, never treated
as zero-duration work. Existing SPARSE_CLOCK elapsed_ms messages are cumulative;
the new spans separate promotion, configuration, logo and RTC recovery.

Qualified controller changes allow input while a frame is pending, retain the
latest dirty state, preserve the original 1 ms provider-pump cadence, and display
selected-icon feedback before launch. Identical timezone boundary actions avoid
redundant raster work. These are host/target-qualified behavior changes, not
measured hardware latency improvements. Panel transfers remain 120,000 bytes for
both full and tiny partial updates; the diagnostics make that cost observable.

The requested corrected Home typography and swipe transitions continue separately;
this diagnostic checkpoint preserves the prior visuals to get actionable device
measurements promptly. No device was flashed by the build task. This is a full
16 MiB image at 0x0 and replaces firmware, NVS and app-data, not a preserving OTA.
