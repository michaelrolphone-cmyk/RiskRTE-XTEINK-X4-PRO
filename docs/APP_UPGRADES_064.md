# X4 0.1.64 candidate

Preserves delivered 0.1.63 display provider `x4pro-uc8279-fast` 0.1.13 byte for
byte. This is an offline full-flash candidate; hardware timing is not qualified.

Quick Actions reference glyphs, words, icons and rounded tiles restrict source
rows before scanning each raster slice. Runtime 0.2.3 adds a guarded append-only
monotonic clock: rendering no longer obtains heap, partition and EFUSE identity
for timing checks. Pending software replay uses a 1 ms scheduler pause. The
original single-row replay and 2 ms budget remain; larger batches failed the
heavy CPU input-latency bound and were rejected. Gesture tracking and snap stay
intact, with separate input reduction, software raster and physical frame stages.

Includes Contexts and SDR 0.3.2, RF Contexts service 0.3.1, timestamped IQ 0.3.0,
scene component/civil-clock dependencies and Lists 0.1.0. Lists uses namespace
63, preserving Home crash spool 62. Home, Contexts and SDR receive exact shared
RF model and rule files; rules can write the existing countdown namespace.
Context detection defaults off. The upgraded Spectrogram source and shared
signal models are integrated in Utilities. X4 has no microphone and does not
install an unsupported audio application.

The native composition forward-ports the temporal worker/shared-file API onto
0.2.2, retaining asynchronous Wi-Fi, failed worker custody, TCP, diagnostic
checkpoints and AppData exports. Selected native capacity: 24 immutable app
policy/requirement rows, 28 providers, 44 graph grants; live app grants remain
16. The installed cohort contains 22 applications and 27 providers.

Validation: 64 complete-buffer Quick reference comparisons; 1,014 primitive
comparisons including rotation/padding; simulated CPU input latency; fast-clock
rendering with health queries forbidden; Quick motion; ordered Springboard
swipes, reversal and queued release; 18 raster custody cases; actual shared-data
ELF authority/persistence; retained-clock rejection; SDR capture/input under
pending display; Lists persistence/components; Context fingerprints/rules; and
native IQ worker lifecycle. Applicable checks include ASan/UBSan. These are not
panel FPS measurements. Older broad RF scenes contain pre-upgrade page/gesture
expectations and are not claimed green; actual resident frame-progress tests pass.

The packager checks clean pinned sources, receipt hashes, native BIN/ELF exports,
each executable, full native graph admission in normal and sanitized modes,
deterministic SPIFFS plus independent readback, bank SHA/CRC pairing and preserved
flash ranges. It emits build custody alongside the image. Exact source trees
and public equivalents are in `minimal/app-upgrades-064-sources.json`.

Lists catalog metadata is retained in build custody and projected out of the
strict runtime app manifest after profile/version checks. Scene font licenses
and source notices accompany the image outside the exact boot-store inventory.

No merge, release or device flashing. The 16 MiB image at offset 0x0 replaces
internal settings, bonds and AppData; back up first. Removable SD is not included.
