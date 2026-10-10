#!/usr/bin/env python3
"""Align X4 UC8279 provider regressions/documentation with panel 0.1.13."""
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"missing expected {label}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str, flags: int = 0) -> str:
    updated, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"expected one {label}, found {count}")
    return updated


# The provider model must understand a byte-aligned, source-narrow partial RAM
# window and the retained four-frame endpoint redraw.
path = "minimal/test/uc8279_fast_test.c"
s = read(path)
if "final_target_refreshes" not in s:
    s = replace_once(
        s,
        "refreshes, pons, pofs, sleeps, polls;",
        "refreshes, final_target_refreshes, pons, pofs, sleeps, polls;",
        "final-target counter declaration",
    )
s = regex_once(
    s,
    r"const uint8_t frames=regs\[0x20\]\[1\];assert\(frames==1\|\|frames==2(?:\|\|frames==X4PRO_FINAL_TARGET_FRAMES)?\);(?:\n\s*if\(frames==X4PRO_FINAL_TARGET_FRAMES\)\+\+final_target_refreshes;)?",
    "const uint8_t frames=regs[0x20][1];assert(frames==2||frames==X4PRO_FINAL_TARGET_FRAMES);\n"
    "   if(frames==X4PRO_FINAL_TARGET_FRAMES)++final_target_refreshes;",
    "A2 frame-count oracle",
)
fast_height = """   height=(((unsigned)regs[0x90][6]<<8)|regs[0x90][7])-120-top+1;
  }else {"""
fast_height_new = """   height=(((unsigned)regs[0x90][6]<<8)|regs[0x90][7])-120-top+1;
   left=((unsigned)regs[0x90][0]<<8)|regs[0x90][1];
   width=(((unsigned)regs[0x90][2]<<8)|regs[0x90][3])-left+1;
   assert(!(left&7u)&&!(width&7u)&&width>=X4PRO_FAST_MIN_WIDTH&&left+width<=800);
  }else {"""
if "width>=X4PRO_FAST_MIN_WIDTH" not in s:
    s = replace_once(s, fast_height, fast_height_new, "fast partial-window geometry")
s = s.replace(
    "for(unsigned y=top;y<top+height;++y)for(unsigned x=0;x<100;++x){",
    "for(unsigned y=top;y<top+height;++y)for(unsigned x=left/8;x<(left+width)/8;++x){",
    1,
)
old_cursor = "if(ptin){assert(regs[0x90][0]==0&&regs[0x90][1]==0&&regs[0x90][2]==3&&regs[0x90][3]==0x1F);pos+=(((unsigned)regs[0x90][4]<<8)|regs[0x90][5])*100;}"
new_cursor = "if(ptin){unsigned left=((unsigned)regs[0x90][0]<<8)|regs[0x90][1];unsigned width=(((unsigned)regs[0x90][2]<<8)|regs[0x90][3])-left+1;unsigned row=width/8;assert(!(left&7u)&&!(width&7u)&&row);pos=((((unsigned)regs[0x90][4]<<8)|regs[0x90][5])+(data_index/row))*100+left/8+(data_index%row);}"
if old_cursor in s:
    s = s.replace(old_cursor, new_cursor, 1)
elif new_cursor not in s:
    raise SystemExit("missing partial RAM cursor model")

# Active fast updates now transfer a qualified 96-pixel source window and use
# a two-frame absolute waveform.  The test rectangles below all resolve to the
# 96-pixel minimum, or 12 bytes per row.
s = s.replace("bytes_sent==100*h", "bytes_sent==12*h")
s = s.replace("m.effective_update.width==800", "m.effective_update.width==96")
s = s.replace("bytes_sent==4000", "bytes_sent==480")
s = s.replace("bytes_sent==64000", "bytes_sent==480")

old_maintenance = """  const unsigned old1=commands[0x10],old2=commands[0x13],off=pofs;
  uint64_t token=submit_default(f,&damage,1);complete_frame(token);
  assert(fast_update&&!absolute_update&&!settle_update&&dtm1_synced);
  assert(commands[0x10]==old1+1&&commands[0x13]==old2+1&&settle_stage==SETTLE_WAIT);
  drain_settle();assert(!screen_powered&&pofs==off+1&&dtm1_synced);"""
new_maintenance = """  const unsigned old1=commands[0x10],old2=commands[0x13],off=pofs,finals=final_target_refreshes;
  uint64_t token=submit_default(f,&damage,1);complete_frame(token);
  assert(fast_update&&absolute_update&&!settle_update&&!dtm1_synced&&fast_lut_frames==2);
  assert(commands[0x10]==old1&&commands[0x13]==old2+1&&settle_stage==SETTLE_READY);
  drain_settle();assert(!screen_powered&&pofs==off+1&&dtm1_synced&&final_target_refreshes==finals+1);"""
if old_maintenance in s:
    s = s.replace(old_maintenance, new_maintenance, 1)
else:
    # Accept the already-0.1.12 form and strengthen its active pulse assertion.
    s = s.replace("off=pofs,finals=final_target_refreshes;", "off=pofs,finals=final_target_refreshes;", 1)
    s = s.replace("!dtm1_synced&&fast_lut_frames==1", "!dtm1_synced&&fast_lut_frames==2", 1)

