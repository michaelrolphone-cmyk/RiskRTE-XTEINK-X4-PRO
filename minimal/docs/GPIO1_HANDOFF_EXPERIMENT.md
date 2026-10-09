# GPIO1 HIGH-before-mux experiment

This isolated change starts from the exact .32 source, without the .33
RTC-before-MSPI wrappers. It tests a conditional handoff hypothesis; it does not
establish the cause of the battery-only reset failure.

At the existing app_main hook, GPIO1 HIGH is written first. Pinned S3 register
operations then select GPIO_OUT/GPIO_ENABLE without inversion, disable open
drain and pulls/interrupts, enable input and output, and select the digital GPIO
IOMUX function. The code checks the digital latch, enable, matrix and IOMUX
registers before the single rtc_gpio_deinit call selects the digital path.
There is no gpio_config call and therefore no hidden early RTC mux change.

The prior pad hold stays enabled throughout this preparation. After the mux
selection, HIGH is confirmed again and the physical input must read HIGH before
gpio_hold_dis is called. The pad is re-held, then read again. A held LOW or held
input-disabled state fails closed without releasing the hold. The pre-release
held-pad reading alone is not proof that the hidden digital configuration is
ready; the separate register checks provide that logical precondition.

Eight operation IDs identify the failing stage: HIGH, digital staging, RTC mux,
confirm HIGH, pre-unhold input, unhold, hold, and final input. Existing record
layout/checksums and old records remain readable; the operation bound expands
from seven to eight. First NVS persistence remains after rail preparation.

Host coverage uses the unmodified pinned Arduino startup bodies and models
digital/RTC entry, unheld/held-HIGH/held-LOW/held-input-disabled states, failures
at every checked step, cold boot, retained resets, and deep-wake reset codes.
The linked proof rejects the .32 deinit-first call order and requires the
noinline digital staging helper before RTC mux selection, with input readback
before hold release. Exact target register stores require disassembly review.
Neither host simulation nor instruction order establishes zero electrical
glitch on the physical board. Hardware qualification is pending.
