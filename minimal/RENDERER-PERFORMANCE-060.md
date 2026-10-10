# X4 0.1.60 renderer performance repair

Replace the twenty portable application ELFs over exact delivered X4 0.1.59. Retain native Runtime 0.2.2, panel 0.1.13, GT911 0.1.10, all providers, GameBoy, boot graph and grants. BLE Buttons retains the latest 0.1.22 input changes and increments to 0.1.23 with the shared adapter repair.

Springboard header font pixels now record two immutable text commands instead of thousands of one-pixel rectangles. The shared recorder allocates commands in blocks of 32. Monochrome rectangles transform coordinates and compute luminance once, then update packed byte spans; partial-byte edges and stride padding remain untouched. Row-bounded replay and input capture stay active. Allocation failure still materializes the complete prefix and preserves every draw call.

Verification covers original font output, original per-pixel rectangle output including rotation/flip, command allocation counts, allocation refusal/terminal retention and input dispatch while rendering is pending. Exact target compilation, full store admission and independent image readback are required by the package recipe. These are software checks; physical touch latency and animation FPS require the device.

The packer accepts a source-built mkspiffs reader only with an explicit SHA256 in the assembly spec. The reader uses the same 256-byte page, 4096-byte block, 32-byte name and four-byte metadata geometry. Existing packers retain their original default tool pin.

Full image at 0x0 replaces internal settings, Bluetooth bonds and AppData. Removable SD contents are excluded.
