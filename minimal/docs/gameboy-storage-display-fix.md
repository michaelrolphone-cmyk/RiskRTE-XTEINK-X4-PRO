# Game Boy storage and display integration repair

## Components

- `x4pro-sd` 0.2.12 prefers the tagged hardware one-bit SDMMC API supplied by the native runtime. The known board signals remain CLK41, CMD42, DAT0 40 and active-low power5. FatFs, save files, export leases and power policy stay in this ELF.
- `x4pro-uc8279-fast` 0.1.8 closes SPI at each owner-poll boundary and resumes the existing RAM cursor in the next poll. Normal refresh and power-on completion now check observed BUSY completion before classifying a delayed poll as a timeout. The ten-second aggregate service budget excludes foreground gaps; physical BUSY deadlines and assertion/error checks remain enforced.
- The Game Boy ELF, ROM format and save-state format do not change.

## Native integration

Hardware SDMMC requires the Runtime feature in `perf/scoped-gpio-read-0188` (RiscRTE PR59), not merely the replacement storage ELF. The current product lock still points to the exact Runtime .96 resident-loader source. That source commit is not available in the public repository at this checkpoint. Its pin, all application selections, and the frozen .46 image records are deliberately preserved; this change does not claim a new full current-product BIN.

Forward-port the Runtime SDMMC feature onto the complete .96 source, commit it, and use that exact resulting Runtime commit when composing the next product. Add `--sdmmc` to the existing `prepare_native_runtime.py prepare` invocation, preserving its other options (USB PHY, retained wake, failure evidence, flash mode, app policy and image cache). The composer rejects absent feature source, records selection in the composition identity, and validates the linked ABI marker. Do not substitute the public .88-based feature test image for the complete product.

The ELF automatically uses the native API when it is present. A legacy runtime keeps the existing GPIO transport for compatibility; it therefore does **not** receive the hardware throughput improvement just by installing the ELF. A hardware initialization/transfer failure never silently falls back to bit-banging a controller that might still own the pins.

## Validation

Eight production fast-display regressions exercise 45-second upload/power/refresh pauses, the original 4.5-second completed-refresh delay, actual stuck BUSY, an unobserved assertion and a GPIO read failure. Before the repair the success cases failed with a held SPI transaction, aggregate deadline, or BUSY-completion timeout. They pass after the repair together with the existing suite, normally and under ASan/UBSan.

Nine new real-ELF/FatFs storage scenarios exercise ROM and state-sized file roundtrips, absent media, failed initialization/teardown, read/write failures, sleep/wake and USB export/return. Exact reads of 524288-byte and 1048576-byte ROM fixtures and a 59320-byte state fixture make zero per-bit GPIO calls. Existing GPIO-transport, filesystem, sleep, export and boot-log tests remain active. These are hardware-boundary models, not measured SD-card speed or emulator snapshot deserialization.

The original capture does not identify the first triggering crash guard. The repaired display defects are reproduced software failures consistent with storage-induced delays; a physical run must still confirm the actual save-state crash is gone. No panel waveform, voltage, BUSY polarity, display geometry, ROM contents or save contents is changed.


## UC8279 0.1.9 plane-coherent idle follow-up

The 0.1.8 delayed-poll repair is retained. The next Game Boy test package removes
the no-upload 30-second maintenance refresh, uses differential DEFAULT frames,
bounds LOW_LATENCY absolute bursts, synchronizes both complete controller planes
before powered idle, powers the panel off after the 2.3-second quiet period, and
replays the selected profile after PON. Game Boy, save formats, Runtime and the
SD provider are otherwise unchanged.
