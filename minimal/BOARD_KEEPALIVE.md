# X4 permanent board keepalive

The battery/RST candidate uses `x4pro-board-power` 0.1.3 with Runtime 0.1.57.
It is source and software qualification, not physical battery/reset acceptance.
Frozen product 0.1.9 and diagnostic 0.1.10 artifacts are unchanged.

## Source evidence and limits

The migrated `Drivers/x4pro_board/x4pro_pins.h` maps peripheral enable to GPIO1,
touch power to GPIO2 and SD power to GPIO5. The frozen `X4BootPower.cpp`
reference asserts GPIO1 HIGH before peripherals and keeps its pad held. GPIO2
and GPIO5 have independent active-low ownership. The minimal 0.1.9 path did not
link that native boot code: it initialized diagnostics, prepared banks/storage,
ran provisioning and admitted the graph before an ordinary provider could claim
GPIO1. Board-power 0.1.2 then left it unheld except during Deep preparation.

Those are verified source differences. No OEM power schematic or electrical
latch/reset deadline was established. They do not prove the root cause of the
reported USB-dependent/RST startup. The current early framework hook is after
Arduino PSRAM/clock/NVS initialization; a device test remains necessary.

## Native startup and admission

The X4-owned `minimal/native/X4EarlyBoot.cpp` supplies Arduino's existing strong
`initVariant` hook only in an explicitly composed X4 native image. It stages
GPIO1 HIGH and input/output configuration before releasing an old hold, restores
the hold, and reads the actual pad. It does no Serial/USB waiting, and never
touches GPIO2 or GPIO5. Every failed operation records its exact startup stage.
The optional generic Runtime status check reports that failure and stops before
bank preparation, mount, provisioning or driver/app execution. Ordinary drivers
still require the full board and selected-graph validation.

See [native composition](NATIVE_BOOT_COMPOSITION.md) for build commands and source custody.
A generic Runtime binary without the X4 composition is insufficient for this
candidate even if its Runtime version matches.

## Ordinary board provider and permanent CPU custody

Board-power remains the sole admitted GPIO1 owner at hardware instance 1. It
requires the append-only `read_retired_output` GPIO suffix before any I/O.
The provider takes its scoped nonblocking owner lock, claims HIGH, reads HIGH,
enables the pad hold, and retires the mutable token into CPU custody. A final
physical read must also succeed before it publishes readiness.

Retirement makes the permanent static hold safe for provider storage, app
handoff and ordinary provider quiescence. Readiness uses the new scoped physical
read, never a cached HIGH or the now-invalid token. It cannot write, PWM,
release or unhold the retired pad. Only a fresh claim by the same CPU scope
can restore configuration/HIGH before unhold; unrelated scopes are rejected.

The existing `PowerReadyV1` and tagged `X4BoardKeepaliveV1` layouts are unchanged.
The power provider still owns GPIO3 and binds `board.power.ready` to instance 1.
Deep prepare now checks the permanently held HIGH and retains the owner lock.
An ordinary native refusal calls restore, checks HIGH again and unlocks; it
never rewrites or unholds GPIO1. Terminal, retained and unknown native returns
keep exact locks, claims and dependencies as before. Recursive ready, prepare,
restore or quiesce cannot consume an in-progress transaction.

Failed claim/read/hold/retirement/unlock or physical LOW retains exact software
ownership and blocks cleanup, readiness and app exit. No release/reclaim is used
to hide uncertainty. Successful quiesce performs a final physical read and
destroys its lock while leaving the CPU-owned pad HIGH and held. A destroy
refusal permits a checked retry. A subsequent provider start makes a fresh
scoped claim and repeats the checked hold/retirement sequence.

## Verification

- Production board provider unit tests cover ABI rejection, all canonical and
  unknown startup hold failures, read/LOW/retirement/unlock failures, reentry,
  repeated ordinary Deep restoration, cleanup retry and reactivation.
- `run_board_keepalive_cpu_test.sh` compiles real board/power providers, JSON
  materialization, CPU scopes and native GPIO open/hold adapters. Both panel
  graphs verify storage/app-exit safety, forced LOW, retained failures, permanent
  HIGH across ordinary refusals/quiescence, same-scope fresh claims and exec-based
  fresh-process reset with HIGH/configuration before unhold.
- `run_native_gpio_readback_test.sh` retains real input-enable-dependent GPIO1
  readback and GPIO41 SD clock LOW/HIGH/LOW coverage.
- Runtime checks retain the old GPIO/Watch ABI, graph/lifecycle/sleep semantics,
  stale-token rejection and no-hook native boot behavior.

Host shims establish software ordering and custody only. Battery-only cold boot,
RST, USB attach/detach, timer/button wake, rail timing and current draw require
physical testing with the exact composed image.
