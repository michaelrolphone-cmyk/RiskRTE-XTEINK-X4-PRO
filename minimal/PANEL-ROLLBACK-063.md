# X4 0.1.63: retain the renderer improvements, restore fast panel 0.1.13

The user reports that 0.1.62 appears faster but its fast panel 0.1.15 ghosts heavily. This image restores the exact `x4pro-uc8279-fast` 0.1.13 ELF and manifest shipped in 0.1.61. All 46 other module ELFs, their manifests, the native firmware, boot graph and grants are preserved byte-for-byte from delivered 0.1.62. The twenty applications retain their existing versions because their binaries are unchanged. Only the panel ELF, panel manifest and product cohort metadata change in the boot store.

This preserves the software/display pipeline overlap, native-row packed fills, exact paper spans, compact Points commands and bounded input sampling. It provides a driver-only comparison without attributing the apparent speed improvement to either the renderer or panel before hardware testing.

The selected driver source SHA-256 is `82ed04151d08364c5e893501af04f1ff5cb2f9a6492649cf08fd40631c71acb0`, matching the prior 0.1.13 source. Its host model and focused tests are restored from `cbf4bfd34372bf87ccf60429e062a6e96d043aca`, which contains that exact source. The cadence fixture retains the 0.1.62 software-pipeline completion and four-ms input checks.

The packager verifies the SHA-256 of both delivered reference images, requires the restored panel bytes to equal the panel extracted from 0.1.61, and permits exactly three changed store files relative to 0.1.62. It runs strict admission of all 47 ELFs, whole-cohort admission normally and under sanitizers, deterministic store generation, two independent store readbacks and paired-bank SHA/CRC checks. The integrated real adapter/Runtime/panel cadence test is run normally and under ASan/UBSan. These checks do not establish on-device ghosting or frame rate.

The output is a 16 MiB full image at offset `0x0`; full flashing overwrites internal settings, Bluetooth bonds and AppData. Removable SD data is outside the image.
