# Springboard first-frame stall regression

The supplied X4 0.1.31 log reaches Springboard successfully: launch accepted at
53.989 s, old app unloaded at 55.054 s, new app initialized at 57.035 s, and its
first presentation accepted at 59.007 s. The failure at 60.453 s is
`PORTABLE_APP error=display-status`, followed by retained app custody.

That exact error means `present_status` returned false. A missed BUSY assertion
alone instead leaves a readable token with state FAILED and produces
`error=display-failed`. This distinction is visible in the production adapter
and selected `x4pro_uc8279_fast` 0.1.6 provider.

The provider retains CS across cooperative polls while uploading each plane.
Its SPI transaction has a 1,000 ms deadline. Runtime `Port::spiTransfer` rejects
the next exchange if that deadline elapsed between polls. The provider then
records `spi exchange retained`, invalidates its image history, and refuses
status/acquisition through its retained-custody guard. Synchronous provider
service from `Runtime::yield` can consume 1,000 ms per invocation; synchronous
trace I/O can also delay a touch log while the panel transaction is held.

## Reproduction and repair

`minimal/test/run_springboard_display_test.py` reuses the existing cadence
hardware model and Runtime bridge, and compiles the actual production
Springboard controller, shared adapter, Runtime scheduler, provider graph and
selected panel provider. It uses the delivered 18-entry catalog and the selected
paper scroll, crossfade, LOW_LATENCY, stage logging and display metrics paths.
Radio, alarm, native time-reader and power-service behavior is outside this
focused fixture. The toolbar and raw GPIO/SPI/clock boundaries are deterministic
fixtures; the SPI model applies the production CpuPort deadline rule.

The test starts with completed Home-like panel history, runs the real
Springboard entry and input loop, and checks its first presentation. It does not
model the ELF Home-to-Springboard loader itself; that handoff already succeeds
in the supplied hardware log.

With Runtime 76873fef, two injected 600 ms service callbacks during the first
plane produce the same application failure signature after 1,235 modeled ms:
32,768 bytes uploaded, no refresh command issued, one SPI deadline rejection,
and retained provider/app custody. With Runtime faa8f629, provider service no
longer runs inside yield: the first 48,000-byte frame completes in 108 modeled ms
without a deadline rejection or retained state. The provider implementation is
byte-identical in both runs.

A separate 1,200 ms synchronous touch-log callback still reproduces the status
failure even with repaired Runtime. A nonblocking callback completes. These
are scheduler boundary models, not validation of the native trace queue;
validation of the logging implementation belongs to its separate tests.

The remaining controls preserve error handling:

- An 82 ms BUSY pulse starting 1 ms after DRF is missed by a delayed poll. The
  token fails with `busy never asserted`; no success is invented.
- A pulse observed immediately after DRF can finish during the same 120 ms
  delayed poll interval and still complete normally.
- An absent BUSY pulse fails assertion; a stuck pulse fails completion.
- After uncertain presentation or SPI retention, another frame cannot be
  acquired.

Eight controls passed against each Runtime, 16 checks total. Neither the
1-second plane deadline, 100 ms BUSY assertion deadline, 3.5-second completion
deadline nor retained-custody guards were changed. The appropriate production
repair is to remove blocking service/log persistence from the foreground
presentation path. No panel production change is required by this reproduction.

## Evidence and limits

`springboard-display-stall-validation.json` contains the commands, source
hashes, catalog, Runtime identities, and all before/after results. The test has
an optional `--sanitize` mode; these recorded runs are normal host runs.

The hardware log does not include the provider's `last_error` result. It proves
successful launch/init followed by a status/custody failure, but cannot by
itself identify SPI deadline expiry versus another GPIO/SPI/sync failure. The
host reproduction establishes a concrete matching software failure and verifies
the Runtime correction. Physical confirmation requires a new device log. No
hardware was accessed or flashed, and the frozen 0.1.31 artifact was not changed.
