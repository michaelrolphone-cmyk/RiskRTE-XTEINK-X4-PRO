# Completed settling pulses after delayed owner polling

The 0.1.39 hardware log shows GameBoy 1.3.16 successfully reading fourteen root directory entries, then failing its first display acquisition after about 4.5 seconds of synchronous storage work. Home subsequently fails its draw as well.

The production driver regression starts an ordinary settling refresh, observes BUSY assert, lets the modeled pulse complete, and delays the next owner poll by 4.5 seconds. Version 0.1.6 rejects this completed pulse with `settle busy completion timeout` and permanently refuses the next frame.

Version 0.1.7 checks the actual completion level before applying that timeout. This only applies to SETTLE_DONE, which requires an observed assertion. A still-active BUSY signal still times out; an unobserved assertion still fails; GPIO/SPI/clock errors still retain their existing failure handling. It issues no extra waveform, changes no panel settings, and does not report a fabricated completion timestamp or frame rate. Idle maintenance uses the same state machine.

The added production-driver tests cover delayed completion followed by a replacement frame, delayed stuck BUSY, an entirely unobserved pulse, and a BUSY-read failure. The full existing panel suite is run normally and under ASan/UBSan. This is a reproduced software defect consistent with the hardware log; a new physical-device test must confirm the user-visible repair.
