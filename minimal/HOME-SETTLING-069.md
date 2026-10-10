# X4 0.1.69 private Home settling test

The first Home image was visible, then its physical settling stalled on both
boot and return. Home 0.4.6 fixes two reproduced software defects:

- The app poll now yields to Runtime at least once when foreground work has
  exhausted the polling interval. Provider progress continues after the panel
  reports the initial presentation complete, while resident settling continues.
- Checked preference reads still pause capture/advertising but preserve cached
  policy validity. Contexts and Broadcast no longer invalidate each other's
  caches on every Home pass. Writes and lifecycle stops retain invalidation;
  failed quiescence still prevents storage access.

The actual clients/guards changed from 100 reads in 20 passes to 5 reads. The
actual adapter, Runtime and selected panel reproduced stuck settling under a
25 ms foreground-work model; it completes with the fix at all tested poll
intervals. GPIO/BUSY/SPI timing is deterministic fixture input, not hardware
measurement. The driver's 2,300 ms physical settling envelope is unchanged.
Normal/sanitized paired-client and panel cases pass, as do actual Home
transition endpoints, sparse storage guards and 14 async sleep/cancel cases.
The old standalone Contexts fixture still fails at line 93 on both untouched
.68 and this source; its raw unguarded write expects a timed policy reload.
It is not represented as passing. See System docs/HOME_SETTLING_069.md.

Only default.elf, default.json and cohort.json may change from the exact .68
image, SHA-256 56f87e2e1e86f0f83807e43721dc826aaddd6a3c34a60f72ba915d3845b7b2c6.
Contexts 0.4.0, scene-host 0.3.0, Springboard 1.7.30, Lists 0.1.1, panel
x4pro-uc8279-fast 0.1.16, boot policy and qualified native Runtime 0.2.3 remain
byte-identical. Contexts' published head was rechecked before assembly.

Build with scripts/create_home_settling_069_spec.py and
scripts/package_home_settling_069.py. Source commits/trees, compiled inputs,
target ELF receipts, test records, the immutable baseline, native admission,
dual SPIFFS readback, paired bank SHA/CRC and preserved flash regions are
checked by the spec and packager. The deliverable is a direct full 16 MiB BIN
for offset 0x0. No hardware test, device flashing, merge or release is implied.
