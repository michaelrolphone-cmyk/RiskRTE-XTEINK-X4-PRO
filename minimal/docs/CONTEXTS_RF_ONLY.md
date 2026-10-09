# RF-only Contexts product integration

The explicit `--contexts-rf-only` cohort adds the ordinary Contexts editor and
RF-only saved-model service, and selects the matching Clock and Waterfall model
owners. Audio remains unavailable because this product has no qualified audio
input. Existing application IDs, default identity and storage namespaces remain.

Clock/default has 16 unique requirements and 17 policy rows, including its two
existing key-value namespaces. Waterfall likewise uses distinct API1/API2
storage rows. Native Runtime and host admission both explicitly select policy17;
live application grants and manifest requirements remain bounded at16. The
native composer records that selection and opt-in image caching, then proves the
compiled marker and cache implementation in both staging and bundle admission.

The editor is an explicit non-broadcast foreground app. It retains Quick
Controls, ordinary Bluetooth/Wi-Fi controls and typed Light cleanup, while the
service arbitrates RF observation. No broadcast capability is added to that app.
Other applications preserve their existing telemetry selection. Only default,
Waterfall and Contexts receive the Contexts service capability.

An enabled monitor with absent models asks the original RF owner to export its
saved preferences, signatures, temporal examples and neural checkpoint. The
owner returns to the same default app. A rejected or unfinished handoff reports
a model failure requiring explicit reload. It does not loop through owners or
invent Audio availability. Sparse timer wake does not acquire Contexts.

Source pins and provider/app requirements are in `minimal/contexts-profile.json`
and `minimal/apps/sources.json`. The editor's native SDK remains its qualified
c77e7175 source; Clock retains the prior dfc0af505 logging/metrics SDK. Both have
the same Runtime API prefix through trace. Host and Xtensa checks preserve every
member offset against the appended stream-client API: old size44, stream at44,
new size48 on Xtensa. No installed app in this cohort requests stream sessions.

The paper editor passed normal/sanitized real-controller and adapter tests in
both reader orientations. Clock passed90 lifecycle and324 Home cases, with
measured live-grant peaks8 foreground,12 deep preparation and6 timer. RF tests
compose the actual service and owner with the native adapter. They caught and
corrected background-provider calls during uncertain capture cleanup. The
Springboard's genuine FontAwesome house glyph and last-entry pressed/launch
path are checked against the selected17-entry catalog.

Those host paths substitute physical peripherals. They do not qualify RF or
sleep hardware. Battery-only startup remains unresolved; the prior retained
startup breadcrumb is preserved. Serial's generic stream/client work is not
selected because an exclusive physical USB/UART transport is still unqualified.

Use the complete store/ELF admission and packed-image verification for the final
exact product commit. A full16MiB first-install BIN replaces NVS and app-data;
it is not a preserving update. Generic provisioning and a preserving update
remain separate workflows.

## Dense store packing

The final store selects the official Espressif v4.4.7 spiffsgen.py producer,
SHA256 5779792a4d98a12383233267511735769a201b5c2c79b582186a40c22b7c6466,
through the shared Watch current_bootfs.build tool selected by --static-spiffs. It uses the unchanged target geometry and
lexicographic flat leading-slash names, constructing final pages without
intermediate deleted-page churn. Every unpacked file must equal the admitted
store. This is offline packing, not a relaxed Runtime capacity limit or a new
filesystem format. The same raw image is also mounted by the pinned production
SPIFFS source in a host read-only qualification. No flash layout changes.
