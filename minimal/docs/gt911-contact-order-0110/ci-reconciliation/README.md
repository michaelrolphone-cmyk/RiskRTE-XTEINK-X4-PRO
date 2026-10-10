# Exact-head CI SDK/fixture reconciliation

Run 38069616640 at source 052b2bcdc failed before GT911 and target steps:
frontlight_cpu_port_test.cpp treated the inherited Runtime 0.1.81 flat GPIO
table as an SDMMC-suffixed table. X4 platform workflow passed.

Reuse only the three test-helper changes from the independently qualified
X4 e74cdf0588271f6c82af290c17ce07231acaf437 source: gpioBase() selects either
flat or suffixed base without changing any assertion or production behavior.
Pin this provider CI workflow to fetchable Runtime
b25b1d467a557e8d693211cff2eff959eb9c79de (exact tree equivalent of the .2.2 SDK
already used for this candidate qualification). Reader remains
29276b4e1cff7819af065a77cf64805f6d5a12ce. No product composition or panel source
is copied; the inherited historical product lock is not a new image recipe.

All three affected actual Runtime/GPIO fixtures pass normal and ASan/UBSan on
the exact candidate branch, with LeakSanitizer disabled under ptrace. Logs are
adjacent. GT911 source/manifest and the qualified target bytes are unchanged.
The next exact-head hosted run is pending; these local results do not claim
full hosted CI success.