old_burst = """ assert(absolute_update&&settle_update&&fast_lut_frames==2&&dtm1_synced&&absolute_frames==0);
 assert(bytes_sent==480&&settle_stage==SETTLE_WAIT);drain_settle();assert(!screen_powered);assert(d->quiesce());"""
new_burst = """ assert(absolute_update&&!settle_update&&fast_lut_frames==2&&!dtm1_synced&&absolute_frames==17);
 assert(bytes_sent==480&&settle_stage==SETTLE_READY);drain_settle();assert(!screen_powered&&dtm1_synced);assert(d->quiesce());"""
if old_burst in s:
    s = s.replace(old_burst, new_burst, 1)
else:
    s = s.replace(
        "assert(absolute_update&&!settle_update&&fast_lut_frames==1&&!dtm1_synced&&absolute_frames==17);",
        "assert(absolute_update&&!settle_update&&fast_lut_frames==2&&!dtm1_synced&&absolute_frames==17);",
        1,
    )
    s = s.replace(
        "assert(bytes_sent==4000&&settle_stage==SETTLE_READY);",
        "assert(bytes_sent==480&&settle_stage==SETTLE_READY);",
        1,
    )

s = s.replace("payload==bytes+4000+120000", "payload==bytes+480+180000")
s = s.replace("payload==bytes+4000+180000", "payload==bytes+480+180000")
s = s.replace(
    "payload==bytes+120000&&commands[0x10]>=2&&commands[0x13]>=2",
    "payload==bytes+180000&&commands[0x10]>=2&&commands[0x13]>=3&&final_target_refreshes>=1",
)
s = s.replace(
    "payload==bytes+180000&&commands[0x10]>=2&&commands[0x13]>=3&&final_target_refreshes>=1&&final_target_refreshes>=1",
    "payload==bytes+180000&&commands[0x10]>=2&&commands[0x13]>=3&&final_target_refreshes>=1",
)
# Strengthen the multi-rectangle assertion so it proves source narrowing.
s = s.replace(
    "assert(bytes_sent==480&&update_area.y==440&&visible[47801]==0x88&&visible[47905]==0x88);",
    "assert(bytes_sent==480&&update_area.x==0&&update_area.width==96&&update_area.y==440&&visible[47801]==0x88&&visible[47905]==0x88);",
)
required = [
    "frames==2||frames==X4PRO_FINAL_TARGET_FRAMES",
    "width>=X4PRO_FAST_MIN_WIDTH",
    "bytes_sent==12*h",
    "m.effective_update.width==96",
    "fast_lut_frames==2",
    "payload==bytes+480+180000",
]
for marker in required:
    if marker not in s:
        raise SystemExit(f"patched provider test lacks {marker}")
write(path, s)

# Version-bound host/profile assertions follow the source package when those
# optional test files exist in this recovered branch.
for path in (
    "minimal/test/run_idle_panel_policy_test.py",
    "minimal/test/uc8279_fast_profile_test.py",
):
    candidate = ROOT / path
    if not candidate.exists():
        continue
    text = candidate.read_text(encoding="utf-8")
    text = re.sub(r"0\.1\.(?:9|10|11|12)", "0.1.13", text)
    candidate.write_text(text, encoding="utf-8")

idle_policy = ROOT / "minimal/test/idle_panel_policy.inc"
if idle_policy.exists():
    text = idle_policy.read_text(encoding="utf-8")
    text = re.sub(r"fast0\.1\.(?:9|10|11|12)", "fast0.1.13", text)
    idle_policy.write_text(text, encoding="utf-8")

# Keep the driver note on the same branch as the source instead of another
# local-only patch archive.
path = "minimal/drivers/x4pro_uc8279_fast/README.md"
s = read(path)
s = re.sub(r"^# X4 UC8279 fast provider .*?$", "# X4 UC8279 fast provider 0.1.13", s, count=1, flags=re.M)
section = """

## Delta-window, multi-pulse motion (0.1.13)

Active DEFAULT and LOW_LATENCY presentations compare the submitted MONO1 target
with the last physically completed target. The provider constrains that delta by
any explicit application damage, then selects the smallest qualified 96-pixel
source window and 40/80/160/480-row gate band containing the changed bytes.
Legacy applications that submit no damage, including Game Boy, receive the same
delta discovery across the complete framebuffer.

Each active target uses a two-frame absolute A2 waveform instead of the prior
one-frame pulse. Expanded window padding and gaps retain the previous target;
only reported/detected changes are committed to completed history. The existing
2.3-second resident settling, four-frame full-target endpoint redraw, complete
DTM1/DTM2 reconciliation, validated POF, profile replay, and fault invalidation
remain unchanged.
"""
if "Delta-window, multi-pulse motion (0.1.13)" not in s:
    s += section
write(path, s)

manifest = json.loads(read("minimal/drivers/x4pro_uc8279_fast/manifest.json"))
if manifest.get("version") != "0.1.13":
    raise SystemExit("panel manifest is not 0.1.13")
print("UC8279 0.1.13 tests and documentation aligned")
