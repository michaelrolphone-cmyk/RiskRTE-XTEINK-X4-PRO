# X4 RTC capability reuse

`x4pro-rtc@0.1.0` is a new ordinary external SD driver, providing the existing
`rtc.clock` API **2** through the canonical `i2c.bus` API 1. This is an X4 package,
not an update/rename of Watch's separate board-specific `twatch-rtc` product.
There must be only one installed owner of the physical RTC address.

## Source and hardware evidence

- [Watch source at 9cfa2aa4](https://github.com/michaelrolphone-cmyk/RiscRTE-T-Watch-S3/tree/9cfa2aa4d572a290b41cf040a27cbec9d78bb35c):
  `include/twatch_caps.h`, `include/twatch_calendar.h`, and
  `drivers/twatch_rtc/driver.c`. The neutral `RiscRtcClockV2.h` retains the exact
  calendar and function-table layout, including read/write/alarm/alarm_pending.
  The original misleading `TWATCH_RTC_API_V1` name has value 2; the new spelling
  does not introduce a new API. Host and Xtensa layout assertions compare every
  field to the frozen source. No Watch repository or delivered bytes are changed.
- [FreeInk board evidence](https://github.com/Free-Ink/freeink-sdk/blob/main/docs/xteink-x4pro-support.md#rtc--usb--battery)
  identifies BM8563, PCF8563 register-compatible, at 7-bit address `0x51` on
  SDA39/SCL38, sharing the existing bus with touch and battery. The existing
  `x4pro_pins.h` is the package's board configuration source. No new bus owner,
  pin probing, GPIO access, or private firmware I2C import is added.
- [NXP PCF8563 register specification](https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf),
  sections 8–9, establishes control 00h, seconds/calendar 02h–08h, BCD, STOP/VL,
  century, alarm 09h–0Ch and the open-drain interrupt. X4's RTC interrupt pin is
  **not established by the board evidence**, so this profile has no GPIO
  dependency and refuses alarm enabling without any bus I/O. Minute sleep uses
  the existing ESP SDK timer; this package makes no hardware RTC wake claim.

`Drivers/common/pcf8563_rtc_ops.h` and `RiscRtcCalendarV2.h` are narrow source reuse
from the Watch implementation, with explicit changes: validated output commits
atomically; STOP/test mode and unsupported century fail closed; error text is
available through the existing provider diagnostics; only an explicit valid-time
write may clear STOP. Startup and ordinary release do not initialize the clock,
change clock-out/alarm registers, clear VL or write synthetic time. Watch's board
hardware/device/GPIO framework is not transplanted into Reader.
Restart writes the normal all-zero control byte: the datasheet permits reserved
N bits to read either value but requires writing them as zero. STOP plus all
reserved bits set is regression-covered; those read values are never echoed.

## Ownership and consumer behavior

The safe tagged I2C contract is required before admission. The single address
claim is retained, along with its bus dependency, if release cannot drain. Each
operation has at most four fixed synchronous bus transactions, each with a total
30 ms timeout; there are no polls or retries. A bad calendar/read never changes
the caller's output. The 2000–2099 convention matches the existing Watch contract
and Reader's current RTC convention.

`NativeRtcClock` is a firmware **consumer adapter**. It binds to the trusted
invocation-owner task, checks the whole API2 table and exact generation grant,
rejects reentry and foreign-task access, copies time out and immediately releases
ordinary leases. A partial/failed release retains its token with interface
revoked. Suspend blocks new acquisitions and retries cleanup; failure refuses
sleep. Resume does not enable acquisitions unless cleanup succeeded. The minute
fast path can suspend before RTC startup without reading/loading the RTC.

X4 `HalClock` contains no compiled register/protocol/Wire implementation. It uses
this capability, preserving the existing UTC/local/reference-epoch conversion
and timezone catalog. Normal cold boot reads after saved settings are loaded;
missing package/chip, VL, STOP or invalid calendar leaves time unset with a log
and never blocks Home. Read failure reasons are in provider diagnostics; the
consumer reports the supported read-failure categories. Existing NTP/browser
manual synchronization writes valid UTC through the provider and verifies the
read-back before existing settings are committed. T5's legacy two-layout RTC
path is unchanged and has both layout fixtures.

## Packaging and verification

The builder emits an ordinary ELF and source manifest; the generic staging path
creates `.package.json`, provider/import metadata and `x4pro-rtc-0.1.0.rte.zip`.
The installed directory and Inbox archive use the same ID/version. This optional
package is included in the staged SD tree but omitted from mandatory `boot.json`,
like the battery package. An older SD tree remains bootable; a missing package
leaves the clock unset. No flash fallback, installation, device action, merge,
release, or default settings write is performed by this change.

Run `bash test/run_x4pro_rtc_test.sh` for actual driver + actual HalClock UTC/local,
valid/invalid/STOP/VL/calendar, write error, wrong-task/reentry, partial/retained
lease, minute wake and absent-provider fixtures, T5 legacy fixtures, and actual
X4 bit-bang I2C + RTC shared-claim/transaction/failure integration. Run existing
clock retry/desk-clock regressions as well. The target builder checks ABI layout;
import validation requires zero undefined imports and the loader relocation map
must have no unmapped sites, values or executable sections. These establish
software behavior only. Physical RTC retention and board wake remain unverified.
