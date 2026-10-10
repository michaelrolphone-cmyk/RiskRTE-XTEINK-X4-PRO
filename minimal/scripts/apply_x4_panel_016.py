#!/usr/bin/env python3
"""Restore the hardware-good 0.1.13 absolute path and lengthen its one DRF."""
from pathlib import Path
import datetime
import hashlib
import json
import subprocess

BASE = "d5fdc8bae639a216340b4b5cbee49b1167d6eb69"
RESTORE = (
    "minimal/drivers/x4pro_uc8279_fast/driver.c",
    "minimal/drivers/x4pro_uc8279_fast/README.md",
    "minimal/drivers/x4pro_uc8279_fast/manifest.json",
    "minimal/test/uc8279_fast_test.c",
    "minimal/test/uc8279_fast_profile_test.py",
    "minimal/test/run_uc8279_fast_cadence_test.py",
    "minimal/test/uc8279_fast_cadence/panel_model.c",
    "minimal/test/run_idle_panel_policy_test.py",
    "minimal/test/idle_panel_policy.inc",
)


def restore(path: str) -> None:
    data = subprocess.check_output(["git", "show", f"{BASE}:{path}"])
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:100]!r}")
    path.write_text(text.replace(old, new, 1))


for item in RESTORE:
    restore(item)

driver = Path("minimal/drivers/x4pro_uc8279_fast/driver.c")
replace_once(
    driver,
    "#define X4PRO_STRONG_MOTION_FRAMES 2u\n#define X4PRO_STRONG_MOTION_MAX_ROWS 160u\n",
    "#define X4PRO_STRONG_MOTION_FRAMES 4u\n"
    "#define X4PRO_BROAD_MOTION_FRAMES 2u\n"
    "#define X4PRO_STRONG_MOTION_MAX_ROWS 160u\n",
)
replace_once(
    driver,
    "            fast_lut_frames = update_area.height <= X4PRO_STRONG_MOTION_MAX_ROWS ?\n"
    "                X4PRO_STRONG_MOTION_FRAMES : 1u;\n",
    "            /* One coherent absolute DRF per accepted target. Localized motion\n"
    "             * receives four target-directed scan frames; broad 480-row motion\n"
    "             * receives two. The proven 0.1.13 lifecycle and finalizer remain. */\n"
    "            fast_lut_frames = update_area.height <= X4PRO_STRONG_MOTION_MAX_ROWS ?\n"
    "                X4PRO_STRONG_MOTION_FRAMES : X4PRO_BROAD_MOTION_FRAMES;\n",
)
replace_once(
    driver,
    "    out->nominal_refresh_millihz = 10000; /* Selected lab mode scheduling hint. */\n"
    "    out->typical_present_latency_us = 100000; /* Integrated timing remains unqualified. */\n",
    "    out->nominal_refresh_millihz = 9000; /* Four-frame localized absolute profile. */\n"
    "    out->typical_present_latency_us = 110000; /* Hardware-lab 160-row estimate. */\n",
)

manifest = Path("minimal/drivers/x4pro_uc8279_fast/manifest.json")
replace_once(manifest, '"version": "0.1.13"', '"version": "0.1.16"')

profile_test = Path("minimal/test/uc8279_fast_profile_test.py")
replace_once(profile_test, "manifest['version']=='0.1.13'", "manifest['version']=='0.1.16'")

test = Path("minimal/test/uc8279_fast_test.c")
text = test.read_text()
if text.count("fast_lut_frames==2") != 6:
    raise SystemExit(f"unexpected 0.1.13 two-frame assertion count: {text.count('fast_lut_frames==2')}")
text = text.replace("fast_lut_frames==2", "fast_lut_frames==4")
if text.count("fast_lut_frames==1u") != 1:
    raise SystemExit(f"unexpected broad one-frame assertion count: {text.count('fast_lut_frames==1u')}")
text = text.replace("fast_lut_frames==1u", "fast_lut_frames==2u")
test.write_text(text)

idle_runner = Path("minimal/test/run_idle_panel_policy_test.py")
text = idle_runner.read_text()
text = text.replace("fast0.1.13", "fast0.1.16")
text = text.replace("['version']=='0.1.13'", "['version']=='0.1.16'")
idle_runner.write_text(text)

readme = Path("minimal/drivers/x4pro_uc8279_fast/README.md")
replace_once(readme, "# X4 UC8279 fast provider 0.1.13\n", "# X4 UC8279 fast provider 0.1.16\n")
text = readme.read_text()
insert = """## Longer absolute active drive (0.1.16)\n\nVersion 0.1.16 returns exactly to the hardware-confirmed 0.1.13 absolute-A2\nimplementation and changes only active pulse duration and its scheduling metadata.\nByte-level changed-region detection, byte-aligned horizontal windows, the\n40/80/160/480-row gate choices, absolute OLD-plane policy, resident endpoint\nsettling, four-frame final-target redraw, complete DTM1/DTM2 reconciliation,\nand POF lifecycle are inherited from 0.1.13.\n\nChanged regions no taller than 160 rows now receive one four-frame absolute DRF.\nBroad 480-row motion receives one two-frame absolute DRF. No differential or\ncomplementary LUT is used, no source-rail or VCOM override is programmed, and no\nsecond active DRF is added. The additional time is therefore spent driving the\ncurrent target within one coherent refresh rather than replaying an intermediate\ntransition. The 160-row laboratory measurement for the four-frame profile was\napproximately 100.9 ms total, or 9.91 FPS.\n\n"""
marker = "## Changed-pixel windows and stronger active drive (0.1.13)\n"
if marker not in text:
    raise SystemExit("0.1.13 README marker missing")
readme.write_text(text.replace(marker, insert + marker, 1))

validation_path = Path("minimal/validation/uc8279-absolute4-0.1.16.json")
tracked = [driver, manifest, readme, test, profile_test, idle_runner]
validation = {
    "schema": "x4.uc8279.absolute4-validation",
    "schema_version": 1,
    "created_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "baseline": {
        "commit": BASE,
        "driver_version": "0.1.13",
        "hardware_result": "last good usable product driver; low active-frame contrast",
    },
    "candidate": {"driver_version": "0.1.16"},
    "implementation": {
        "active_path": "absolute A2 only",
        "localized_max_rows": 160,
        "localized_frames": 4,
        "broad_frames": 2,
        "single_drf_per_accepted_target": True,
        "electrical_profile": "unchanged from 0.1.13; no explicit voltage or VCOM override",
        "lifecycle": "restored from exact 0.1.13",
        "unchanged": [
            "byte-level changed-region detection",
            "byte-aligned horizontal windows",
            "40/80/160/480 row quantization",
            "absolute active refresh",
            "resident endpoint settling",
            "four-frame full-target endpoint redraw",
            "complete DTM1/DTM2 reconciliation and POF",
        ],
    },
    "lab_basis": {
        "four_frame_160_row_total_us": 100859,
        "four_frame_160_row_fps": 9.91,
        "five_frame_160_row_total_us": 112586,
        "five_frame_160_row_fps": 8.88,
        "selection": "four frames chosen for the best first test balance of contrast and cadence",
    },
    "software_validation": {
        "status": "pending exact-head GitHub Actions qualification",
        "workflow": ".github/workflows/x4-fast-display.yml",
    },
    "integrated_driver_hardware_tested": False,
    "files_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked},
}
validation_path.write_text(json.dumps(validation, indent=2) + "\n")
