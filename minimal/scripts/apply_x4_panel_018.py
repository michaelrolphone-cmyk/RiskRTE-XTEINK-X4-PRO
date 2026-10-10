#!/usr/bin/env python3
"""Build panel 0.1.18 directly from the exact 0.1.12 source cohort."""
from pathlib import Path
import datetime
import hashlib
import json


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:160]!r}")
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
    "       SETTLE_FINAL_DONE, SETTLE_FINAL_CLOSE, SETTLE_SYNC_OLD,\n"
    "       SETTLE_SYNC_NEW, SETTLE_POF, SETTLE_POF_ASSERT, SETTLE_POF_DONE };\n",
    "       SETTLE_FINAL_DONE, SETTLE_FINAL_CLOSE, SETTLE_SYNC_OLD,\n"
    "       SETTLE_SYNC_NEW };\n",
)
replace_once(
    driver,
    "    settle_stage = absolute_update ? SETTLE_READY : SETTLE_WAIT;\n",
    "    /* One accepted frame receives one complete differential DRF. Normal\n"
    "     * quiet time never replays an intermediate animation target. */\n"
    "    settle_stage = SETTLE_WAIT;\n",
)
replace_once(
    driver,
    "            if (settle_sync_offset == 60000u) {\n"
    "                dtm1_synced = true; absolute_frames = 0; absolute_started_ms = UINT64_MAX;\n"
    "                if (cancel) settle_stage = SETTLE_NONE;\n"
    "                else settle_stage = SETTLE_POF;\n"
    "            }\n"
    "        } else if (settle_stage == SETTLE_POF) {\n"
    "            if (cancel) settle_stage = SETTLE_NONE;\n"
    "            else {\n"
    "                if (!panel_pin_read(X4PRO_PIN_EPD_BUSY)) { set_reason(\"idle power busy active\"); goto failed; }\n"
    "                if (!sample_now(&settle_power_ms)) goto failed;\n"
    "                command(0x02);\n"
    "                if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) {\n"
    "                    settle_phase_deadline = settle_power_ms + 100u; settle_stage = SETTLE_POF_ASSERT;\n"
    "                } else {\n"
    "                    settle_phase_deadline = settle_power_ms + 1500u; settle_stage = SETTLE_POF_DONE;\n"
    "                }\n"
    "            }\n"
    "        } else if (settle_stage == SETTLE_POF_ASSERT) {\n"
    "            if (now >= settle_phase_deadline) { set_reason(\"idle power busy never asserted\"); goto failed; }\n"
    "            if (panel_pin_read(X4PRO_PIN_EPD_BUSY)) break;\n"
    "            settle_phase_deadline = settle_power_ms + 1500u; settle_stage = SETTLE_POF_DONE;\n"
    "        } else if (settle_stage == SETTLE_POF_DONE) {\n"
    "            const bool complete = panel_pin_read(X4PRO_PIN_EPD_BUSY);\n"
    "            if (io_failed) goto failed;\n"
    "            if (!complete) {\n"
    "                if (now >= settle_phase_deadline) { set_reason(\"idle power completion timeout\"); goto failed; }\n"
    "                break;\n"
    "            }\n"
    "            screen_powered = false; settle_stage = SETTLE_NONE;\n"
    "        } else { set_reason(\"invalid settle state\"); goto failed; }\n",
    "            if (settle_sync_offset == 60000u) {\n"
    "                /* Keep the reconciled panel powered. POF was observed to\n"
    "                 * trigger rapid relaxation and re-expression of old pixels. */\n"
    "                dtm1_synced = true; absolute_frames = 0; absolute_started_ms = UINT64_MAX;\n"
    "                settle_stage = SETTLE_NONE;\n"
    "            }\n"
    "        } else { set_reason(\"invalid settle state\"); goto failed; }\n",
)
replace_once(
    driver,
    "    out->flags = RISC_DISPLAY_INFO_RETAINS_IMAGE | RISC_DISPLAY_INFO_PARTIAL_DAMAGE | RISC_DISPLAY_INFO_QUIESCE_SLEEP;\n",
    "    /* This diagnostic profile deliberately retains panel power and refuses\n"
    "     * normal sleep preparation after the first physical frame. */\n"
    "    out->flags = RISC_DISPLAY_INFO_RETAINS_IMAGE | RISC_DISPLAY_INFO_PARTIAL_DAMAGE;\n",
)
replace_once(
    driver,
    "    out->nominal_refresh_millihz = 10000; /* Selected lab mode scheduling hint. */\n"
    "    out->typical_present_latency_us = 100000; /* Integrated timing remains unqualified. */\n",
    "    out->nominal_refresh_millihz = 11000; /* Three-frame differential reference. */\n"
    "    out->typical_present_latency_us = 90000; /* Approximately 89 ms at 160 rows. */\n",
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
    "        /* Truthful DTM1 OLD plus DTM2 NEW makes unchanged 00/11 pixels idle.\n"
    "         * Only actual W->B and B->W transitions receive the bounded pulse. */\n"
    "        absolute_update = false;\n"
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
    "        /* Keep exact 0.1.12 full-width 40/80/160/480-row geometry while\n"
    "         * making unchanged pixels electrically idle. */\n"
    "        update_area = tested_window((uint32_t)update_area.y, (uint32_t)update_area.y + update_area.height);\n"
    "        fast_lut_frames = update_area.height <= X4PRO_ACTIVE_LOCAL_MAX_ROWS ?\n"
    "            X4PRO_ACTIVE_LOCAL_FRAMES : X4PRO_ACTIVE_BROAD_FRAMES;\n"
    "    } else if (!quality_partial) update_area = (risc_display_rect_v1){0, 0, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT};\n",
)
replace_once(
    driver,
    "    if (valid && absolute_update && settle_stage == SETTLE_WAIT) settle_stage = SETTLE_READY;\n",
    "    if (valid && settle_stage == SETTLE_WAIT) settle_stage = SETTLE_READY;\n",
)
replace_once(
    driver,
    "        if (screen_powered) {\n"
    "            command(0x02);\n"
    "            if (io_failed) return RISC_DISPLAY_POWER_RETAINED;\n"
    "            shutdown_stage = 1u; screen_powered = false;\n"
    "            /* Start settling after the completed command, not before its GPIO I/O. */\n"
    "            result = power_checkpoint(deadline);\n"
    "            shutdown_not_before = last_sample_ms + (controller == PROBE_UC8279 ? 1u : 200u);\n"
    "            if (result) return result;\n"
    "        } else shutdown_stage = 2u;\n",
    "        if (screen_powered) {\n"
    "            /* No POF/DSLP while an established image is on glass. Returning\n"
    "             * BUSY keeps the device awake rather than entering the observed\n"
    "             * unpowered relaxation state. */\n"
    "            return RISC_DISPLAY_POWER_BUSY;\n"
    "        } else shutdown_stage = 2u;\n",
)
replace_once(
    driver,
    " * completed before deadline sampling so retries never replay partial POF/DSLP.\n"
    " * A pending fast-frame settle is finalized physically before POF; an already\n"
    " * settled or quality frame adds no display work to this lifecycle. */\n",
    " * completed before deadline sampling. Once the panel has displayed an image,\n"
    " * this diagnostic profile refuses sleep preparation rather than issuing POF\n"
    " * or DSLP and allowing inactive-region relaxation. */\n",
)
replace_once(
    driver,
    "     * frame clear, PON or refresh. RESET recovers partial POF/DSLP/refusals. */\n",
    "     * frame clear, PON or refresh. RESET recovers pre-display DSLP/refusals. */\n",
)
replace_once(driver, 'append(destination, capacity, &used, "v=0.1.12 cause=");',
             'append(destination, capacity, &used, "v=0.1.18 cause=");')

