#!/usr/bin/env python3
"""Keep PTIN active through the 0.1.18 differential DTM1 synchronization."""
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
path.write_text(text.replace(old, new, 1))
