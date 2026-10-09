# Exact bridge-to-Contexts route

`build_contexts_update_route.py` creates one source-bound paired payload from the
qualified 0.1.25 native bridge to the complete 0.1.26 cohort. It accepts the exact
bridge source `def7a7f8b0a1e4f3756f430273708056c76fb9e1`, its verified native
composition and unchanged installed files. The target must be built from this
exact clean product source and canonical Runtime 0.1.75.

Only the target `boot.json` changes. The added migration records the exact source
product, version and revision and grants only the new `contexts` identity access
to KV API1 namespace1. The production Runtime must prove that this namespace was
already universally shared. Private app-data/KV ownership, board declarations,
existing providers and every application byte remain governed by ordinary
cohort admission. Both installed-native admission and target self-validation run
before packing, followed by independent SPIFFS byte readback.

The payload has a source-specific asset name, separate from the normal full
installation and paired payload. Its local catalog binds all eight source-route
selectors, including the actual active store SHA-256. The legacy `firmware`
entry is null: an unmatched old client must not receive a universal offer.
Catalog URLs describe future repository asset naming; nothing is published.

This helper does not make the original empty-feed installation usable. The
owner-controlled entry/bootstrap mechanism and actual native-bank transaction,
receipt inheritance, interruption, retry, rollback and preservation tests remain
separate requirements. `transaction_qualified` stays false in its receipt until
an independent exact-artifact qualification is recorded. It performs no device,
network, owner-input, release or feed operation.
