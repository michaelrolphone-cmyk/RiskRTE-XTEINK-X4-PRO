# X4 native one-bit SD ordinary provider

`x4pro-sd@0.2.5` adapts the X4 native CLK/CMD/DAT0 transport to scoped
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

The shared hook delta is published as Reader PR441, commit
`4f5620a9ca4b2ee6c9a8adb038955671acde9556`, based on frozen Reader
`34d8e694d89a1e72d8854403d8592c289fae3ddc` and original volume.c blob
`c47cbca4da28351081c89cd50263c1be267d9f01`. The patch file records the exact
narrow delta for review. Production builds must select that published shared
commit, not silently apply an untracked patch. Existing OS-mutex and T5 paths
remain unchanged; the source branch associated with PR350 is untouched.

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
bounded. A power-down commit succeeds only after every scoped GPIO transition
succeeds; held claims and read handles keep the ELF pinned until reset.

No physical card or device has been operated. Native-card wire simulation and
an Xtensa ELF build do not constitute hardware timing/throughput qualification.

## Verified checks

After applying the proposed shared hooks to a temporary copy only:

```sh
RISCRTE_RUNTIME_ROOT=/path/to/RiscRTE \
RISCRTE_READER_ROOT=/path/to/shared-source-with-hooks \
SANITIZE=1 bash minimal/test/run_sd_test.sh
```

The 23 host scenarios cover dependency/config validation, absent media, native
FAT32 and MBR operations, stale file/directory handles, reentry/non-owner
rejection, sync create/take/unlock/destroy failures, retained GPIO releases and
claims, failed shutdown writes, GPIO reads, stuck clock counters, CRC failure,
rejected writes and busy timeout, operation budgets, generation exhaustion,
and successful/failed power commits. The unchanged Reader X4/T5 gate suite
adds 25 passing admission/lifecycle/absent scenarios against the shared patch.

The target ELF links with `-O2 -fPIC -mtext-section-literals -mlongcalls
-fvisibility=hidden -fno-builtin -nostdlib -nostartfiles -shared
-Wl,--hash-style=sysv -Wl,--exclude-libs,ALL -Wl,--no-relax`, compiling this
`driver.c` with shared `fatfs/ff.c` and `fatfs/ffunicode.c`, linked against
`-lgcc`. Use one canonical SDK include directory from `prepare_sdk.py` and
include the selected shared `Drivers/storage_fatfs` directory. It exports only
`t5_driver_get`; imports are `memcpy`, `memcmp`, `memset`, `strlen`, `strchr`.
Xtensa relative-target validation passes 314 pointers.
