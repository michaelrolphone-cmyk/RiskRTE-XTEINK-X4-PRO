# Battery-only boot investigation

## What this candidate changes

The X4 native composition asserts the documented GPIO1 peripheral/touch rail at
the beginning of IDF's call to `app_main`, before Arduino USB initialization,
`setCpuFrequencyMhz`, `psramInit`, NVS initialization, `initVariant`, and the
creation of the Runtime setup task. GNU ld wraps the original entry; the original
Arduino startup body is retained. GPIO1 is staged HIGH and input/output-enabled
before releasing an old hold; it is held again and physically read back. Failures
retain their first operation and reach the existing Runtime startup gate, which
stops product startup without resetting/releasing the rail.

This is a repair to the demonstrably late peripheral-rail assertion. It is not a
diagnosed CPU-latch repair or proof that the reported battery-only boot failure is
resolved. ROM, second-stage bootloader, IDF hardware initialization (including
initial PSRAM setup), constructors, and scheduler startup all precede this hook.
A failure in those stages cannot be repaired or fully observed by this hook.

The stock bootloader, CPU and flash frequencies, flash/PSRAM modes, brownout
threshold and protection, GPIO2/5 and USB pins are unchanged. The wrapper only
writes GPIO1 and a 60-byte RTC diagnostic record; it never accesses NVS or flash.

## Primary reference and actual startup order

Reference: Free-Ink/freeink-sdk commit
`425d200a8ea447326b4b9696e4e47dc84ad9d7f6`.

- [Corrected X4 Pro hardware notes](https://github.com/Free-Ink/freeink-sdk/blob/425d200a8ea447326b4b9696e4e47dc84ad9d7f6/docs/xteink-x4pro-support.md#power-rails)
  identify GPIO1 HIGH as required for the touch rail together with active-low
  GPIO2. They say it does not visibly affect display or SD. They do not establish
  the CPU supply/latch circuit or a battery-only reset deadline.
- [BoardConfig](https://github.com/Free-Ink/freeink-sdk/blob/425d200a8ea447326b4b9696e4e47dc84ad9d7f6/libs/hardware/BoardConfig/include/BoardConfig.h)
  carries GPIO1 as `power.latch0` and clears an old hold before asserting HIGH.
  Its older comments claim display/SD are unpowered without it; those comments
  conflict with the corrected hardware notes. A generic `latch0` field name is
  not evidence of a CPU self-latch.
- [PowerManager](https://github.com/Free-Ink/freeink-sdk/blob/425d200a8ea447326b4b9696e4e47dc84ad9d7f6/libs/hardware/PowerManager/src/PowerManager.cpp)
  holds switched touch/SD rails at their OFF polarity for deep sleep and enables
  deep-sleep pad holding. These are separate rails from GPIO1.
- The exact pinned Arduino 2.0.17 `main.cpp` calls `initArduino` before creating
  `loopTask`; its `esp32-hal-misc.c` calls `initVariant` after CPU-frequency,
  Arduino PSRAM, log, NVS and BT-memory initialization. The new host test compiles
  the unmodified, SHA-256-pinned `main.cpp` and exact `initArduino` body instead
  of assuming that ordering in a hand-written imitation.
- [IDF 4.4.7 startup](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/startup.c)
  initializes core services, runs constructors and component initializers before
  starting the app. The framework's qio_opi sdkconfig enables 240 MHz CPU, boot
  PSRAM, and brownout protection at level 7. `initVariant` cannot cover these
  earlier stages; neither can the new app_main wrapper.
- [IDF GPIO implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/gpio.c)
  routes `gpio_hold_dis`/`gpio_hold_en` through RTC hold operations for RTC-capable
  pins. There is no missing second RTC hold release to add for GPIO1.
- Native USB is HWCDC (board supplies `ARDUINO_USB_MODE=1`). Its `begin()` does
  not wait for enumeration. The new hook contains no USB operations or host
  wait; diagnostics later use Runtime's pre-existing capacity-bounded sink.
- FreeInk's sample uses a different Arduino/IDF generation and inherits a DIO
  flash mode. This is a configuration difference, not evidence that the current
  QIO/80 MHz mode causes the failure. No speculative flash-mode or voltage
  protection change is made.

## Automatic diagnostic lines

At Runtime's startup gate the candidate emits three `X4_BOOT` lines, in every
selected native build (no user command needed):

1. Current boot sequence, interpreted reset reason, raw reset reasons for both
   CPUs, wake cause, and timestamps for app_main entry, initVariant and setup gate.
2. Raw GPIO input and RTC hold registers captured before touching GPIO1, the
   brownout register at entry, and GPIO1's current input reading. The pre-input
   register may read LOW when input sensing was disabled; it is not a voltage
   measurement. GPIO21 is charger STAT, not a verified USB-presence signal, and
   is deliberately not reported as VBUS.
3. Previous checksum-valid record, including last phase/operation and timestamps.
   Phase 1 means app_main/pin operation, 2 means GPIO1 ready before Arduino startup,
   3 means initVariant reached, 4 means Runtime setup gate reached. Operations
   1–7 are RTC mux, stage HIGH, configure, confirm HIGH, release hold, apply hold,
   and readback. An operation remaining nonzero is the failed/pending operation.

The record is sealed after every boundary with a checksum; a interrupted update
is invalidated. Retention is diagnostic only and never changes boot policy. The
`checksum_valid` field says only that the bytes validate. RTC RAM is not guaranteed
to survive brownout, external reset or complete power loss; a missing record
cannot prove that the CPU never ran. A valid record across a power-on-class reset
may be an older record. Sequence numbers likewise are not persistent flash counts.
The wrapper cannot retain a failure before it runs. USB output is best effort;
no host is required and no output retry blocks startup.

## Required physical check

Hardware and flashing are unavailable in this task. On the authorized test unit,
start with a sufficiently charged battery and note the exact installed image.
Test battery-only cold start and RST with no USB cable; test USB-powered start,
then unplug USB and test RST; test deep wake by button and timer. Record whether
new pixels appear, whether touch responds and whether a later USB connection
shows an earlier `X4_BOOT` record. Preserve reset reasons and the last recorded
phase before interpreting a failure. Distinguish a CPU that never starts from
one that boots but cannot power peripherals or render the panel. If no app_main
breadcrumb can be recovered, a serial/logic/power trace before application entry
is needed to distinguish ROM strap, flash/PSRAM startup, supply collapse and
brownout; GPIO1 timing alone is insufficient evidence.
