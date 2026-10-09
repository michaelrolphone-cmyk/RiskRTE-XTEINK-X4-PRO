# X4 0.1.36 USB SD preparation

This cohort combines the observed battery-working 0.1.35 startup with the qualified 0.1.34 USB preparation repair. GPIO1 startup code, bootloader and power behavior are unchanged from 0.1.35. The earlier 0.1.33 RTC-order experiment remains excluded. The successful Reset-then-hold-Power procedure matches the official X4 Pro manual; it does not establish that the GPIO experiment caused the success.

USB Transfer 0.1.1 renders Preparing SD before its explicit preparation steps. Each step closes one checked log append. A finite snapshot prevents newly generated diagnostics from extending preparation indefinitely. Only after sync/unmount succeeds does the USB provider claim the PHY and expose the card to the host. Local SD writes stay blocked until checked eject and remount. Internal capture continues; late diagnostics drain afterward. Genuine media/close failures retain custody and report their original cause.

SD 0.2.11 and MSC provider 0.1.2 are selected. All other app/provider ELF bytes, including Home and Springboard, remain unchanged from 0.1.35. The Springboard entry remains USB Transfer on page 2; Home Quick Actions shell consolidation is separate queued work.

Normal and sanitizer production SD/FatFs/TinyUSB tests cover preparation, cancel, retained errors, read/write, eject and remount. Physical USB enumeration and card behavior remain untested at packaging time.

This complete first-install image is flashed at offset 0x0 and resets internal settings and app data. It does not format the SD card. The card-root files remain x4-boot.log and x4-boot.previous.log.
