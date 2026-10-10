#!/usr/bin/env python3
"""Remove the diagnostic sleep refusal while retaining awake no-POF behavior."""
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
    "/* Each operation has a total owner-admission deadline, at most 1500 ms and\n"
    " * 150 ten-ms readiness polls. Single commands are finite (at most 6 bytes),\n"
    " * completed before deadline sampling. Once the panel has displayed an image,\n"
    " * this diagnostic profile refuses sleep preparation rather than issuing POF\n"
    " * or DSLP and allowing inactive-region relaxation. */",
    "/* Each operation has a total owner-admission deadline, at most 1500 ms and\n"
    " * 150 ten-ms readiness polls. Single commands are finite (at most 6 bytes),\n"
    " * completed before deadline sampling so retries never replay partial POF/DSLP.\n"
    " * Awake idle retains panel power; an explicit system-sleep request still uses\n"
    " * the established POF, DSLP, reset-hold and resume lifecycle. */",
)
replace_once(
    driver,
    "        /* Refuse sleep before invalidating any live state. A BUSY result must\n"
    "         * leave started/history/planes intact so normal rendering can continue. */\n"
    "        if (screen_powered) return RISC_DISPLAY_POWER_BUSY;\n"
    "        started = false; previous_seeded = completed_history = dtm1_synced = false;\n"
    "        absolute_frames = 0; absolute_started_ms = UINT64_MAX;\n"
    "        shutdown_stage = 2u;",
    "        started = false; previous_seeded = completed_history = dtm1_synced = false;\n"
    "        absolute_frames = 0; absolute_started_ms = UINT64_MAX;\n"
    "        if (screen_powered) {\n"
    "            command(0x02);\n"
    "            if (io_failed) return RISC_DISPLAY_POWER_RETAINED;\n"
    "            shutdown_stage = 1u; screen_powered = false;\n"
    "            /* Explicit system sleep, not ordinary awake-idle finalization. */\n"
    "            result = power_checkpoint(deadline);\n"
    "            shutdown_not_before = last_sample_ms + (controller == PROBE_UC8279 ? 1u : 200u);\n"
    "            if (result) return result;\n"
    "        } else shutdown_stage = 2u;",
)
replace_once(
    driver,
    "    /* The same controller register setup as initial start, but without probe,\n"
    "     * frame clear, PON or refresh. RESET recovers pre-display DSLP/refusals. */",
    "    /* The same controller register setup as initial start, but without probe,\n"
    "     * frame clear, PON or refresh. RESET recovers partial POF/DSLP state. */",
)
replace_once(
    driver,
    "    /* This diagnostic profile deliberately retains panel power and refuses\n"
    "     * normal sleep preparation after the first physical frame. */\n"
    "    out->flags = RISC_DISPLAY_INFO_RETAINS_IMAGE | RISC_DISPLAY_INFO_PARTIAL_DAMAGE;",
    "    /* Ordinary awake idle retains panel power; explicit system sleep remains\n"
    "     * available through the typed power lifecycle. */\n"
    "    out->flags = RISC_DISPLAY_INFO_RETAINS_IMAGE | RISC_DISPLAY_INFO_PARTIAL_DAMAGE |\n"
    "        RISC_DISPLAY_INFO_QUIESCE_SLEEP;",
)
replace_once(driver, 'append(destination, capacity, &used, "v=0.1.18 cause=");',
             'append(destination, capacity, &used, "v=0.1.19 cause=");')

manifest = Path("minimal/drivers/x4pro_uc8279_fast/manifest.json")
replace_once(manifest, '"version": "0.1.18"', '"version": "0.1.19"')

profile_test = Path("minimal/test/uc8279_fast_profile_test.py")
replace_once(profile_test, "manifest['version']=='0.1.18'", "manifest['version']=='0.1.19'")