manifest = Path("minimal/drivers/x4pro_uc8279_fast/manifest.json")
replace_once(manifest, '"version": "0.1.12"', '"version": "0.1.18"')

profile_test = Path("minimal/test/uc8279_fast_profile_test.py")
replace_once(profile_test, "manifest['version']=='0.1.12'", "manifest['version']=='0.1.18'")

readme = Path("minimal/drivers/x4pro_uc8279_fast/README.md")
replace_once(readme, "# X4 UC8279 fast provider 0.1.12\n", "# X4 UC8279 fast provider 0.1.18\n")
section = """

## Transition-selective powered-retention experiment (0.1.18)

This candidate is based directly on the exact 0.1.12 source cohort. It retains
0.1.12's controller setup and full-width 40/80/160/480-row gate windows, but
changes the physical active and resting policies identified by device testing.

Interactive updates use truthful DTM1 OLD and DTM2 NEW planes. Unchanged white
and unchanged black transition buckets are electrically idle; only W->B and
B->W pixels are driven. Localized windows up to 160 rows receive one three-frame
differential DRF. Broad 480-row motion receives one two-frame differential DRF.
After BUSY completes, the new target is immediately copied into DTM1 before the
presentation completes. No resident replay reinforces an obsolete animation
frame.

After the 2.3-second quiet period, the existing four-frame exact-target endpoint
redraw and dual-plane reconciliation still run. The panel then remains powered:
automatic POF is removed, the sleep capability flag is not advertised, and an
explicit power-prepare request returns BUSY once an image is established. This
intentionally trades idle power for a direct test of the observed POF-triggered
relaxation and old-image bleed.

The driver still accepts MONO1. Content grayscale dithering therefore remains
upstream; the Game Boy producer's stable 2x2 Bayer mapping must remain enabled.
The driver contribution is electrical transition selectivity, not replacement
of the producer's grayscale quantization.
"""
readme.write_text(readme.read_text().rstrip() + section + "\n")

