# X4 0.1.34 USB SD preparation repair

This test cohort uses the 0.1.32 startup path. The unsuccessful 0.1.33 RTC-before-MSPI experiment is not selected. Battery-only startup is still unresolved.

USB Transfer 0.1.1 presents its preparing screen before the SD owner drains a finite snapshot of complete diagnostic lines. Each explicit step performs one checked append/close transaction. The next step handles sync/unmount before the MSC owner claims USB. New diagnostic lines remain buffered until checked eject and remount; local SD writes remain disabled during host custody. Short provider services now capture RAM only, avoiding the one-second service deadline interrupting a FatFs close.

The SD provider is 0.2.11 and the Reader MSC provider is 0.1.2. Existing app capabilities and USB PHY authority remain unchanged. The Springboard 1.7.17 ELF is preserved and separately bound to its original System source. The Transfer app uses its own exact source receipt; the builder rejects absent or changed tagged preparation policy.

Qualified source tests cover real SD/FatFs/MSC preparation, cancellation, finite log cutoff, late diagnostic tail, raw host read/write, checked eject/remount, stale tokens and retained media failures. Card timing and USB enumeration still require hardware testing. The reproduced deadline failure is software evidence; the user log did not supply exact card timing or the original SD error.

This is a complete first-install image for offset 0x0 and initializes internal settings and app data. It does not format the SD card. Logs remain x4-boot.log and x4-boot.previous.log at the card root. Pre-SD buffered data and the early NVS recovery summary have finite retention limits; ROM and power loss before firmware persistence cannot be reconstructed.
