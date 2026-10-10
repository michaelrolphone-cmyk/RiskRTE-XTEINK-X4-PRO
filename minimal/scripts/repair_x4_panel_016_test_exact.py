#!/usr/bin/env python3
"""Restore the exact 0.1.13 UC8279 fixture and apply only 0.1.16 expectations."""
from pathlib import Path
import subprocess

BASE = "d5fdc8bae639a216340b4b5cbee49b1167d6eb69"
PATH = Path("minimal/test/uc8279_fast_test.c")
PATH.write_bytes(subprocess.check_output(["git", "show", f"{BASE}:{PATH}"]))
text = PATH.read_text()

# All six localized-window expectations move from two to four absolute frames.
if text.count("fast_lut_frames==2") != 6:
    raise SystemExit(f"unexpected localized expectation count: {text.count('fast_lut_frames==2')}")
text = text.replace("fast_lut_frames==2", "fast_lut_frames==4")

# An identical target performs no panel I/O and retains the initialization
# sentinel of one frame. A real broad 480-row transition now uses two frames.
old_polarity = (
    "  assert(same?(settle_stage==settle_before&&fast_lut_frames==1u):\n"
    "              (update_area.x==0&&update_area.y==0&&update_area.width==800&&update_area.height==480&&fast_lut_frames==1u));\n"
)
new_polarity = (
    "  assert(same?(settle_stage==settle_before&&fast_lut_frames==1u):\n"
    "              (update_area.x==0&&update_area.y==0&&update_area.width==800&&update_area.height==480&&fast_lut_frames==2u));\n"
)
if text.count(old_polarity) != 1:
    raise SystemExit("exact no-change/broad polarity expectation not found")
text = text.replace(old_polarity, new_polarity, 1)

old_broad = (
    "assert(bytes_sent==48000&&fast_lut_frames==1u&&update_area.x==0&&update_area.y==0&&"
    "update_area.width==800&&update_area.height==480&&commands[0x10]==old_sync&&"
    "visible[0]==0xCC&&visible[47999]==0xCC);"
)
new_broad = old_broad.replace("fast_lut_frames==1u", "fast_lut_frames==2u")
if text.count(old_broad) != 1:
    raise SystemExit("exact broad-frame expectation not found")
text = text.replace(old_broad, new_broad, 1)

old_metadata = "assert(info.nominal_refresh_millihz==10000&&info.typical_present_latency_us==100000);"
new_metadata = "assert(info.nominal_refresh_millihz==9000&&info.typical_present_latency_us==110000);"
if text.count(old_metadata) != 1:
    raise SystemExit("cadence metadata expectation not found")
text = text.replace(old_metadata, new_metadata, 1)

PATH.write_text(text)
