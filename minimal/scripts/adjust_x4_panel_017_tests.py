#!/usr/bin/env python3
"""Update only host expectations affected by the 0.1.17 pulse policy."""
from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:120]!r}")
    path.write_text(text.replace(old, new, 1))


test = Path("minimal/test/uc8279_fast_test.c")
replace_once(
    test,
    "const uint8_t frames=regs[0x20][1];assert(frames==1||frames==2||frames==X4PRO_FINAL_TARGET_FRAMES);",
    "const uint8_t frames=regs[0x20][1];assert(frames==1||frames==2||frames==3||frames==X4PRO_FINAL_TARGET_FRAMES);",
)
replace_once(
    test,
    "assert(info.nominal_refresh_millihz==10000&&info.typical_present_latency_us==100000);",
    "assert(info.nominal_refresh_millihz==11000&&info.typical_present_latency_us==90000);",
)
replace_once(
    test,
    "assert(commands[0x10]==old_sync&&bytes_sent==100*h);risc_display_present_metrics_v1 m=snapshot();",
    "assert(commands[0x10]==old_sync&&bytes_sent==100*h&&fast_lut_frames==3);risc_display_present_metrics_v1 m=snapshot();",
)
replace_once(
    test,
    "fast_band(440,40);owner_poll(8);assert(settle_stage==SETTLE_DONE);",
    "fast_band(440,40);owner_poll(8);assert(settle_stage==SETTLE_WAIT);",
)
replace_once(
    test,
    "assert(fast_update&&!quality_partial&&bytes_sent==4000&&commands[0x10]==synced&&settle_stage==SETTLE_READY&&visible[47999]==0xAA);",
    "assert(fast_update&&!quality_partial&&bytes_sent==4000&&commands[0x10]==synced&&fast_lut_frames==3&&settle_stage==SETTLE_WAIT&&visible[47999]==0xAA);",
)
replace_once(
    test,
    "assert(fast_update&&absolute_update&&!settle_update&&!dtm1_synced&&fast_lut_frames==1);\n"
    "  assert(commands[0x10]==old1&&commands[0x13]==old2+1&&settle_stage==SETTLE_READY);",
    "assert(fast_update&&absolute_update&&!settle_update&&!dtm1_synced&&fast_lut_frames==3);\n"
    "  assert(commands[0x10]==old1&&commands[0x13]==old2+1&&settle_stage==SETTLE_WAIT);",
)
replace_once(
    test,
    "assert(screen_powered&&absolute_update&&settle_stage==SETTLE_READY);assert(d->quiesce());return;",
    "assert(screen_powered&&absolute_update&&fast_lut_frames==3&&settle_stage==SETTLE_WAIT);assert(d->quiesce());return;",
)
replace_once(
    test,
    "assert(absolute_update&&!settle_update&&fast_lut_frames==1&&!dtm1_synced&&absolute_frames==17);\n"
    " assert(bytes_sent==4000&&settle_stage==SETTLE_READY);",
    "assert(absolute_update&&!settle_update&&fast_lut_frames==3&&!dtm1_synced&&absolute_frames==17);\n"
    " assert(bytes_sent==4000&&settle_stage==SETTLE_WAIT);",
)
replace_once(
    test,
    "fast_band(440,40);assert(settle_stage==SETTLE_READY&&!dtm1_synced);",
    "fast_band(440,40);assert(settle_stage==SETTLE_WAIT&&!dtm1_synced);",
)
replace_once(
    test,
    "assert(!settle_stage);complete_frame(token);assert(pofs==off&&settle_stage==SETTLE_READY);",
    "assert(!settle_stage);complete_frame(token);assert(pofs==off&&settle_stage==SETTLE_WAIT);",
)
replace_once(
    test,
    "complete_frame(t);assert(bytes_sent==48000&&commands[0x10]==old_sync&&visible[0]==0xCC&&visible[47999]==0xCC);",
    "complete_frame(t);assert(bytes_sent==48000&&fast_lut_frames==2&&commands[0x10]==old_sync&&visible[0]==0xCC&&visible[47999]==0xCC);",
)

settled = Path("minimal/test/uc8279_fast_settled.inc")
replace_once(
    settled,
    "assert(absolute_update&&!settle_update&&settle_stage==SETTLE_READY);const uint64_t token=pending_token;\n"
    "  const unsigned io=exchanges;const uint64_t now=tick;\n"
    "  assert(ext->request_settle(NULL,token)&&settle_stage==SETTLE_READY&&io==exchanges&&tick==now);",
    "assert(absolute_update&&!settle_update&&fast_lut_frames==3&&settle_stage==SETTLE_WAIT);const uint64_t token=pending_token;\n"
    "  const unsigned io=exchanges;const uint64_t now=tick;\n"
    "  assert(ext->request_settle(NULL,token)&&settle_stage==SETTLE_READY&&io==exchanges&&tick==now);",
)

idle = Path("minimal/test/run_idle_panel_policy_test.py")
replace_once(idle, '"""Actual fast0.1.12 provider and production Light idle helper, host only."""',
             '"""Actual fast0.1.17 provider and production Light idle helper, host only."""')
replace_once(idle, "['version']=='0.1.12'", "['version']=='0.1.17'")

idle_inc = Path("minimal/test/idle_panel_policy.inc")
replace_once(
    idle_inc,
    'puts("Actual fast0.1.12 panel + automatic Light: settling/finalized-off/pending frame readiness PASS");',
    'puts("Actual fast0.1.17 panel + automatic Light: bounded-drive settling/finalized-off/pending frame readiness PASS");',
)
