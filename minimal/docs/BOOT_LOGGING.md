# Persistent X4 boot diagnostics

The recovered 0.1.29 native boot logger is integrated with the existing typed SD
owner. SD package `x4pro-sd` advances from 0.2.6 to 0.2.7; Runtime 0.1.78 supplies
the optional `platform.diagnostic-source@1` copied, read-only provider dependency
and the post-output native drain. Runtime is based on canonical 0.1.76, without
the separately reserved/unqualified 0.1.77 request_default change.

## Where the evidence appears

- Physical SD root: `x4-boot.log`, with `x4-boot.previous.log` after rotation.
  The ordinary volume-relative paths are `/x4-boot.log` and
  `/x4-boot.previous.log`; Files exposes the card under `/sd`.
- Internal app-data: `/appdata/x4-boot.log` and
  `/appdata/x4-boot.previous.log`.
- Early persistent fallback: eight checksummed 456-byte session summaries in
  the existing NVS `x4_bootlog` namespace. No erase/format recovery is performed.

The first flash checkpoint is after the existing GPIO1 rail preparation in
`__wrap_app_main`, before Arduino initialization and filesystem mounting. Reset,
wake, GPIO/strap observations, named boot/provider/app timestamps, first display
completion and the first recorded terminal failure are retained. Power source
and battery voltage are unmeasured. A later successful boot exports earlier
sessions rather than relabeling its own power conditions as the failed attempt.

SD export begins only after normal graph validation, SD provider activation and
a successful FatFs mount. The explicit diagnostic cohort sets `boot_start="cold"` only on its selected
SD node while retaining global demand-retained activation. Native reset reason
classifies every non-deep reset as cold; all deep wakes remain demand-only, even
with missing/corrupt app RTC records. The existing session graph grant keeps the
SD owner available for later ordinary consumers, with normal cleanup/sleep rules. No second native/Arduino SD stack, early peripheral
activation, app grant, pin change, flash-mode change or USB host is introduced.

The file contains plain named text with monotonic timestamps and explicit
session/revision identity. `record_end=complete` terminates each fully copied
snapshot; a partial final record is not proof of a complete checkpoint. This is
a bounded checkpoint journal, not an exhaustive transcript of every log line.
Ordinary touch/move/render chatter does not create flash checkpoints or SD writes.

## Ownership, bounds and failures

Runtime copies at most 1535 bytes plus NUL for one of nine slots (current and up
to eight flash slots). The source has no storage authority, callback registration
or borrowed pointer lifetime. Apps cannot acquire this raw platform capability.

The SD owner considers pending snapshots when leaving an admitted operation.
Initial mount drains up to nine snapshots automatically. Later ordinary calls
write at most one snapshot each,
only with zero caller-owned file/directory handles, active rails and a usable
mount. The current caller's errors and handle positions are preserved. Snapshot
sequence/revision acknowledgments happen only after checked FatFs close/sync;
repeated ordinary calls and refresh do not append acknowledged revisions again.
A later boot can append recovered summaries again, with explicit identity.

The initial batch has at most nine append/checked-close pairs, 13,815 text bytes
and 81 RAM source reads. The entire batch shares the existing 15,000 ms / 2048
sector / 1,048,576 filesystem-step budget, rather than resetting it per snapshot.
The deadline is checked between bounded transport operations; an in-flight
sector may finish after it. Native transfer cooperation continues during I/O.

SD rotation occurs before the next record would exceed 128 KiB. At most two
owned log files are kept. Rotation removes the older previous log before renaming
the current file; a power interruption can lose that older rotation, while the
current evidence remains in either current or previous. This is not an atomic
two-file transaction. Internal app-data uses the recovered 128 KiB rotation
threshold; a bounded history batch may extend it by fewer than nine 1536-byte
records. NVS ordinary checkpoints are capped at 64 per startup, with first display
and first terminal failure preserved beyond that cap. One subsequent first-part
provider detail may enrich the checkpoint while preserving the original failure;
further detail/cleanup chatter is ignored.

Missing/unformatted media keeps the provider's existing refresh behavior and
leaves evidence in NVS/app-data. Read-only/full/open/partial/write/close/rotation
failures are reported by the published volume `last_error` callback as
`boot-log: ...`, alongside existing caller errors. The ABI2 diagnostic suffix
also exposes the same bounded RAM-only status after failed start or quiescence,
so Runtime can include it in persistent failure evidence. The first logger failure is
kept and SD writes are disabled for that provider instance, avoiding repeated
uncertain appends. A failed log close retains the actual writable FIL and its
ordinary file slot, fences successful operation returns, and makes remount,
sleep and quiesce fail safely with the provider/dependencies retained. Caller
files are never closed, committed, aborted or displaced by logging.

Sleep preparation freezes export. Nothing writes while prepared, committed,
retained, unmounted or shutting down. Successful checked resume may export a
pending snapshot after normal remount. If a failure is followed by no further
SD operation, it remains in NVS and is exported on a later usable activation.
Internal app-data retries before writing are capped at two per checkpoint;
uncertain write/sync/close failure disables its further appends for that boot.

## Verification and limits

Build the selected diagnostic cohort with `build_test_bundle.py --boot-log`;
the source feature and explicit flag must agree. The bundle receipt records the
activation policy, SD path and maximum initial export size. This introduces no
Clock grant or display dependency.

`minimal/test/run_bootlog_test.sh` compiles the production native implementation
against IDF/NVS stubs and a real host filesystem. `minimal/test/run_sd_test.sh`
compiles the production provider, shared FatFs and native-card wire model; the
67 cases include absent/unformatted/read-only/full media, partial/write/close
failure, caller-handle custody, shutdown/sleep, repeated exports and rotation.
Both pass normally and under ASan/UBSan; LeakSanitizer is disabled because the
executor uses ptrace. The generic Runtime source/drain has independent actual
provider binding, app-denial, owner/reentry and flag-off tests.

`minimal/test/run_sd_target_test.sh` verifies the Xtensa ELF export/import boundary,
relative relocation targets and absence of provider-BSS compare-and-set. Full
composed firmware, cohort, exact-source receipt and image qualification remain
separate coordinator steps; a host/ELF pass is not a device test.

No coverage is claimed for ROM/bootloader failure, earlier flash/PSRAM startup,
failure before the application rail step completes, or power loss before a
checkpoint commits. No logged reset flag establishes battery-only power.
Storage instrumentation changes timing and power demand and is not an electrical
measurement. No device operation, flash erase, USB observation requirement or
battery-startup fix is part of this change.

For offline recovery from a separately obtained NVS or unencrypted flash image,
`minimal/scripts/recover_bootlog.py` scans checksum-valid contiguous candidate
blobs. It is a forensic fallback, not an NVS transaction/page parser; fragmented,
superseded or unindexed records have the limitations stated in its output. Share
the resulting diagnostic text rather than unrelated raw NVS settings.
