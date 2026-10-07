# Explicit hardware profiles

Generate with `minimal/scripts/generate_profile.py --panel ssd1677|uc8279` and
`--output DIR`. Neither panel is the default: the recovered source supports
both, but does not identify the controller fitted to this user's physical board.
The produced hardware-only graph includes all nine X4 providers with exact
instance bindings. It is a composition fixture, not a runnable product store:
application ELFs/policies and ordinary shared services must still be supplied.

Touch requires the shared Runtime touch.i2c@2 extension pinned in the source
lock. Runtime code stays in its own repository. The host profile test feeds
actual materialized records into the full panel and SD driver test suites,
covering zero-filled unused ABI slots rather than handwritten assumptions.

Panel is native800x480 MONO1 with portrait application rotation90; touch reports
logical480x800. The declared SPI frequency is metadata, not measured GPIO-driven
cadence. The native one-bit SD transport uses its separate GPIO bank. RTC
protocol identity is explicit without inventing an unidentified silicon vendor.
Battery and RTC are mandatory selected providers, not optional stage omissions.

## Provisioning payload from a completed candidate

`minimal/scripts/create_provisioning_profile.py` wraps the immutable
`RiscRTE` `scripts/provision_profile.py` pinned in `minimal/sources.lock.json`.
It consumes an existing `build_test_bundle.py` output directory (`store/`,
`firmware.bin`, `build-custody.json`) and its matching native candidate directory
(`candidate.json` and all recorded assets). It never builds or edits those inputs.
The store must already have completed the product's graph/ELF admission checks;
this wrapper checks byte custody and selections, not electrical behavior or ELF
execution. It does not upgrade a builder's skipped checks into qualification.

Supply your own canonical HTTPS URLs. The base URL ends in `/` and corresponds
to the contents of the generated `files/` directory. The profile URL records the
intended owner-profile location; it must not collide with a store file URL.
Neither URL is fetched or published. No URL defaults or production endpoints
are provided.

```sh
python3 minimal/scripts/create_provisioning_profile.py \
  --runtime "$RISCRTE_RUNTIME_ROOT" \
  --bundle "$COMPLETED_X4_BUNDLE" --native "$MATCHING_NATIVE_CANDIDATE" \
  --panel ssd1677 \
  --base-url "$PAYLOAD_BASE_URL" --profile-url "$OWNER_PROFILE_URL" \
  --output "$NEW_PAYLOAD_DIRECTORY"
```

Use `--panel uc8279` only for that explicit candidate. The wrapper checks the
board configuration against the selected X4 variant, the entire store against
its custody manifest, all recorded native asset hashes, matching bundle/native
firmware, and compiled source/version/ABI markers. It requires the locked
Runtime version, ABI2, and `riscrte-paired-appdata-v2` target identity. It refuses
missing or stale receipts, unsafe paths and symlinks, invalid manifest references,
more than 128 files, capacity overflow, and an existing output directory. The
output parent must exist; on systems with symlinked temporary paths, use their
canonical physical paths.

Output includes frozen `files/`, the shared `inventory.json`, compact
`payload-profile.json`, custody metadata in `payload.json`, `SHA256SUMS`, and a
completion marker written last. The template pins every file's path, length and
SHA-256 using schema2's shared base URL. It reserves worst-case space for the
owner's legal Wi-Fi fields within the native 16 KiB profile limit.

**The credential-free payload template is not an installable native profile.**
The pinned native parser requires `wifi`; this tool intentionally omits it.
Use the shared Runtime's separate `provision_profile.py profile` owner workflow
with the generated inventory, the same base URL, private Wi-Fi input, validator
and explicit time server. That workflow produces a different, private profile
with its own digest and validates owner inputs. The template digest is never a
claim about the final owner profile at the recorded URL. This wrapper generates
no credentials, NVS, releases, uploads or device actions.

Run focused offline tests against the actual pinned shared implementation:

```sh
RISCRTE_RUNTIME_ROOT=/path/to/pinned/RiscRTE \
  python3 minimal/test/provisioning_profile_test.py
```

Tests use synthetic bytes for custody checks; no target build or hardware result
is implied. Existing delivered images and board-power sources are unchanged.
