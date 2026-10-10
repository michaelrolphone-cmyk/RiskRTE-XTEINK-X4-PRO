# X4 0.1.46 resident startup repair

The delivered 0.1.45 image loads `default.elf` but refuses it before calling init. The exact Home ELF exports `app_main`, matching init/fini functions, and the 16-byte `risc_resident_app_descriptor_v1` object. Runtime requires that descriptor to select the persistent host role. Its ESP loader previously collected only function exports, so `dlsym` could not return the descriptor. Host glibc lookup and structural ELF admission did not exercise this target-loader boundary.

This increment retains all 0.1.45 application/provider binaries, grants, startup rail sequence, DIO/OPI flash profile and retained Clock payload. It changes the native object-symbol lookup and cohort identity. The repair must be checked through the production ELF loader against the actual packaged Home and foreground ELFs, including cached and uncached paths, before packaging. Physical boot remains unverified until the replacement image is tested.

## Validation

The exact .45 Home and 19 foreground binaries passed production reader, validator, relocator and dynamic-symbol lookup in uncached, cache-miss and cache-hit paths. The old .93 source reproduced 60 missing-descriptor results; the repaired source passed 111 cases normally and 111 under ASan/UBSan, including malformed bounds and padded data/BSS sections. Architecture relocation/publication and RTOS use host hooks; no Xtensa app instructions are executed by that host fixture.

Both native target configurations and seven surrounding host suites passed. Eight admission-log cases passed in each mode, including missing entry, unmatched lifecycle hooks and invalid resident descriptor fields. The product image preserves all 90 non-cohort store files and its 44 ELFs, passes complete native/store admission, exact DIO/startup asset checks and full partition/image readback. The full first-install BIN is 16 MiB with SHA256 `bef2dd81a78b8607ed7fe090744b56ef88fb2dec400d453e30e2284a041b1008`. Physical boot remains pending; Windows MSC and the separate USB exit/serial correction remain unresolved.
