# X4 0.1.46 resident startup repair

The delivered 0.1.45 image loads `default.elf` but refuses it before calling init. The exact Home ELF exports `app_main`, matching init/fini functions, and the 16-byte `risc_resident_app_descriptor_v1` object. Runtime requires that descriptor to select the persistent host role. Its ESP loader previously collected only function exports, so `dlsym` could not return the descriptor. Host glibc lookup and structural ELF admission did not exercise this target-loader boundary.

This increment retains all 0.1.45 application/provider binaries, grants, startup rail sequence, DIO/OPI flash profile and retained Clock payload. It changes the native object-symbol lookup and cohort identity. The repair must be checked through the production ELF loader against the actual packaged Home and foreground ELFs, including cached and uncached paths, before packaging. Physical boot remains unverified until the replacement image is tested.
