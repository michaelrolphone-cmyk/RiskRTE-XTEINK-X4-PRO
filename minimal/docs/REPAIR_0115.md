X4 battery-startup investigation, product0.1.15

This power-first test candidate moves GPIO1 peripheral-rail assertion before
Arduino USB/CPU/PSRAM/NVS setup through a verified wrapper around app_main.
It preserves the existing safe HIGH/hold sequence. GPIO1 is documented as a
peripheral/touch rail, not a proven CPU self-latch. This corrects late assertion;
it does not establish the root cause of the reported battery-only failure.
ROM, bootloader and earlier IDF initialization still precede the new hook.

Automatic X4_BOOT statements report reset/wake causes, entry/variant/setup
timestamps, GPIO/hold/brownout register snapshots and the previous valid RTC
phase record. RTC breadcrumbs may not survive supply loss and do not prove
hardware power integrity. Brownout protection and flash/CPU settings are
unchanged. No host wait or persistent flash logging is introduced.

Runtime0.1.62 preserves all13 early startup statements in the existing bounded
USB TX ring until a host is ready, emits complete bounded provider-failure text,
and logs exact native Wi-Fi/Bluetooth SDK initialization/cleanup errors without
credentials. Provider ready is not reported as a connected radio.

Fast display provider0.1.4 uses the accepted corrected polarity and extends
resident-image settling from1.6 to2.3 seconds as requested. All16 application
ELFs remain identical to0.1.14. The separate new Home/desk-clock/list/UI cohort
is not included in this focused power checkpoint.

Pinned startup ordering/failure tests, sanitizers, exact target call-path proof,
provider builds and full-store admission pass. This is a testable startup
candidate; battery-only/RST hardware behavior remains to be checked. Hosted CI
was not awaited. No device was flashed here.

UC8279 only. Flash full16MiB BIN at0x0. This replaces firmware, NVS and app-data.
