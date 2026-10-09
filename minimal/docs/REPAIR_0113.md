X4 UC8279 polarity repair, product0.1.13

This is an inversion-only repair of0.1.12. Fast provider0.1.2 corrects
absolute-drive LUT tables0x21 and0x24 so the selected drive follows the new
pixel regardless of controller old-plane state. The source-derived mapping
is corroborated by the stock grayscale fold/invert and upload implementation;
physical polarity and contrast confirmation remain pending. No blanket
framebuffer inversion is applied.

Twenty-seven host scenarios pass normally and with ASan/UBSan, including an
independent two-plane transition decoder that fails against the old table.
Target ELF validation passes. The20MHz native transfer, one-frame duration,
48KB full-frame DTM2 upload, normal controller geometry, probe repair, Runtime
and16 application ELFs are unchanged. No settling or UI changes are included.

The separate requested1.6-second resident-image settling implementation is
still in progress. Startup diagnostics remain automatic; early USB-disconnected
log loss remains under investigation and is not claimed fixed here.

UC8279 only. Flash the full16MiB BIN at0x0; this replaces firmware, NVS and
app-data. No device was flashed or physically tested here.
