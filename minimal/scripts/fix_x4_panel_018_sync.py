#!/usr/bin/env python3
"""Repair 0.1.18 differential DTM1 sync and powered-retention admission."""
from pathlib import Path

path = Path("minimal/drivers/x4pro_uc8279_fast/driver.c")
text = path.read_text()

old = """                } else if (async_stage == UC_ASYNC_SYNC) {
                    settle_coverage_valid = false;
                    if (!fast_update) remember_completed_frame();
                    dtm1_synced = true;
"""
new = """                } else if (async_stage == UC_ASYNC_SYNC) {
                    /* Differential OLD synchronization must use the same PTIN
                     * window and RAM cursor as the NEW upload. Close PTIN only
                     * after DTM1 contains the physically completed target. */
                    if (fast_update) { command(0x92); if (io_failed) goto failed; }
                    settle_coverage_valid = false;
                    if (!fast_update) remember_completed_frame();
                    dtm1_synced = true;
"""
if text.count(old) != 1:
    raise SystemExit("OLD-sync completion block not found exactly once")
text = text.replace(old, new, 1)

old = """            if (fast_update) {
                command(0x92); if (io_failed) goto failed;
                remember_completed_frame();
"""
new = """            if (fast_update) {
                /* Absolute updates end PTIN here because they intentionally do
                 * not synchronize OLD. Differential updates retain PTIN until
                 * the matching DTM1 window has been copied. */
                if (absolute_update) { command(0x92); if (io_failed) goto failed; }
                remember_completed_frame();
"""
if text.count(old) != 1:
    raise SystemExit("refresh-completion PTIN block not found exactly once")
text = text.replace(old, new, 1)

old = """    if (shutdown_stage == 0u) {
        const bool busy = !panel_pin_read(X4PRO_PIN_EPD_BUSY);
        if (io_failed) return RISC_DISPLAY_POWER_RETAINED;
        if (busy) return RISC_DISPLAY_POWER_BUSY;
        started = false; previous_seeded = completed_history = dtm1_synced = false;
        absolute_frames = 0; absolute_started_ms = UINT64_MAX;
        if (screen_powered) {
            /* No POF/DSLP while an established image is on glass. Returning
             * BUSY keeps the device awake rather than entering the observed
             * unpowered relaxation state. */
            return RISC_DISPLAY_POWER_BUSY;
        } else shutdown_stage = 2u;
    }
"""
new = """    if (shutdown_stage == 0u) {
        const bool busy = !panel_pin_read(X4PRO_PIN_EPD_BUSY);
        if (io_failed) return RISC_DISPLAY_POWER_RETAINED;
        if (busy) return RISC_DISPLAY_POWER_BUSY;
        /* Refuse sleep before invalidating any live state. A BUSY result must
         * leave started/history/planes intact so normal rendering can continue. */
        if (screen_powered) return RISC_DISPLAY_POWER_BUSY;
        started = false; previous_seeded = completed_history = dtm1_synced = false;
        absolute_frames = 0; absolute_started_ms = UINT64_MAX;
        shutdown_stage = 2u;
    }
"""
if text.count(old) != 1:
    raise SystemExit("powered-retention prepare block not found exactly once")
text = text.replace(old, new, 1)

path.write_text(text)
