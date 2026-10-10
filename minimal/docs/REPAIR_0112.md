X4 UC8279 probe repair and startup logging, product0.1.12

This is a focused repair of0.1.11. Native three-wire receive now leaves MOSI
output disabled. The previous sequence selected input and then made a redundant
ROM routing call that re-enabled output, driving the read wire and causing the
observed all-zero controller identification. The corrected pinned-SDK model
reproduces the old failure and passes the repaired receive sequence. All-zero,
floating and mismatched controller IDs still fail closed.

The fast display provider0.1.1,20MHz transfer, selected lab0.1.5 waveform and
all16 application ELFs are identical to0.1.11. No aggressive lab0.1.6 geometry,
TCON or clock settings are enabled. First/reset/resumed presentation still
establishes OTP history before ordinary fast updates.

Diagnostic builds now allocate an8KiB native USB transmit ring before startup
and after USB recovery, replacing the256-byte default that could drop startup
bursts. Output remains nonblocking without a host. Automatic timestamped boot,
load, input, draw and display statements need no command. RTE_LOG counters still
report any remaining capacity loss or truncation.

The RTC first-boot recovery and permanent/early board keepalive from0.1.11 remain.
New Home typography and animated transitions are preserved separately and are
not added to this repair checkpoint. Physical probe/display/battery behavior
requires device confirmation; host and target tests are not that confirmation.

UC8279 only. Flash the full16MiB BIN at0x0 for the explicit new-install layout;
this replaces firmware, NVS and app-data. No device was flashed here.
