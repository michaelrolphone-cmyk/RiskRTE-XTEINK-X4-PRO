#!/usr/bin/env python3
"""Restore the exact 0.1.13 test fixture, then apply only 0.1.16 expectations."""
from pathlib import Path
import subprocess

BASE = "d5fdc8bae639a216340b4b5cbee49b1167d6eb69"
path = Path("minimal/test/uc8279_fast_test.c")
path.write_bytes(subprocess.check_output(["git", "show", f"{BASE}:{path}"]))
text = path.read_text()

if text.count("fast_lut_frames==2") != 6:
    raise SystemExit(f"unexpected localized assertion count: {text.count('fast_lut_frames==2')}")
text = text.replace("fast_lut_frames==2", "fast_lut_frames==4")

if text.count("fast_lut_frames==1u") != 3:
    raise SystemExit(f"unexpected broad assertion count: {text.count('fast_lut_frames==1u')}")
text = text.replace("fast_lut_frames==1u", "fast_lut_frames==2u")

old = "assert(info.nominal_refresh_millihz==10000&&info.typical_present_latency_us==100000);"
new = "assert(info.nominal_refresh_millihz==9000&&info.typical_present_latency_us==110000);"
if text.count(old) != 1:
    raise SystemExit("cadence metadata assertion missing")
text = text.replace(old, new)
path.write_text(text)
