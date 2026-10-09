# Ordered X4 boot trace

SD package `x4pro-sd` 0.2.11 writes actual initialization text to SD root
`/x4-boot.log`, with bounded rotation to `/x4-boot.previous.log`. Files exposes
these as `/sd/x4-boot.log` and `/sd/x4-boot.previous.log`; USB mass storage exposes
the card root. Internal app-data uses the same two filenames under `/appdata`.
The old .30 checkpoint-only artifact remains unchanged.

Each complete line contains `X4_TRACE session=… event=… us=…` followed by the
actual diagnostic statement. `us` is monotonic microseconds at observation;
`event` increases within the session. Session numbers come from NVS and survive
loss of RTC RAM. An unavailable NVS identity is explicitly `session=0` and
`session_identity=unassigned`; reset/raw/wake values and firmware identity mark
each new attempt. Power source is unmeasured.

Capture starts at application-wrapper entry. GPIO1 rail preparation includes
actual operation/result/time rows. All admitted Runtime lines are copied before
USB/backpressure can discard them: RTE_SOURCE/BOOT/PROVISION/STORAGE, timestamped
provider load/start/reference/detail, app load/init/entry, and application
initialization/draw/transfer/completion. The existing PROVREF ESP_LOG initialization
stages are mirrored through the same diagnostic facility. ROM/bootloader text
before app_main and unrelated IDF log producers are outside this capture.

First display remains a special marker, but may only be a logo: later RTC/Clock
and other named initialization still appears. Each app-entry boundary reopens
its first-frame capture. Repetitive APP touch/draw/transfer/display chatter after
a completed frame is filtered; later named initialization and errors continue.
A terminal RTE_BOOT error closes ordinary capture, with up to eight subsequent
failure/detail lines. Credential-bearing diagnostics are replaced by an explicit
redaction. Long source lines and exhausted capacity are explicitly marked.

## Memory and persistence

The early prefix is 8 KiB of internal RAM. At initVariant, after Arduino PSRAM
initialization, an explicit PSRAM-only allocation reserves 64 KiB for current
text and 64 KiB for the preceding attempt. Allocation failure uses only the early
buffer and emits an overflow marker when full; it does not consume a large
internal-heap fallback. Two KiB is reserved for overflow/failure evidence.
Drawing/submit/transfer defers native filesystem and ordinary NVS work until display completion/skip; a new terminal failure remains durable. Repeated frame/input diagnostics with no new text perform no file I/O. The observer only copies bounded text: no allocation, NVS/file I/O, diagnostics,
Runtime or provider calls.

The existing post-output native drain writes new text to app-data after its
normal mount. It appends/flushes/fsyncs/closes a 4 KiB batch or a partial batch after two seconds, measured after the preceding commit. First display and new failure evidence force persistence. Ordinary NVS milestones coalesce on the same two-second cadence. The previous file is
read into bounded PSRAM and frozen before SD can consume the source; then current
is renamed to previous once per boot. This lets a USB recovery boot export the
preceding battery attempt even when its last lines never reached SD. A partial
trailing line is discarded on recovery. Complete current/previous files are
at most 64 KiB each. Uncertain append/sync/close disables further internal writes
for that boot, with an explicit buffered error; pre-write retries are bounded.

The small, checksummed eight-slot NVS crash summaries remain separate. Their
64 ordinary-checkpoint limit does not limit the full text. The initial app_main NVS checkpoint remains immediate, before initVariant, PSRAM allocation and app-data mount. First display and
first failure retain special evidence beyond that limit. The newest valid earlier NVS checkpoint is mirrored into the current stream
as `recovered-summary`, with its original session, phase, reset, last line and
first failure. This preserves summary evidence from attempts that stopped before
initVariant or app-data mount; it does not reconstruct their missing full text.
Mirroring uses the already-read NVS history and adds no storage operation. No erase/format retry
runs. Before app-data is writable, early full text is volatile: sudden power
loss can lose it, although committed NVS summaries may survive. After mounting, the final unflushed RAM suffix can still be lost on reset or power loss. Explicit USB export drains quiet partial SD batches before handing over the card. Neither PSRAM
nor RTC RAM is claimed to survive complete power loss. A reset during a file
write can leave a partial tail; checked writes improve evidence, not electrical
or filesystem guarantees.

