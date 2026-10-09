# X4 native one-bit SD ordinary provider

`x4pro-sd@0.2.8` adapts the X4 native CLK/CMD/DAT0 transport to scoped
`platform.gpio@1`, `platform.clock@1` and `platform.sync@1`, with explicit
`board.power.ready@1` admission. It is not SPI and imports no firmware SD,
FreeRTOS, task identity, MMIO or filesystem service.

The hardware envelope must select compatible `xteink,x4-pro-sd-native1`, revision
`unspecified`, config `gpio.bank@1`, and exactly `[5,41,42,40]` (count4, canonical zero-filled unused ABI slots).
Bank active-high is 1, pull-up authority is 1 and all timing/reserved fields are 0. Channel
order is power, CLK, CMD, DAT0. The native protocol owns GPIO5's active-low power
sequence and CMD/DAT0 direction changes and pull-ups. No generic bus identity is
assigned to this native transport.

## Source ownership

Transport and existing volume behavior originate at Reader commit
`34d8e694d89a1e72d8854403d8592c289fae3ddc`. `volume.c`, `sd_protocol.h`, and FatFs
remain external shared sources; the build includes them directly from the
selected Reader/shared-source checkout. This directory does not vendor FatFs.

The shared external-guard baseline is Reader PR441 at
`cccfa3fd9b606998c27adb03665722ea44d5358f`. The checked sleep recovery delta is
published as [Reader PR443](https://github.com/michaelrolphone-cmyk/T5S3-Reader/pull/443)
on the separate `codex/storage-sleep-resume` branch based on that commit. Select the exact shared commit recorded in `minimal/sources.lock.json`;
do not silently apply an untracked helper/header patch. Existing OS-mutex and T5
paths remain unchanged; the source branch associated with PR350 is untouched.

## Safety and limits

One owner-task, nonrecursive, nonblocking sync guard admits each operation.
Failed unlock retains its lock, GPIO claims, dependencies and ELF; failed pin
release retains the exact token and failed sync destroy is retriable without
reopening API admission. Unknown claim ownership is never discarded. Shared
file/dir generations, live-writer retention and operation/sector budgets remain
unchanged. Transfer cooperation is checked each 64 bytes and yields after 4 KiB
or 4 ms, including across calls.

GPIO readback and a bounded cycle-counter guard enforce each clock phase.
Identification phases have a 300-cycle minimum (1.25 us at max 240 MHz); selected
phases retain a 24-cycle minimum. Stuck counter/readback and card-busy waits are
bounded. Legacy `prepare_power_down` / `commit_power_down` remain terminal:
held claims and frozen read handles keep the ELF pinned until reset.

The optional tagged `storage.volume@1` sleep suffix is a separate zero-handle
transaction. Its checked prepare rejects readers, writers and directories;
commit unregisters FatFs, parks the bus, switches GPIO5 HIGH, then holds it.
After a refused deep-sleep entry, resume releases the hold and runs the normal
bounded SD initialization/remount. Old closed tokens remain invalid; generations
are never reset. Legacy and new transactions cannot be mixed.

Resume returns READY only after usable media is restored. MEDIA_UNAVAILABLE
means GPIO custody and powered rails are recovered but there is no usable
filesystem; readiness/I/O fail and normal refresh can retry. Partial GPIO
commit, hold/unhold, uncertain remount and unlock failures return RETAINED and
keep the provider, remaining claims and dependencies pinned. A busy/wrong-owner
call returns REFUSED without changing the existing transaction. Repeated resume
after recovery does not cycle the card again.

No physical card or device has been operated. Native-card wire simulation and
an Xtensa ELF build do not constitute hardware timing/throughput qualification.

## Verified checks

Against the exact shared source and Runtime checkouts:

```sh
RISCRTE_RUNTIME_ROOT=/path/to/RiscRTE \
RISCRTE_READER_ROOT=/path/to/shared-source-with-hooks \
SANITIZE=1 bash minimal/test/run_sd_test.sh
```

The 54 host scenarios cover dependency/config validation, absent media, native
FAT32 and MBR operations, stale file/directory handles, reentry/non-owner
rejection, sync create/take/unlock/destroy failures, retained GPIO releases and
claims, failed shutdown writes, GPIO reads, stuck clock counters, CRC failure,
rejected writes and busy timeout, operation budgets, generation exhaustion,
and successful/failed terminal power commits. New cases cover empty/absent/
removed/unformatted media, live-handle refusal, prepare rollback, repeated
cycles and stale closed file/directory tokens, mixed old/new calls, busy/reentry,
partial clock/CMD/DAT/rail/hold changes, failed unhold/remount and repeated-phase
unlock retention. ASan/UBSan pass. The unchanged Reader X4/T5 gate suite adds 25
admission/lifecycle/absent scenarios; both full FatFs wire-model suites pass.
Legacy X4 and T5 objects are byte-identical with no new conditional hook.

The target ELF links with `-O2 -fPIC -mtext-section-literals -mlongcalls
-fvisibility=hidden -fno-builtin -nostdlib -nostartfiles -shared
-Wl,--hash-style=sysv -Wl,--exclude-libs,ALL -Wl,--no-relax`, compiling this
`driver.c` with shared `fatfs/ff.c` and `fatfs/ffunicode.c`, linked against
`-lgcc`. Use one canonical SDK include directory from `prepare_sdk.py` and
include the selected shared `Drivers/storage_fatfs` directory. It exports only
`t5_driver_get`; imports are `memcpy`, `memcmp`, `memset`, `strlen`, `strchr`.
`minimal/test/run_sd_target_test.sh` reproduces the target checks with the
selected `NATIVE_DRIVER_CC` and `PYTHON`. Xtensa 8.4.0
(esp-2021r2-patch5), `-O2 -fno-ivopts --no-relax`, passes 357 relative pointers,
the exact export/import boundary, and absence of provider-BSS compare-and-set.
The checked ELF is 62,976 bytes, SHA-256
`7c88b67189a7fe5dcc694541823ac9225394debfbcf41a25dadb1755ad1fa5c8`.

This branch changes only SD source/package `0.2.5 -> 0.2.6`, its tests and the
shared dependency pin. It does not update the frozen X4 0.1.6 artifacts, enable
deep-sleep policy, implement retained clock state, merge code, or flash hardware.
The future coordinator must close all storage handles before prepare, retain
the provider across the whole transaction, and distinguish all resume results.

## Persistent diagnostic export

0.2.7 adds the read-only `platform.diagnostic-source@1` dependency and writes
`/x4-boot.log` through this same FatFs owner after a usable mount. No app grant
or second SD stack is added. See [capture, ownership, failure and test details](../../docs/BOOT_LOGGING.md).


## Exclusive USB raw-media export

0.2.8 adds the optional `RiscStorageExportV1.h` tail after the existing sleep
prefix. A USB provider retains its `storage.volume` dependency and invokes this
tail only on the normal SD owner task, never from a USB task, ISR or callback.
The begin/read/write/sync/end callbacks return READY, REFUSED or RETAINED; end
may also return MEDIA_UNAVAILABLE after checking local custody. Consumers must
inspect the result, including failed begin, and keep every dependency pinned on
RETAINED. Successful end consumes the token; old tokens never become valid again
through refresh, quiesce/start or another export. Generation exhaustion refuses.

Begin rejects every caller file/directory, drains a bounded pending diagnostic
batch, pauses the logger, checks card sync and checks FatFs unregistration.
CMD9 supplies the physical block count: CRC-checked CSD v1/SDSC or v2/SDHC/SDXC,
with an explicit unsupported-version/address-range rejection. It never derives
capacity from FAT or a partition table. Unformatted media can be exported;
absent media or invalid CSD cannot. Transfers are whole 512-byte sectors, at
most eight per call, with checked 64-bit range arithmetic and the existing
byte/time checkpoints, scheduler yields and transport timeouts.

While the host owns media, every normal file/directory operation, refresh,
sleep/legacy power transition and quiesce refuses. Logger and diagnostics do
not touch the filesystem. Once USB has stopped requests on eject/disconnect,
end checks sync, runs normal native initialization/remount, and resumes logging.
Absent/unformatted media returns MEDIA_UNAVAILABLE and permits later refresh;
failed transfer, sync, unmount, remount integrity, logger close or unlock retains
custody. No uncertain write is retried and no path formats the card.

The 93-scenario native SD suite passes ASan/UBSan, including 26 raw-export cases:
ABI prefix/tag/size checks, all caller-handle refusals, retained caller/logger
close, owner/reentry refusal, sleep exclusion, stale generations, exhaustion,
CSD CRC/version and full 2^32-sector parsing, absent/unformatted media, bounds,
checked unmount, failed raw I/O/sync/unlock/remount, removal, logger pause/resume,
and cleanup. The existing 25-case shared X4/T5 admission suite and tagged sleep
ABI checks pass. Both legacy SD objects remain byte-identical with the optional
admission hook undefined. Xtensa links and verifies 516 relative pointers,
exports only `t5_driver_get`, and imports only `memcmp`, `memcpy`, `memset`,
`strchr`, `strlen`. The tested ELF is 76,160 bytes, SHA-256
`573e50fd5b8d92665b0310984707c03f823adf0e4c1e80a3c76348592f0b8232`.
Host simulation and the target build do not establish physical USB/SD timing,
hot unplug, power-loss or filesystem consistency after an unsafe host removal.
