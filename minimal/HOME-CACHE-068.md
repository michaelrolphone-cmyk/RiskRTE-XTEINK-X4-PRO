# X4 0.1.68

User testing reports that .67 Springboard, Quick Actions and the other apps are
fast and reliable, while Home remains slow. This image changes only Home and
cohort identity. Home 0.4.5 reuses its saved framebuffer when a resident return
or other redraw request has not changed the visible contents. The previous
Home 0.4.4 treated all redraw requests as full cache invalidations.

Contexts 0.4.0 is the latest published version at Utilities
3610548e8d1a17758873bd32f30506a448f0cc0e. It was already included in .67 and is
preserved byte-for-byte, together with scene-host 0.3.0, Lists 0.1.1,
Springboard 1.7.30, panel x4pro-uc8279-fast 0.1.16, and native Runtime 0.2.3.

Home's actual draw function passes full-pixel comparisons for warm unchanged
requests, minute/battery/Points/format/notice/press changes, orientation, pending
frame readiness and unavailable caches. Normal and ASan/UBSan checks pass, as
do actual adapter/Home transition endpoint checks and the Xtensa build. Cold
Home startup, background service timing and hardware settling remain unqualified;
this change does not claim to resolve every Home delay.

create_home_cache_068_spec.py pins the qualified .67 full image, clean source
identities and exact component/test inputs. package_home_cache_068.py verifies
49 ELFs, normal/sanitized cohort admission, deterministic SPIFFS, dual readback,
pair SHA/CRC and unchanged native/boot/AppData regions. The output is one 16 MiB
BIN at 0x0 for private hardware testing. It is a full image, not a data-preserving
component update. No release or merge is part of this build.