## SD ownership and service

The source extends the unchanged v1 snapshot prefix with a size-guarded
`read_after` copied-text tail. A byte cursor identifies immutable-prefix text;
reads are bounded, allocation/I/O-free and repeatable. Only checked FatFs close
advances the SD consumer cursor. No native storage pointer escapes.

The existing cold-boot selection activates the admitted SD provider before app
entry. Runtime 0.1.81 invokes the short provider service at lifecycle boundaries,
never from display/input yields. In 0.2.11 this service is RAM-only: at most four
copied-source reads of <=1535 bytes stage one <=4095-byte batch, with no FatFs or
physical SD operation. A 1-second scheduling slice must not interrupt an open
FatFs writable transaction. Existing explicit long SD owner operations can
append/close one staged batch under the normal 15-second/2048-sector guard.
The internal persistent trace remains the full recovery source while the SD
tail is pending. SD freshness during an idle app is therefore not immediate.

The USB transfer preparation screen explicitly brings the SD log current.
begin_prepare freezes a finite copied-source high-water mark and reserves a
checked preparation token, pausing ordinary drains and all local admission.
Each explicit prepare_step completes at most one append/close transaction, with
its own 15-second/2048-sector hard guard. A later empty-tail step syncs/unmounts;
only READY allows PHY/USB ownership. There is no whole-backlog deadline that can
expire inside a later writable transaction. Status polls never advance this
work. Cancel between transactions checks media sync and restores local custody.
Progress diagnostics after the frozen cutoff remain pending for local drainage
after cancel/eject, without being dropped or prematurely acknowledged.

The ordinary transport continues scheduler cooperation and bounded sector waits.
Caller handles, sleep, quiesce, retained custody and host ownership exclude local
logging. Genuine transport, transaction deadline, write and close failures still
retain custody; their first media error is preserved in the bounded diagnostic.

At most two 512 KiB SD logs exist. Rotation occurs before the next complete chunk
would exceed the bound; it removes the older previous file then renames current.
This is not an atomic two-file transaction. Logger failures are reported beside
the caller's existing diagnostic. Uncertain writes are never retried; an uncertain
close retains the actual writable FIL/slot and blocks sleep, remount and unload.
No caller handle is closed, displaced, or committed by logging.

## Verification

`run_bootlog_test.sh` compiles the production native logger against IDF/NVS mocks
and real host files. It generates failed/recovery sessions with more than 100
initialization events, reset boundaries, first-display then RTC/Clock initialization,
monotonic event/time checks, no-USB operation, missing storage, PSRAM failure,
credential redaction, overflow and uncertain persistence.

`run_sd_test.sh` feeds that exact production output through the real SD provider,
shared FatFs and native-card wire model, then reads the actual saved file and
compares every byte. It verifies no-work I/O, owner/recursive custody, at most one
chunk per explicit preparation step, zero service I/O, slow transaction deadlines, invalid sources,
uncertain close, pending-text export, and existing storage/sleep/export cases.
`X4_SD_LOG_ARTIFACT` saves the verified file for inspection. Runtime tests separately
exercise copied-source guards and real mapped-provider/Runtime service boundaries.
`run_bootlog_latency_test.sh` compares the frozen .31 source with the changed
production logger against the same transcript, with 25 ms file-sync and 8 ms NVS
latency. It asserts identical source-line payloads, zero frame-time commits and
zero additional writes across 1000 no-work drains. The SD cadence test injects
8 ms/read and 20 ms/write sector costs and presents four new statements per
acquisition, asserting bounded append/close counts and 1000 no-work calls.
These are operation-count/latency-model checks, not device timing predictions.

`run_sd_target_test.sh` verifies the linked Xtensa driver and its import/relocation
boundary. Host/target validation is not a physical battery/SD/USB test.

`run_sd_usb_test.sh` links the actual native copied-text producer, ordinary SD
provider, shared FatFs, MSC owner and production TinyUSB BOT/control stack in one
process. It checks recovered/current boot text, diagnostics appended between
preparation steps, RAM-only wait polls, host read/write/eject CSW, checked local
return and exact later-tail persistence. Cancel and genuine timeout/write/close
failure cases preserve custody without an early PHY claim. Only GPIO/clock,
NVS and packet-controller hardware are modeled; sector times are injected and
are not measurements of the user's card.
