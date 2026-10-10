# X4 resident integration checkpoint

Development product 0.1.44 starts from accepted 0.1.43 (798724aed06ed56deafc3e0ac3338999e6c3a80b). It retains the accepted display, touch, startup and storage driver sources, adding the bounded retained payload and its compiled RTC budget proof. `minimal/apps/resident-native.json` explicitly selects the accepted DIO/80 MHz flash and octal PSRAM profile; the default QIO composition is rejected for this cohort.

Runtime 0.1.90 (5ab1f4e9e3f17efb1153966868b52660bde53be0) combines the qualified resident policy/legacy handoff with validated GPIO-read hints. Its independent target and focused sanitizer checks pass. The composed DIO native also passes actual startup, flash, RTC and option proofs. Its bootloader, partition table and initial app-data are byte-identical to accepted 0.1.43. Runtime 0.1.87 USB-exit code is excluded while its target action remains stopped. Its host results do not establish a successful target build or Windows behavior.

The installed-app inventory stays at 21 applications. Twenty resident-role targets are qualified individually, and the development store passes production Runtime/native admission for all 44 ELFs with no provider or storage I/O. Final reference-renderer and crash-screen source integration precedes the final target selection. GameBoy 1.3.22 retains its original UI and uses the qualified clean legacy handoff; it includes the loading-stage diagnostics and separately tested save/config stat repair. Nested discovery remains deferred.

The selected Points service is native-UTC 0.4.8, source 0189ce87185b545af27015311c3bb4537e84c08d. Its ten existing/new KV bindings preserve all prior permissions, including read-write occurrence storage. AppData namespace 5 is verified unclaimed in the accepted source cohort and becomes the Points catalog/ledger namespace. No legacy KV record is rewritten by composition.

`package_resident_cohort.py` enforces the complete installed inventory, explicit role descriptors, one host Quick Actions renderer, no child renderer, terminal alarm guards, compatible native SDK hashes, distinct changed-app versions, compiled native admission, ELF compaction invariants and exact SPIFFS readback. The final `resident-cohort.json` selection is added only after the final host source/target is frozen. No 0.1.44 image is yet claimed deliverable.

This branch is local development only. Runtime .82 and later selected System source publication have separate unresolved review stops. No blocked source is being published through this product checkpoint.
