# X4 Pro read-only battery gauge

`board.battery@1` reads the resident CW2017 at `0x63` through the installed
`i2c.bus@1`. It accepts only the tagged API-1 safety contract (serialization, total deadline
and retained release) and exactly the single manifest dependency. Missing,
short, mistagged or incompatible contracts are rejected before any I/O. Startup and every sample require running VERSION
`0x0d`/`0x0f`, normal CONFIG, 14-bit VCELL that converts to nonzero millivolts,
and integer SOC in 0–100. VERSION `0xa0` is
the power-on/startup value and is rejected as not ready.
Failed reads do not modify the caller's sample. These checks do not establish
that the resident battery profile is correct or calibrate its SOC estimate.

No reset, wake, BATINFO/profile or other gauge-register writes are performed.
A gauge still sleeping/resetting fails closed; this provider does not initialize
an absent profile. GPIO21 is configured input/no-pull after startup validation
and read active-high. The boolean reports charging only. It does not establish
VBUS/USB presence, charge-full, battery health, temperature or current.

## Sources and evidence limits

- [Cellwise CW2017-DS v1.3](https://www.cellwise-semi.com/Public/assests/menu/20230216/63ede1073bafc.pdf),
  register table and CONFIG/VCELL/SOC descriptions. The manufacturer-hosted PDF
  could not be fetched during this repair; the manufacturer-authored datasheet
  [distributor copy](https://uploadcdn.oneyac.com/upload/document/1737612108523_8373.pdf)
  provides the default VERSION `0xa0`, 14-bit VCELL with 312.5 µV per count,
  and SOC register definitions. The [Cellwise datasheet copy](https://uploadcdn.oneyac.com/attachments/files/brand_pdf/cellwise/1D/14/Cellwise-CW2017.pdf)
  describes CONFIG `0xf0` at power-up and `0x00` normal mode. Reserved CONFIG
  low bits must be zero. Nonzero CONFIG is rejected without changing the chip.
- [FreeInk battery implementation](https://github.com/Free-Ink/freeink-sdk/blob/main/libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp)
  distinguishes power-on VERSION `0xa0` from running versions `0x0d` and `0x0f`
  with `(version & 0xfd) == 0x0d`; it rechecks running VERSION and normal mode
  before a sample. This readiness rule is based on the X4 Pro OEM-derived
  implementation, not on treating the datasheet default as a fixed chip ID.
- [FreeInk X4 Pro hardware notes](https://github.com/Free-Ink/freeink-sdk/blob/main/docs/xteink-x4pro-support.md#rtc--usb--battery)
  identify GPIO21 input/no-pull and active-high charging from the recovered OEM
  `Cw2017PowerHal` getter and board initialization. These are primary
  reverse-engineering notes; this repair has not physically verified that
  polarity. The same notes say VBUS detection is not conclusively identified.

## Lifecycle and boundedness

Repeated start cannot overwrite a live or failed-release bus/token. Failed
startup releases its token once when possible. `quiesce()` and `stop()` use the
same checked release; failure prevents further reads but retains ownership so a
later cleanup attempt can recover. Successful cleanup is idempotent.

Startup and read each perform at most four synchronous transactions. Each has
one register-address byte and at most two returned bytes, with a requested
20 ms total transaction budget and no consumer retry/poll loop. The matching
`x4pro-i2c 0.1.2` serializes the UI-owner battery poll against the independent
GT911 capture task, rejects busy admission, and enforces per-transfer deadline
checks with fixed safety cleanup. A failed or contended sample is unavailable.
This does not claim a hard 20/80 ms wall-time bound or physical timing proof.
See [the bus contract and limits](../x4pro_i2c/README.md).

Run `bash test/run_x4pro_battery_test.sh` for the host regression against the
actual driver, with a fake bus and GPIO input boundary.

The test inherits sanitizer settings. In ptrace-based sandboxes, use
`ASAN_OPTIONS=detect_leaks=0 bash test/run_x4pro_battery_test.sh` if LeakSanitizer
cannot start; AddressSanitizer and UBSan remain active. The fixture and driver
do not allocate heap memory.
