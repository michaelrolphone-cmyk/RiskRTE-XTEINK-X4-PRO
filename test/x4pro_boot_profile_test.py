#!/usr/bin/env python3
"""Verify the staged X4 board profile selects every bundled board driver."""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SD = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "dist/x4-independent-packages/sdcard"

EXPECTED = [
    "platform-clock-v1",
    "x4pro-panel",
    "x4pro-buttons",
    "x4pro-frontlight",
    "x4pro-sd",
    "x4pro-i2c",
    "x4pro-gt911",
    "x4pro-battery",
    "x4pro-rtc",
]

boot_path = SD / "System/Config/boot.json"
board_path = SD / "System/Config/board.json"
assert boot_path.is_file(), boot_path
assert board_path.is_file(), board_path

boot = json.loads(boot_path.read_text())
board = json.loads(board_path.read_text())
assert board.get("board_id") == "xteink-x4-pro"
assert boot.get("board") == "System/Config/board.json"

selected = []
for entry in boot.get("drivers", []):
    manifest = entry.get("manifest", "")
    prefix, suffix = "Drivers/", "/manifest.json"
    assert manifest.startswith(prefix) and manifest.endswith(suffix), manifest
    identity = manifest[len(prefix):-len(suffix)]
    assert identity and "/" not in identity
    assert (SD / manifest).is_file(), manifest
    selected.append(identity)

installed = sorted(p.name for p in (SD / "Drivers").iterdir() if p.is_dir())
assert selected == EXPECTED, (selected, EXPECTED)
assert sorted(selected) == installed, (selected, installed)

# These two physical X4 devices used to be deliberately omitted and therefore
# depended on generic lazy discovery. They must remain board-selected so later
# unrelated provider-discovery changes cannot remove battery/RTC telemetry.
assert "x4pro-battery" in selected
assert "x4pro-rtc" in selected
assert selected.index("platform-clock-v1") < selected.index("x4pro-i2c")
assert selected.index("x4pro-i2c") < selected.index("x4pro-battery")
assert selected.index("x4pro-i2c") < selected.index("x4pro-rtc")

print("X4 boot profile: all 9 bundled board drivers selected; battery/RTC no longer lazy-only PASS")
