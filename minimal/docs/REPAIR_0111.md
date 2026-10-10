X4 UC8279 fast driver and plain diagnostics, product0.1.11

This test image selects x4pro-uc8279-fast0.1.1. It uses the lab0.1.5
20MHz native-SPI absolute one-frame waveform and full-width40/80/160/480-row
windows. Normal visible full frames transfer48000bytes with no old-plane sync.
It keeps the800x600 controller geometry and120-row visible offset. Compact
geometry, TCON changes,40/80MHz experiments and PLL0x3F are excluded.

The first presentation after reset/resume establishes history with the original
OTP clean sequence. Subsequent ordinary scene changes use the fast profile.
Missing BUSY assertion or uncertain completion stops further presentations and
requires restart; no guessed refresh-rate success is reported. The laboratory's
reported9.97FPS is a comparison baseline, not a measurement of this integration.

Plain timestamped boot, provider and app load/init/entry/return statements print
automatically at115200. All16 apps were rebuilt with touch/action/draw/submit/
completion statements. Clock, Springboard and Settings additionally print actual
provider transfer/BUSY timestamps, byte count and effective native damage window.
No perf command or phase decoder is needed. RTE_LOG reports dropped/truncated
lines if the host cannot keep up. Capture the serial stream from reset through
one interaction; physical touch time is unknown, so touch timestamps identify
the first accepted software sample.

The native image also includes the X4-owned early GPIO1 HIGH hold and persistent
board keepalive, and fixes UNSET native time being blocked by invalid SDK wall
clock contents before external RTC recovery. Host tests reproduce that RTC
failure and verify recovery, but have not established the cause of the device's
intermittent first-boot report. Battery-only/RST behavior is also unverified on
hardware; the early hook runs after Arduino's initial clock/PSRAM/NVS work.

This checkpoint prioritizes the requested fast driver and readable diagnostics.
The newer Home typography/Points work and animated pull-down/crossfade are
qualified separately and are not included in this image. Existing16app scope
and controls remain. Serial Monitor and OTA/App Store are still omitted.

Flash this full16MiB image at0x0 only for the explicit new installation layout.
It replaces firmware, NVS and app-data. It is UC8279-specific. Preserve any data
you need before flashing. No device was flashed or physically qualified here.
