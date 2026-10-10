# X4 declarative Alarms prototype 0.1.0

This opt-in selection uses the same Alarms 0.3.0 ELF as Watch, with the shared optional scene host, an external portrait-monochrome presentation profile, and native-UTC alarm control. Display rotation, touch geometry, pagination, rasterization and refresh handling are outside the application.

`sources.json` pins shared component inputs, not the current native firmware. Existing deployment locks, default firmware, saved alarm/countdown/Points data and legacy Alarms are unchanged. The referenced current native Runtime commit `ae83a7c14f83313101c0d491a19101f420d28dc9` was unavailable through GitHub during this work. The public Runtime prototype therefore is not substituted for that newer native composition.

## Build a component stage

Check out the three source pins into sibling directories Runtime, System and Utilities, then use the pinned Xtensa compiler:

```sh
python System/scripts/build_scene_services.py --runtime Runtime --output build/scene-system
python Utilities/scripts/build_alarms_scene.py --runtime Runtime --system-apps System --output build/scene-alarms
python Utilities/scripts/compose_alarms_scene.py \
  --store "$CURRENT_X4_STORE" \
  --system-packages build/scene-system \
  --alarm-packages build/scene-alarms \
  --profile X4/minimal/prototypes/alarms-scene/profile.json \
  --runtime Runtime --output build/x4-scene-stage
```

The component stager copies the store, validates the existing API2 scheduler/timezone/storage bindings, replaces only Alarms and its grants, adds the three selected providers, and checks namespace 61 and provider capacity. Unrelated packages remain byte-identical. Obsolete cohort metadata is removed rather than reused.

The current native candidate needs the generic capacity extension at 28 providers/44 grants with PSRAM metadata. Adding `--native-elf "$NEW_NATIVE_ELF"` verifies its linked capacity witness. A staged component directory remains non-flashable until the device's current native image and changed package selection are bound into a new cohort/image.

## Verification boundary

The shared production-Runtime suite runs the actual controller, presenter, alarm control, scheduler, loader lifecycle and file storage against hardware doubles. It proves real ELF unload/reload and restored drafts, and fires/dismisses an alarm after the GUI has unloaded. The monochrome presenter uses an 800x480 physical surface rotated to 480x800 logical portrait, independent touch rotation, completed-frame hit maps and pending-refresh teardown handling.

Physical X4 verification and current product-image binding remain outstanding: open/edit/arm, Home/relaunch draft restoration, delivery with UI absent, dismiss/cancel, countdown/Points preservation, touch/Back, BUSY/pending refresh, sleep/wake, memory and power. No target was flashed by this prototype.

A headless deployment selects only domain services. Terminal or remote web presentation can implement the semantic scene/event boundary later; this prototype does not require or start either transport.
