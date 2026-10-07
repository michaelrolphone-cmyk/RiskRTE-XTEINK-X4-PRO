# Agent guidance

This repository owns the Xteink X4 Pro platform files listed in migration-source.json, its X4 PlatformIO environment, and device provisioning. Shared runtime, SDK, loader, USB, platform clock, FatFs storage and shared RTC helpers remain upstream.

Prefer original paths for overlay composition because the source uses relative include contracts. Use the exact runtime commit and tree pinned in migration-source.json. Build through scripts/prepare_runtime.py; do not vendor shared runtime implementations here or modify a paused upstream branch.

Imported provider sources and manifests retain their package IDs and versions. For later distributable code changes, bump the affected manifest version and keep package/archive identity consistent. A byte-identical repository migration does not create a new product version.

Read README.md and docs/MIGRATION.md before changing build or provenance behavior. Prefer host tests plus target builds; report hardware testing separately. Publication of branches and pull requests is separate from merge, release and device flashing.
