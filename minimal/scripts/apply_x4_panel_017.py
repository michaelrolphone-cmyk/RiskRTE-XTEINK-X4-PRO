#!/usr/bin/env python3
"""Build panel 0.1.17 directly from the exact 0.1.12 source cohort."""
from pathlib import Path
import datetime
import hashlib
import json


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:120]!r}")
    path.write_text(text.replace(old, new, 1))


driver = Path("minimal/drivers/x4pro_uc8279_fast/driver.c")
replace_once(
    driver,
    "#define X4PRO_FINAL_TARGET_FRAMES 4u\n",
    "#define X4PRO_FINAL_TARGET_FRAMES 4u\n"
    "#define X4PRO_ACTIVE_LOCAL_FRAMES 3u\n"
    "#define X4PRO_ACTIVE_BROAD_FRAMES 2u\n"
    "#define X4PRO_ACTIVE_LOCAL_MAX_ROWS 160u\n",
)
replace_once(
    driver,
    "    settle_stage = absolute_update ? SETTLE_READY : SETTLE_WAIT;\n",
    "    /* FastEPD's useful behavior is a bounded number of drive passes, not\n"
    "     * indefinite replay of a completed animation target. Each accepted\n"
    "     * 0.1.17 frame receives its complete active pulse before completion;\n"
    "     * normal quiet time then waits for the exact-target finalizer. An\n"
    "     * explicit request_settle may still promote WAIT to READY. */\n"
    "    settle_stage = SETTLE_WAIT;\n",
)
replace_once(
    driver,
    "    if (fast_update) {\n"
    "        /* Hardware 0.1.52/0.1.53 testing showed that one-frame differential\n"
    "         * DEFAULT updates were visibly under-driven. Restore the known-working\n"
    "         * absolute motion path for both interactive intents. DTM1 is reconciled\n"
    "         * only after the target-ending full-frame redraw. */\n"
    "        absolute_update = true;\n"
    "    }\n",
    "    if (fast_update) {\n"
    "        /* Preserve the 0.1.12 full-width absolute-A2 band model. Localized\n"
    "         * motion receives three target-directed scan frames, matching the\n"
    "         * proven FastEPD per-pixel drive-pass count; broad motion receives\n"
    "         * two frames to avoid collapsing full-screen cadence. */\n"
    "        absolute_update = true;\n"
    "    }\n",
)
replace_once(
    driver,
    "    if (fast_update) {\n"
    "        /* Use the source's tested gate-window heights. Preserve completed\n"
    "         * pixels in both dimensions when damage needs a wider/taller band. */\n"
    "        update_area = tested_window((uint32_t)update_area.y, (uint32_t)update_area.y + update_area.height);\n"
    "    } else if (!quality_partial) update_area = (risc_display_rect_v1){0, 0, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT};\n",
    "    if (fast_update) {\n"
    "        /* Keep the exact 0.1.12 full-width 40/80/160/480-row windows. */\n"
    "        update_area = tested_window((uint32_t)update_area.y, (uint32_t)update_area.y + update_area.height);\n"
    "        fast_lut_frames = update_area.height <= X4PRO_ACTIVE_LOCAL_MAX_ROWS ?\n"
    "            X4PRO_ACTIVE_LOCAL_FRAMES : X4PRO_ACTIVE_BROAD_FRAMES;\n"
    "    } else if (!quality_partial) update_area = (risc_display_rect_v1){0, 0, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT};\n",
)
replace_once(
    driver,
    "    out->nominal_refresh_millihz = 10000; /* Selected lab mode scheduling hint. */\n"
    "    out->typical_present_latency_us = 100000; /* Integrated timing remains unqualified. */\n",
    "    out->nominal_refresh_millihz = 11000; /* Three-frame 160-row lab profile. */\n"
    "    out->typical_present_latency_us = 90000; /* 89.1 ms measured reference. */\n",
)
replace_once(driver, 'append(destination, capacity, &used, "v=0.1.12 cause=");',
             'append(destination, capacity, &used, "v=0.1.17 cause=");')

manifest = Path("minimal/drivers/x4pro_uc8279_fast/manifest.json")
replace_once(manifest, '"version": "0.1.12"', '"version": "0.1.17"')

readme = Path("minimal/drivers/x4pro_uc8279_fast/README.md")
text = readme.read_text()
if text.startswith("# X4 UC8279 fast provider 0.1.12"):
    text = text.replace("# X4 UC8279 fast provider 0.1.12", "# X4 UC8279 fast provider 0.1.17", 1)
else:
    raise SystemExit("unexpected README heading")
section = """
## 0.1.12-based bounded three-pass experiment (0.1.17)

This candidate is based directly on the exact 0.1.12 source cohort. It keeps
0.1.12's full-width 40/80/160/480-row absolute-A2 windows, electrical profile,
controller setup, endpoint redraw, dual-plane reconciliation, and POF lifecycle.
It does not import the later narrow-horizontal-window or differential-overdrive
implementations.

Interactive updates no taller than 160 rows receive one three-frame absolute
DRF. Broad 480-row motion receives one two-frame absolute DRF. A normal
completed frame enters a quiet wait rather than repeatedly replaying the same
intermediate animation target; after the 2.3-second quiet interval, the existing
four-frame exact-target finalizer runs. Explicit lifecycle settling can still
promote the wait state when required.

The three-pass count deliberately mirrors the useful part of the direct FastEPD
video implementation: changed pixels retain drive debt until three scans have
completed. Grayscale dithering remains a producer responsibility because this
provider receives an already quantized MONO1 frame. The current GameBoy renderer
uses a static 2x2 Bayer mapping unless GBEMU_FAST_MONO is explicitly enabled.

"""
readme.write_text(text + section)

profile = Path("minimal/test/uc8279_fast_profile_test.py")
if profile.exists():
    ptext = profile.read_text().replace("0.1.12", "0.1.17")
    profile.write_text(ptext)

validation_path = Path("minimal/validation/uc8279-012-three-pass-0.1.17.json")
tracked = [driver, manifest, readme]
if profile.exists():
    tracked.append(profile)
validation = {
    "schema": "x4.uc8279.012-three-pass-validation",
    "schema_version": 1,
    "created_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "baseline": {
        "commit": "c081eebddad9ac61740b758e3fa74e5c92e325b1",
        "driver_version": "0.1.12",
        "selection_reason": "best overall hardware baseline reported after later driver regressions",
    },
    "candidate": {"driver_version": "0.1.17"},
    "active_policy": {
        "drive": "absolute A2",
        "window_model": "0.1.12 full-width 40/80/160/480-row bands",
        "local_frames": 3,
        "local_max_rows": 160,
        "broad_frames": 2,
        "normal_resident_replays": 0,
        "quiet_interval_ms": 2300,
        "final_target_frames": 4,
    },
    "dither_findings": {
        "panel_input": "MONO1; grayscale information is unavailable in the driver",
        "gameboy": "static 2x2 Bayer is enabled unless GBEMU_FAST_MONO is defined",
        "fastepd": "per-pixel state tracks three completed drive passes and stops settled pixels",
        "decision": "preserve producer Bayer and test bounded three-pass drive before adding transition-mask dithering",
    },
    "electrical_changes": "none",
    "integrated_driver_hardware_tested": False,
    "files_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked},
}
validation_path.write_text(json.dumps(validation, indent=2) + "\n")
