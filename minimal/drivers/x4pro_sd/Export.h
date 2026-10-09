/* Included after the shared volume and private logger. All operations use the
 * same nonrecursive owner-task guard and synchronous native SD transport. */
static int32_t export_retain(const char *reason) {
    export_state = EXPORT_RETAINED;
    bootlog_paused = true;
    io_failed = true;
    mounted = false;
    if (reason) fail(reason);
    return RISC_STORAGE_EXPORT_RETAINED;
}
static int32_t export_admission_failure(void) {
    /* Wrong-task/reentrant callers cannot inspect or mutate owned state. */
    return valid_task() && (mutex_poisoned || gpio_retained)
        ? RISC_STORAGE_EXPORT_RETAINED : RISC_STORAGE_EXPORT_REFUSED;
}
static int32_t export_leave(int32_t result) {
    return leave() ? result : export_retain("export unlock/log custody retained");
}
static int32_t export_begin(void *context, risc_storage_export_token_t *token,
                            uint64_t *blocks, uint32_t *block_size) {
    (void)context;
    if (!token || !blocks || !block_size) return RISC_STORAGE_EXPORT_REFUSED;
    *token = 0; *blocks = 0; *block_size = 0;
    if (!enter_lifecycle()) return export_admission_failure();
    if (export_state == EXPORT_RETAINED || bootlog_retained)
        return export_leave(export_retain("export custody retained"));
    if (export_state != EXPORT_LOCAL || !started || power_down_prepared ||
        power_down_committed || sleep_state != SLEEP_ACTIVE || has_handles() ||
        !card_ready || !card_block_count || export_generation == UINT64_MAX)
        return export_leave(RISC_STORAGE_EXPORT_REFUSED);
    if (io_failed || !transport_idle())
        return export_leave(export_retain("export media custody uncertain"));
    /* Flush every available diagnostic slot once, bounded by the existing
     * mount batch budget. Never retry an append already marked uncertain. */
    bootlog_mount_pending = true;
    bootlog_drain();
    bootlog_paused = true;
    if (bootlog_retained || has_handles() || io_failed || !transport_idle())
        return export_leave(export_retain("export boot-log flush retained"));
    export_state = EXPORT_HOST; /* Freeze all local admission before transition. */
    export_token = ++export_generation;
    *token = export_token;
    if (!sync_card()) return export_leave(export_retain("export media sync retained"));
    const FRESULT result = f_mount(NULL, "", 0);
    mounted = false;
    if (result != FR_OK) return export_leave(export_retain("export unmount retained"));
    *blocks = card_block_count;
    *block_size = RISC_STORAGE_EXPORT_BLOCK_SIZE;
    return export_leave(RISC_STORAGE_EXPORT_READY);
}
static int32_t export_check(risc_storage_export_token_t token) {
    if (export_state == EXPORT_RETAINED) return RISC_STORAGE_EXPORT_RETAINED;
    return export_state == EXPORT_HOST && token && token == export_token
        ? RISC_STORAGE_EXPORT_READY : RISC_STORAGE_EXPORT_REFUSED;
}
static bool export_range(uint64_t lba, uint32_t count, const void *buffer) {
    return buffer && count && count <= RISC_STORAGE_EXPORT_BLOCKS_MAX &&
        lba <= UINT32_MAX && lba < card_block_count && count <= card_block_count - lba;
}
static int32_t export_read(void *context, risc_storage_export_token_t token,
                           uint64_t lba, uint32_t count, void *buffer) {
    (void)context;
    if (!enter_lifecycle()) return export_admission_failure();
    const int32_t state = export_check(token);
    if (state != RISC_STORAGE_EXPORT_READY) return export_leave(state);
    if (!export_range(lba, count, buffer)) return export_leave(RISC_STORAGE_EXPORT_REFUSED);
    for (uint32_t i = 0; i < count; ++i)
        if (!disk_budget() || !read_sector((uint32_t)(lba + i), (uint8_t *)buffer + i * 512u))
            return export_leave(export_retain("export read/CRC retained"));
    return export_leave(RISC_STORAGE_EXPORT_READY);
}
static int32_t export_write(void *context, risc_storage_export_token_t token,
                            uint64_t lba, uint32_t count, const void *buffer) {
    (void)context;
    if (!enter_lifecycle()) return export_admission_failure();
    const int32_t state = export_check(token);
    if (state != RISC_STORAGE_EXPORT_READY) return export_leave(state);
    if (!export_range(lba, count, buffer)) return export_leave(RISC_STORAGE_EXPORT_REFUSED);
    for (uint32_t i = 0; i < count; ++i)
        if (!disk_budget() || !write_sector((uint32_t)(lba + i), (const uint8_t *)buffer + i * 512u))
            return export_leave(export_retain("export write uncertain; retained"));
    return export_leave(RISC_STORAGE_EXPORT_READY);
}
static int32_t export_sync(void *context, risc_storage_export_token_t token) {
    (void)context;
    if (!enter_lifecycle()) return export_admission_failure();
    const int32_t state = export_check(token);
    if (state != RISC_STORAGE_EXPORT_READY) return export_leave(state);
    return export_leave(sync_card() ? RISC_STORAGE_EXPORT_READY :
                        export_retain("export sync retained"));
}
static int32_t export_end(void *context, risc_storage_export_token_t token) {
    (void)context;
    if (!enter_lifecycle()) return export_admission_failure();
    const int32_t state = export_check(token);
    if (state != RISC_STORAGE_EXPORT_READY) return export_leave(state);
    if (!sync_card()) return export_leave(export_retain("export return sync retained"));
    /* The host has stopped I/O. Reinitialize/remount rather than reuse cached
     * FAT state. Missing/unformatted media is distinct from uncertain I/O. */
    mounted = card_ready = io_failed = false;
    error[0] = 0;
    (void)init_card();
    if (io_failed || !transport_idle())
        return export_leave(export_retain("export remount retained"));
    const int32_t result = mounted && card_ready
        ? RISC_STORAGE_EXPORT_READY : RISC_STORAGE_EXPORT_MEDIA_UNAVAILABLE;
    export_token = 0;
    export_state = EXPORT_LOCAL;
    bootlog_paused = false;
    return export_leave(result);
}
