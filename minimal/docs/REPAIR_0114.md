X4 UC8279 resident-image settling test, product0.1.14

This builds on the inversion-only0.1.13 provider. After each completed fast
frame, provider0.1.3 repeats refresh of the resident controller image during
a1,600ms settling window. The repeats do not retransmit DTM1 or DTM2 pixels.
A newer accepted frame cancels further repeats; a pulse already in progress
must complete before the new upload. Input service remains cooperative.
Sleep and cleanup stop new repeats and drain any active pulse before power
changes. The original completed presentation token and metrics remain stable.

TwentyMHz SPI, normal600-gate geometry with the480-row visible offset, selected
one-frame waveform and corrected target polarity are retained. No experimental
geometry/TCON/PLL changes or additional UI changes are included. This is a
host/target-tested development image; device contrast and settling behavior
still need physical confirmation. Hosted CI was not awaited.

The16 application ELFs and Runtime0.1.61 remain identical to0.1.13. Automatic
plain startup/input/frame logs remain enabled; the separate correction for
logs discarded before USB host readiness is not included in this checkpoint.

UC8279 only. Flash the full16MiB BIN at0x0; this replaces firmware, NVS and
app-data. No device was flashed here.
