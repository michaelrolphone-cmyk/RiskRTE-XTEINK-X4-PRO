# X4 board-alive deep-sleep transaction

This is future desk-clock source work. It does not change the frozen 0.1.6 image,
operate hardware, measure sleep current, or qualify the product on a device.
The pinned foundation is X4 `cdb7145f1cec59b766f73883e461ca001f1d56d6`,
RiscRTE `3c39aa7ac50ecd7f6da0296a62a2d7af066f82d1` (0.1.47), and
Reader SDK `aac8c06d3221139084acd0cfc64f7b0ba194a97a`.

## Ownership and compatibility

- `x4pro-board-power` 0.1.2 remains the only GPIO1 owner at hardware instance 1.
  It adds the scoped `platform.sync@1` dependency.
- `PowerReadyV1.h` and its layout are unchanged. The optional, tagged
  `X4BoardKeepaliveV1.h` suffix appends `prepare` and `restore`. Both return zero
  on success and use the canonical `RISC_DEEP_SLEEP_*` negative status values.
- `x4pro-power` 0.1.2 owns GPIO3 at instance 17 and has an explicit
  `board.power.ready@1` binding to instance 1. Generated sleep profiles for both
  panel variants carry that binding. A missing or wrong binding fails graph
  preflight before any hardware I/O.
- An old board-ready prefix remains valid for light sleep, while deep entry
  returns `RISC_DEEP_SLEEP_UNSUPPORTED`. Truncated, wrong-tag, wrong-version,
  or incomplete keepalive suffixes also fail deep entry closed.
- The `X4PowerV1` light prefix and existing `X4PowerDeepV1` entry layout do not
  change. No Clock grant is added; navigation and battery gain no authority.

## Transaction

The board provider claims GPIO1 as HIGH. The canonical CPU/native claim stages
that HIGH latch and input/output configuration before removing any old pad hold.
This preserves the migrated `X4BootPower.cpp` keepalive policy. GPIO1 is never
written LOW. Independently owned touch/SD rails remain outside this transaction.

After the existing GPIO3 neutrality check, the power provider calls board
`prepare` immediately before the synchronous native timed deep entry. Preparation
holds the board owner's nonblocking lock, writes HIGH, reads back HIGH, and
engages the typed GPIO hold. Successful preparation keeps that lock held.
There is no Runtime yield, provider polling, app callback, or unrelated operation
between successful preparation and native entry/checked refusal restoration.

Success is terminal. Native `RETAINED`, zero, positive, and out-of-contract
negative returns preserve both owner locks, tokens, dependencies and the rail
hold. They do not invoke normal restore or unlock. Ready, reentrant preparation,
and quiesce cannot steal or release the prepared board transaction. An explicit
restore-phase guard also rejects recursive restore during unhold/readback.

Only a canonical ordinary native refusal runs board `restore`: unhold GPIO1,
read back HIGH, unlock the board owner, then unlock the power owner. Repeated
refusal requires a new released-key neutral observation, exactly as before.

An ordinary hold-enable refusal is recoverable only because the canonical CPU
contract proves rollback. The provider still checks HIGH before unlocking.
Failed/unknown hold cleanup, failed unhold, failed HIGH write/readback, and failed
unlock latch retained uncertainty. Subsequent ready/prepare/restore/quiesce/stop
cannot perform GPIO cleanup, release/reclaim the rail, or claim readiness.
Recoverable ordinary release/destroy failures keep the exact remaining ownership
and permit checked quiesce retry; no replacement token is manufactured.

## Verification

Focused host suites exercise every partial boundary, all canonical hold-refusal
statuses, missing/legacy suffixes, wrong/missing bindings, owner/reentry guards,
and repeated restoration. `run_board_keepalive_cpu_test.sh` compiles both real
providers with the actual JSON graph, CPU port, and native GPIO open/hold adapter.
Its IDF shim verifies HIGH-before-hold, retained shutdown, and a terminal
exec-based fresh-process boot that configures HIGH before unholding GPIO1.

Normal and sleep graphs for both panels, the existing GPIO readback regression,
all nine provider suites, power/navigation and legacy light Clock tests remain
covered. Target checks use pinned Xtensa ESP32-S3 GCC 8.4.0, canonical SDK assembly,
relocation normalization/validation, the `t5_driver_get` export-only boundary,
restricted C imports, and the PSRAM-unsafe compare-and-set exclusion.

Host emulation establishes software ordering and ownership only. Physical
GPIO1 retention, timer/button wake reliability and current draw remain untested.