readme = Path("minimal/drivers/x4pro_uc8279_fast/README.md")
replace_once(readme, "# X4 UC8279 fast provider 0.1.18", "# X4 UC8279 fast provider 0.1.19")
replace_once(
    readme,
    "## Transition-selective powered-retention experiment (0.1.18)",
    "## Transition-selective powered retention with normal sleep (0.1.19)",
)
replace_once(
    readme,
    "After the 2.3-second quiet period, the existing four-frame exact-target endpoint\n"
    "redraw and dual-plane reconciliation still run. The panel then remains powered:\n"
    "automatic POF is removed, the sleep capability flag is not advertised, and an\n"
    "explicit power-prepare request returns BUSY once an image is established. This\n"
    "intentionally trades idle power for a direct test of the observed POF-triggered\n"
    "relaxation and old-image bleed.",
    "After the 2.3-second quiet period, the existing four-frame exact-target endpoint\n"
    "redraw and dual-plane reconciliation still run. The panel then remains powered\n"
    "during ordinary awake idle, so a static screen does not automatically enter the\n"
    "observed POF-triggered relaxation state. This does not replace the product power\n"
    "lifecycle: an explicit system-sleep request drains finalization, issues validated\n"
    "POF and DSLP, holds reset, and resumes through the established reset/profile path.\n"
    "The normal sleep capability is advertised; no diagnostic BUSY interlock remains.",
)

host_test = Path("minimal/test/uc8279_fast_test.c")
replace_once(
    host_test,
    "assert(info.nominal_refresh_millihz==11000&&info.typical_present_latency_us==90000&&!(info.flags&RISC_DISPLAY_INFO_QUIESCE_SLEEP));",
    "assert(info.nominal_refresh_millihz==11000&&info.typical_present_latency_us==90000&&(info.flags&RISC_DISPLAY_INFO_QUIESCE_SLEEP));",
)
replace_once(
    host_test,
    "  assert(panel_power->prepare(NULL,1500)==RISC_DISPLAY_POWER_BUSY);\n"
    "  assert(panel_power->resume(NULL,1500)==RISC_DISPLAY_POWER_OK);\n"
    "  assert(screen_powered&&pofs==initial_pof&&sleeps==initial_sleep&&!d->quiesce());",
    "  assert(panel_power->prepare(NULL,1500)==RISC_DISPLAY_POWER_OK);\n"
    "  assert(!screen_powered&&pofs==initial_pof+1&&sleeps==initial_sleep+1&&reset_held);\n"
    "  assert(panel_power->resume(NULL,1500)==RISC_DISPLAY_POWER_OK);\n"
    "  assert(!screen_powered&&pofs==initial_pof+1&&sleeps==initial_sleep+1&&!completed_history);\n"
    "  assert(d->quiesce());",
)

validation_path = Path("minimal/validation/uc8279-012-selective-realsleep-0.1.19.json")
tracked = [driver, manifest, readme, profile_test, host_test]
validation = {
    "schema": "x4.uc8279.selective-real-sleep-validation",
    "schema_version": 1,
    "created_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "baseline": {
        "commit": "6cecb165152af12ae49979924b02ac308223fe12",
        "driver_version": "0.1.18",
        "problem": "diagnostic sleep refusal replaced the product sleep lifecycle",
    },
    "candidate": {"driver_version": "0.1.19"},
    "rendering": {
        "active_drive": "unchanged 0.1.18 truthful differential A2",
        "localized_frames": 3,
        "broad_frames": 2,
        "automatic_awake_idle_pof": False,
        "endpoint": "four-frame exact-target redraw plus dual-plane reconciliation",
    },
    "power": {
        "sleep_capability_advertised": True,
        "explicit_sleep": "drain finalizer, POF, DSLP, reset hold",
        "resume": "reset plus controller profile replay",
        "sleep_refusal": False,
    },
    "software_validation": {
        "status": "pending focused exact-head qualification",
        "workflow": ".github/workflows/apply-x4-panel-019.yml",
    },
    "integrated_driver_hardware_tested": False,
    "files_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked},
}
validation_path.write_text(json.dumps(validation, indent=2) + "\n")
