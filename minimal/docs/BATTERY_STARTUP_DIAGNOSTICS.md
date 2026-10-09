# Battery startup investigation

Observed: USB-assisted boot succeeds and the X4 remains running when USB is unplugged. Battery-only/RST startup is still reported to fail. This narrows investigation to startup/reset behavior; it does not prove a particular rail, brownout, USB wait or PMIC cause.

Source comparisons establish that Runtime setup has no Serial-ready wait and pinned Arduino HWCDC begin does not wait for a host. The ordinary logger uses capacity-bounded writes. GPIO1 is the documented peripheral/touch rail, not a demonstrated CPU self-latch. Its existing early HIGH/hold sequencing is unchanged by this diagnostic increment.

Raw GPIO_IN1 and the latched GPIO_STRAP register are captured separately; the low-bank GPIO input snapshot is not a strap latch. No strap mapping is inferred from those raw numbers.

The X4 record grows from 60 to 264 bytes in RTC no-init memory. Its new magic rejects older layouts. Named startup/provider/app statements, boot errors and the first display completion are recorded without flash/NVS writes. The existing Runtime owner and reentry gates protect the optional native observer. Text is copied and bounded; a truncation bit is reported. Touch/move/draw chatter does not call the timer or update the record. The checksum is evaluated at reset recovery, not every log/touch callback.

On the next boot, the usual reset/raw reset/wake/rail fields are followed by the prior milestone timestamp, first-display observation and readable last line. Checksum validity does not prove that RTC memory survived a complete power loss or that the record belongs to a battery-powered attempt. Early ROM/bootloader/IDF failures before app_main cannot be observed by this hook. No retained data changes operational behavior.

Tests use actual pinned Arduino app_main/initArduino and production X4 source for entry order, held pads, each rail failure, reset classes, interrupted writes, record-byte corruption, observer bounds and no touch work. Production Runtime tests verify present/absent/disabled callbacks, absent USB, backpressure, owner and reentry rules. Target proof verifies RTC placement and actual observer call linkage. Physical reproduction remains necessary to establish the startup failure cause.
