# X4 0.1.66: correct the actual Contexts launch path

The delivered 0.1.65 Contexts app still launched the paged `cu_draw` list.
The earlier screenshot test exercised a separate legacy renderer. The claimed
mockup implementation was not established by those screenshots.

This correction installs Contexts 0.4.0 and scene-host 0.3.0 together, and grants
Contexts `ui.scene` and `time.civil`. The real app_main now uses shared NOVA
components for Home, Library tabs, teaching, saved profiles, rules, settings,
workflows and confirmation dialogs. Its raw-input tests execute the actual app
and renderer on Watch and X4 fixtures, including pending presentation input and
failed saves. Library/model storage continues to use the existing namespaces.

The full image is composed over the exact delivered 0.1.65 image. Home,
Springboard, Lists, native Runtime 0.2.3 and panel 0.1.12 remain byte-identical.
The package script validates all ELFs and the complete cohort, both SPIFFS
readbacks, bank pairing and preserved regions. Source inputs and package hashes
are recorded in build-custody.json. Physical device testing remains outstanding.

As with the .65 full image, flashing at 0x0 replaces internal settings and
AppData; this is not a migration preserving data already on a device. The
component-stage recipe itself does not replace saved profile/rule files.
