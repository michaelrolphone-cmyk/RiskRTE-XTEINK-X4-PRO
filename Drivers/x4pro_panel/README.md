# X4 panel clock/sleep adaptation

Version 0.1.14 keeps the original `display.output@1` prefix. Shared Reader
`DeskClockSleep` and `DeskClockFaces` remain the clock engine; the six faces,
retained minute, 30-minute full refresh and timer/button behavior match the
healthy `firmware-v1.3.53` reference. This ELF implements only panel-specific I/O.

An optional tagged history suffix copies a caller-owned acquired frame into a
bounded previous-image buffer before normal submit. The clock reconstructs its
previous minute using the existing renderer. The provider uses old/new pixels
and byte-aligned clipped damage, with UC8279 gate offset 120 or descending
SSD1677 gates. Unsupported history falls back to the ordinary full-frame API;
old providers lacking the explicit quiescent-sleep information flag cannot
satisfy the new X4 sleep barrier. No controller RAM is assumed valid after reset.

The sequences reuse pinned FreeInk `111fdcc7f0176c3ee38391a160ee296bf492dbd8`:
- [UC8279 refresh/power](https://github.com/Free-Ink/freeink-sdk/blob/111fdcc7f0176c3ee38391a160ee296bf492dbd8/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.cpp):
  full CDI97/TSSET1E; clipped differential CDID7/TSSET5A with PTIN/PTL,
  post-PON PSR17/4D, DRF and PTOUT. Power-off02 waits idle before DSLP07/A5.
- [SSD1677](https://github.com/Free-Ink/freeink-sdk/blob/111fdcc7f0176c3ee38391a160ee296bf492dbd8/libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.cpp):
  full bypass-red/control F7; differential old-red/current-BW control FC;
  power-off border80/control03/activate20 with 200 ms + bounded BUSY wait,
  then deep-sleep10/03.
- RESET GPIO14 is held HIGH because X4 has no switched panel rail. Startup
  releases only its own hold. Touch and SD pins/rails are never touched here.

Quiescence refuses held/queued/active frames and busy physical transfers.
Power-off progress is retained, so a failed POF wait retries observation instead
of blindly sending another POF/DSLP. Acceptance means physical transition is
finished; final stop makes no new hardware calls. Transfer loops bound work and
time with real scheduler cooperation. The actual-driver pin model checks full
and differential register bytes, distinct old/new planes, clipped coordinates,
POF timeout/retry, single DSLP and RESET hold. Host models/CI are not physical
current, waveform-quality or wake validation.
