/* Included after the shared volume owner. No second SD stack or public grant.
 * One bounded text chunk is copied/written per owner operation, only while no
 * caller owns a file/directory. Own uncertain writable custody uses the same
 * slot table as ordinary storage so sleep/remount/quiesce all retain it. */
#define X4_BOOTLOG_SD_PATH "/x4-boot.log"
#define X4_BOOTLOG_SD_PREVIOUS "/x4-boot.previous.log"
#define X4_BOOTLOG_SD_MAX_BYTES (512u * 1024u)
static risc_storage_volume_api_v1_export logging_api;
static struct { uint64_t sequence; uint32_t revision; } bootlog_seen[RISC_DIAGNOSTIC_SOURCE_MAX_SLOTS];
static unsigned bootlog_next;
static uint64_t bootlog_cursor;
static bool bootlog_servicing;
static const risc_diagnostic_source_api_v1_trace *bootlog_trace_source(void) {
    if(!diagnostic_source || diagnostic_source->struct_size < RISC_DIAGNOSTIC_SOURCE_TRACE_V1_SIZE)return NULL;
    const risc_diagnostic_source_api_v1_trace *trace=(const risc_diagnostic_source_api_v1_trace *)diagnostic_source;
    return trace->read_after?trace:NULL;
}
static bool bootlog_disabled, bootlog_reporting, bootlog_retained;
static bool bootlog_custody_safe(void) { return !bootlog_retained || bootlog_reporting; }
static const char *bootlog_error;
static char bootlog_text[RISC_DIAGNOSTIC_SOURCE_TEXT_MAX];