test = Path("minimal/test/uc8279_fast_test.c")
replace_once(
    test,
    "const uint8_t frames=regs[0x20][1];assert(frames==1||frames==2||frames==X4PRO_FINAL_TARGET_FRAMES);",
    "const uint8_t frames=regs[0x20][1];assert(frames==1||frames==2||frames==3||frames==X4PRO_FINAL_TARGET_FRAMES);",
)
replace_once(
    test,
    "assert(info.nominal_refresh_millihz==10000&&info.typical_present_latency_us==100000);",
    "assert(info.nominal_refresh_millihz==11000&&info.typical_present_latency_us==90000&&!(info.flags&RISC_DISPLAY_INFO_QUIESCE_SLEEP));",
)
needle = " if(!strcmp(s,\"quality-seeded-cold\")){test_quality_seeded_cold();goto done;}baseline();\n"
scenario = """ if(!strcmp(s,\"quality-seeded-cold\")){test_quality_seeded_cold();goto done;}baseline();
 if(!strcmp(s,\"selective-nopof\")){
  const unsigned initial_pof=pofs,initial_sleep=sleeps,initial_final=final_target_refreshes;
  const unsigned old1=commands[0x10],old2=commands[0x13];
  risc_display_surface_v1 f=acquire_frame();memcpy(f.pixels,previous_frame,FRAME_BYTES);
  const unsigned index=220u*100u+2u;((uint8_t*)f.pixels)[index]^=0xFFu;
  const risc_display_rect_v1 damage={16,220,8,1};uint64_t token=submit_frame(f,&damage,1,false);
  assert(fast_update&&!absolute_update&&fast_lut_frames==3u);complete_frame(token);
  assert(dtm1_synced&&settle_stage==SETTLE_WAIT&&screen_powered);
  assert(commands[0x10]==old1+1&&commands[0x13]==old2+1&&bytes_sent==8000u);
  assert(pofs==initial_pof&&sleeps==initial_sleep);
  assert(!memcmp(old_ram+12000,ram+12000,FRAME_BYTES));
  assert(visible[index]==ram[index+12000]);
  const uint8_t unchanged=visible[index-1u];
  drain_settle();
  assert(screen_powered&&dtm1_synced&&pofs==initial_pof&&sleeps==initial_sleep);
  assert(final_target_refreshes==initial_final+1&&visible[index-1u]==unchanged);
  f=acquire_frame();memset(f.pixels,0x33,FRAME_BYTES);
  const unsigned broad1=commands[0x10],broad2=commands[0x13];
  token=submit_frame(f,NULL,0,false);assert(fast_update&&!absolute_update&&fast_lut_frames==2u);complete_frame(token);
  assert(commands[0x10]==broad1+1&&commands[0x13]==broad2+1&&bytes_sent==96000u&&dtm1_synced);
  drain_settle();assert(screen_powered&&pofs==initial_pof&&sleeps==initial_sleep);
  const risc_display_output_api_v1_power *panel_power=risc_display_output_power(output);assert(panel_power);
  assert(panel_power->prepare(NULL,1500)==RISC_DISPLAY_POWER_BUSY);
  assert(panel_power->resume(NULL,1500)==RISC_DISPLAY_POWER_OK);
  assert(screen_powered&&pofs==initial_pof&&sleeps==initial_sleep&&!d->quiesce());
  goto done;
 }
"""
replace_once(test, needle, scenario)

validation_path = Path("minimal/validation/uc8279-012-selective-nopof-0.1.18.json")
tracked = [driver, manifest, readme, test, profile_test]
validation = {
    "schema": "x4.uc8279.selective-nopof-validation",
    "schema_version": 1,
    "created_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "baseline": {
        "commit": "befc6f0e57eadd70c7b8ad195907e6678309fd1c",
        "driver_version": "0.1.12",
        "hardware_result": "preferred all-around baseline before later active-drive experiments"
    },
    "candidate": {"driver_version": "0.1.18"},
    "implementation": {
        "window_geometry": "0.1.12 full-width 40/80/160/480-row",
        "active_drive": "truthful differential A2",
        "localized_frames": 3,
        "broad_frames": 2,
        "unchanged_transition_buckets": "idle",
        "old_plane": "synchronized immediately after every completed active DRF",
        "resident_replay": False,
        "endpoint": "four-frame exact-target redraw plus dual-plane reconciliation",
        "automatic_pof": False,
        "sleep_after_display": "refused with BUSY; panel remains powered",
        "content_dither": "producer-owned MONO1; preserve static Game Boy 2x2 Bayer"
    },
    "lab_basis": {
        "two_frame_differential_160_row_fps": 13.01,
        "three_frame_160_row_reference_fps": 11.21,
        "selection": "three selective frames balance additional transition drive with accepted cadence"
    },
    "integrated_driver_hardware_tested": False,
    "files_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked}
}
validation_path.write_text(json.dumps(validation, indent=2) + "\n")
