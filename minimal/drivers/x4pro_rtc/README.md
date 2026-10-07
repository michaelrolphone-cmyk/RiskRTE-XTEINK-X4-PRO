# Ordinary X4 RTC provider

`x4pro-rtc@0.1.1` adapts Reader source commit
`34d8e694d89a1e72d8854403d8592c289fae3ddc` to immutable `peripheral.i2c@1`
configuration and the safe `i2c.bus@1` claim/release suffix. It requires the
explicit `riscrte,pcf8563-compatible-rtc` protocol identity, address0x51 and no
IRQ. The shared `Drivers/common/pcf8563_rtc_ops.h` remains external. No OS/MMIO
imports or startup writes are introduced. STOP/VL invalid reads leave caller
output untouched; only an explicit valid time write may restart the RTC.

Run `minimal/test/run_rtc_test.sh` with `RISCRTE_RUNTIME_ROOT` and
`RISCRTE_READER_ROOT`, optionally `SANITIZE=1`. Host and target builds do not
qualify battery-backed physical operation or a specific unidentified RTC chip.
