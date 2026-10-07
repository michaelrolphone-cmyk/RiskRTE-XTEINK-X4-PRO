# RiscRTE for Xteink X4 Pro

X4-specific provider sources, board support, display/boot adaptation, build profile, and SD package provisioning. The immutable shared runtime dependency is recorded in [migration-source.json](migration-source.json).

## Build

```sh
git clone https://github.com/michaelrolphone-cmyk/T5S3-Reader.git ../riscrte-runtime
git -C ../riscrte-runtime checkout 34d8e694d89a1e72d8854403d8592c289fae3ddc
python3 scripts/prepare_runtime.py --upstream ../riscrte-runtime --output build/runtime
python3 scripts/run_host_tests.py build/runtime
python3 -m pip install platformio==6.1.19 pyelftools==0.32
(cd build/runtime && pio pkg install -e xteink-x4-pro)
# Install the upstream pinned Espressif provider compiler as shown in CI.
export NATIVE_DRIVER_CC=/path/to/xtensa-esp32s3-elf-gcc
export RISCRTE_X4_LINK_PROFILE=esp14-no-relax
(cd build/runtime && python3 scripts/build_platform_clock_v1.py)
python3 scripts/build_platform_drivers.py --runtime build/runtime
python3 scripts/stage_platform_packages.py --runtime build/runtime
python3 scripts/run_host_tests.py build/runtime --built
(cd build/runtime && pio run -e xteink-x4-pro)
```

The generated runtime directory must be new; preparation never edits the dependency checkout. Its `build-origin.json` records both the platform commit and pinned runtime commit/tree. Generated firmware and the staged SD tree belong to that combined source identity.

The SD profile selects all nine ordinary packages: shared platform clock plus X4 panel, buttons, frontlight, SD, I2C, touch, battery, and RTC. Package IDs, ABI contracts and imported versions remain unchanged.

## Ownership

Shared USB, platform clock, FatFs, shared RTC helpers, SDK/loader, and generic packaging/build tooling remain in T5S3-Reader. Several shared tools and CPU helpers still have historical X4 names; this migration references them instead of making a second maintained copy.

See [migration notes](docs/MIGRATION.md) for provenance, retained shared dependencies, validation and limitations. Historical hardware records remain upstream. No device flashing or hardware qualification is implied.