static void bootlog_failed(const char *reason) {
    if (!bootlog_error) bootlog_error = reason;
    /* Never repeat an uncertain append. NVS remains the recovery source. */
    bootlog_disabled = true;
}
static bool bootlog_result(FRESULT result, const char *reason) {
    if (result == FR_OK) return true;
    (void)result_ok(result); /* Preserve shared media-integrity/retention rules. */
    bootlog_failed(reason);
    return false;
}
static bool bootlog_step(void) {
    if (bootlog_paused || bootlog_disabled || bootlog_reporting || !diagnostic_source || !started ||
        !mounted || !card_ready || io_failed || gpio_fault || gpio_retained ||
        mutex_poisoned || quiescing || power_down_prepared || power_down_committed ||
        sleep_state != SLEEP_ACTIVE || has_handles()) return false;
    uint32_t count = 0, revision = 0; uint64_t sequence = 0;
    unsigned slot = bootlog_next;
    const risc_diagnostic_source_api_v1_trace *trace=bootlog_trace_source();
    uint64_t next=0;
    if(trace) {
        const int32_t result=trace->read_after(diagnostic_source->context,bootlog_cursor,
            bootlog_text,sizeof(bootlog_text),&count,&next);
        if(result<0 || (result>0 && (!count || count>=sizeof(bootlog_text) ||
           next<=bootlog_cursor || next-bootlog_cursor!=count || bootlog_text[count] ||
           bootlog_text[count-1]!='\n'))) {
            bootlog_failed("trace source invalid; export disabled");return false;
        }
        if(result==0)return false;
    } else
    /* Recover current and flash history in round-robin order. At most nine bounded RAM-only reads, one write. */
    for (unsigned attempt = 0; attempt < RISC_DIAGNOSTIC_SOURCE_MAX_SLOTS; ++attempt) {
        slot = (bootlog_next + attempt) % RISC_DIAGNOSTIC_SOURCE_MAX_SLOTS;
        const int32_t result = diagnostic_source->read(diagnostic_source->context, slot,
            bootlog_text, sizeof(bootlog_text), &count, &sequence, &revision);
        if (result < 0 || (result > 0 && (!count || count >= sizeof(bootlog_text) ||
                !revision || bootlog_text[count] != 0))) {
            bootlog_failed("source invalid; export disabled"); return false;
        }
        if (result > 0 && (bootlog_seen[slot].sequence != sequence ||
                          bootlog_seen[slot].revision != revision)) break;
        count = 0;
    }
    if (!count) return false;
    bool exported = false;
    char caller_error[sizeof(error)]; memcpy(caller_error, error, sizeof(error));
    /* A separate bounded volume operation; caller handles and their positions,
     * errors, generation numbers and commit/abort ownership stay untouched. */
    FILINFO info;
    FRESULT result = f_stat(X4_BOOTLOG_SD_PATH, &info);
    if (result != FR_OK && result != FR_NO_FILE) {
        (void)bootlog_result(result, "stat failed; export disabled"); goto finish;
    }
    if (result == FR_OK && (info.fattrib & AM_DIR)) {
        bootlog_failed("log path is a directory; export disabled"); goto finish;
    }
    if (result == FR_OK && info.fsize > X4_BOOTLOG_SD_MAX_BYTES - count) {
        result = f_stat(X4_BOOTLOG_SD_PREVIOUS, &info);
        if (result == FR_OK && (info.fattrib & AM_DIR)) {
            bootlog_failed("rotation path is a directory; export disabled"); goto finish;
        }
        if (result == FR_OK) {
            if (!bootlog_result(f_unlink(X4_BOOTLOG_SD_PREVIOUS), "rotation remove failed; export disabled")) goto finish;
        } else if (result != FR_NO_FILE) {
            (void)bootlog_result(result, "rotation stat failed; export disabled"); goto finish;
        }
        if (!bootlog_result(f_rename(X4_BOOTLOG_SD_PATH, X4_BOOTLOG_SD_PREVIOUS),
                            "rotation rename failed; export disabled")) goto finish;
    }
    /* Claim an ordinary owned writable slot before any fallible file action.
     * It is never handed to the caller and never displaces a caller's object. */
    file_slot *file = &files[0];
    const uint32_t handle = allocate_handle(0);
    if (!handle) { bootlog_failed("handle budget exhausted; export disabled"); goto finish; }
    result = f_open(&file->object, X4_BOOTLOG_SD_PATH, FA_WRITE | FA_OPEN_ALWAYS);
    if (!bootlog_result(result, "open denied/full/failed; export disabled")) goto finish;
    file->handle = handle; file->flags = RISC_STORAGE_OPEN_WRITE;
    file->error = 0; file->abortable = false;
    memcpy(file->path, X4_BOOTLOG_SD_PATH, sizeof(X4_BOOTLOG_SD_PATH));
    bool complete = bootlog_result(f_lseek(&file->object, f_size(&file->object)),
                                   "append seek failed; export disabled");
    if (complete) {
        UINT written = 0;
        result = f_write(&file->object, bootlog_text, count, &written);
        complete = bootlog_result(result, "write failed; export disabled");
        if (written != count) { bootlog_failed("partial write/full; export disabled"); complete = false; }
    }
    /* f_close performs FatFs sync; no acknowledgment before checked close.
     * Failure retains this exact FIL/slot and blocks shutdown and sleep. */
    result = f_close(&file->object);
    if (bootlog_result(result, "close/sync failed; writable log retained")) {
        file->handle = 0;
        if (complete) {
            exported = true;
            if(trace)bootlog_cursor=next;
            else {
                bootlog_seen[slot].sequence = sequence; bootlog_seen[slot].revision = revision;
                bootlog_next = (slot + 1u) % RISC_DIAGNOSTIC_SOURCE_MAX_SLOTS;
            }
        }
    } else {
        file->error = result; io_failed = true; mounted = false; bootlog_retained = true;
    }
finish:
    /* The private status callback reports log failure beside the caller error.
     * Logging never erases an existing caller's diagnostic or handle error. */
    memcpy(error, caller_error, sizeof(error));
    return exported;
}
static void bootlog_drain(void) {
    if (bootlog_servicing || bootlog_paused || !clock_api || !started || !mounted || bootlog_reporting || bootlog_disabled ||
        power_down_prepared || power_down_committed || sleep_state != SLEEP_ACTIVE || has_handles()) return;
    unsigned limit = bootlog_trace_source()?1u:(bootlog_mount_pending ? RISC_DIAGNOSTIC_SOURCE_MAX_SLOTS : 1u);
    bootlog_mount_pending = false;
    /* One shared operation budget for the entire initial batch: at most nine
     * 1535-byte append/close pairs (13,815 payload bytes), 81 RAM source reads,
     * 2048 physical sectors and the existing 15-second checkpoint deadline.
     * An in-flight bounded sector may complete after the checked deadline. */
    operation_start = clock_api->monotonic_ms(clock_api->context);
    operation_steps = operation_sectors = 0;
    while (limit-- && bootlog_step()) {}
}
static void bootlog_copy(char *out, size_t capacity, size_t *used, const char *text) {
    if (!text) return;
    while (*text && *used + 1u < capacity) out[(*used)++] = *text++;
    out[*used] = 0;
}
static bool bootlog_descriptor_error(char *out, size_t capacity) {
    if (!out || !capacity) return false;
    size_t used = 0; out[0] = 0;
    bootlog_copy(out, capacity, &used, error);
    if (bootlog_error) {
        if (used) bootlog_copy(out, capacity, &used, "; ");
        bootlog_copy(out, capacity, &used, "boot-log: ");
        bootlog_copy(out, capacity, &used, bootlog_error);
        if (bootlog_retained) bootlog_copy(out, capacity, &used, "; writable log retained");
    }
    return used != 0;
}
static bool bootlog_last_error(void *context, char *out, size_t capacity) {
    (void)context;
    if (!out || !capacity || !enter_lifecycle()) return false;
    bootlog_reporting = true;
    const bool present = bootlog_descriptor_error(out, capacity);
    const bool okay = leave(); bootlog_reporting = false;
    return okay && present;
}

/* Explicit synchronous owner service. This is not an 8-ms poll callback.
 * Source probing is RAM-only. No pending bytes means no mutex or storage I/O.
 * Each service appends/closes at most one complete <=1535-byte chunk, with one
 * shared deadline/64-sector budget and the existing uncertain-close custody. */
static void bootlog_service(uint32_t budget_ms) {
    if(!budget_ms || budget_ms>RISC_DRIVER_SERVICE_MAX_MS || !valid_task() ||
       !bootlog_trace_source() || bootlog_disabled || bootlog_paused || bootlog_servicing ||
       !started || !mounted || !card_ready || io_failed || quiescing ||
       power_down_prepared || power_down_committed || sleep_state!=SLEEP_ACTIVE || has_handles())return;
    char probe[RISC_DIAGNOSTIC_SOURCE_TEXT_MAX];uint32_t count=0;uint64_t next=0;
    const risc_diagnostic_source_api_v1_trace *trace=bootlog_trace_source();
    const int32_t status=trace->read_after(diagnostic_source->context,bootlog_cursor,
        probe,sizeof(probe),&count,&next);
    if(status==0)return;
    if(status<0){bootlog_failed("trace service source invalid; export disabled");return;}
    if(!enter_lifecycle())return;
    bootlog_servicing=true;
    bootlog_budget_ms=budget_ms;bootlog_sector_limit=64;
    (void)bootlog_step();
    bootlog_budget_ms=15000;bootlog_sector_limit=2048;
    (void)leave();bootlog_servicing=false;
}
