# X4 .58 touch performance: read-only source and trace audit

Hardware feedback reported broad touch slowdown on .58. This audit does not
identify a hardware root cause and makes no production or rollback changes.
The controlled .13-only successor keeps GT911 .10 and all other .58 changes.

## GT911

The .58 driver SHA256 is ddb898395bcfb4ca126b284f01b288274da97f34efd2e2e63c9347dff8ed4cb5.
The only .9 -> .10 delta splits additions from retained-contact motion. Poll
limits, I2C transactions, ACKs, clocks, queues and delays are unchanged. For a
single contact the extra work is one in-memory lookup, without a new wait.

run_gt911_trace.py derives the unchanged 20,000-report single-contact reference
test and adds trace instrumentation. Exact .9 and .58 .10 pass normally and
under ASan/UBSan with identical semantic hashes, transaction/lock/clock counts,
snapshots and modeled time. gt911-trace-qualification.json records all commands,
source hashes and outputs. This is not physical CPU/cache timing evidence.

The .57/.58 store receipts compare as 90/97 identical paths. Changes are panel,
touch and Buttons ELF/manifest pairs plus cohort.json. Shared application ELFs
other than Buttons are byte-identical; native regions remain unchanged.

## Panel paths

Selected .58 panel source is 879ebef75b47f723655253ec04428f7a48e487b2 in
/workspace/shared/x4-fast-panel-014-external-source; driver SHA256
1dd623d2770772cd86ca3c01fd92e31be02c92c9a018ef07e56652036fc991c9.
Delivered .57 .13 driver SHA256 is
82ed04151d08364c5e893501af04f1ff5cb2f9a6492649cf08fd40631c71acb0,
confirmed in x4-wifi-ui-composition-057 commit 28ada8dae97dcd2ba071ab43e2d92c8217e10f2c.

- .14's effective 40/80/160-row windows send OLD then NEW; .13 sends NEW only.
  An 800x160 window goes from 16000 to 32000 payload bytes and 32 to 64 exchanges.
  Ideal 20MHz payload time is 6.4 to 12.8ms across slices; this is arithmetic.
  Effective 480-row updates remain absolute. Driver lines 296-313,708-737,969-975.
- Runtime yield calls GraphV2 poll before scheduler delay. Its nominal 10ms
  aggregate and <=8ms provider budgets surround synchronous callbacks and cannot
  preempt them. Runtime.cpp 989-1013, ProviderGraphV2.cpp 333-350 in frozen .2.2.
- Both providers' settle_sync_chunk performs up to 32x512-byte SPI exchanges
  before the outer time check. Ideal wire time alone is 6.554ms; begin/end,
  pin routing, memory copies and SDK/DMA overhead are additional. Driver 372-388,
  492-498,554-558,621-660. A hypothetical 1ms/exchange would yield 32ms; unmeasured.
- NativeSpi.inc 75-105 queues DMA then waits for completion. CpuPort.cpp 967-984
  caps a three-wire requested wait at 8 ms; tick conversion adds one tick.
  Other RTOS tasks can execute while the foreground app/input stack waits.
- submit scans all 48000 framebuffer bytes without time checkpoints, including
  small damage. Completion copies the window without checkpoints. Both costs
  are inherited. Driver 391-458,959-976.
- present_status is lock/validation/state copy only. The selected async adapter
  does not use blocking wait_present. Adapter 1273-1290,1309-1314.

## Limits of existing panel cadence evidence

/workspace/shared/x4-fast-panel-014-qualification cadence logs use ideal SPI
wire cost only, fixed 20ms BUSY and a tiny 16x40 partial window. GPIO-cost input is
ignored by panel_cadence_start; no native DMA/SDK/CPU scheduling overhead is
charged. Both versions report 22ms frame elapsed and 2 ms maximum touch gap while
payload doubles 80 to 160bytes. The 8 ms case fails an inherited idle assertion;
20/50ms cases are not reached. These tests do not exclude hardware delay.

## Narrow next evidence

Continue the controlled panel-only comparison. Distinguish logical input/action
latency from visible feedback. Compare <=160-row and 480-row updates, continuous
interaction, the 2300ms settling window and settled idle. If later requested,
measure submit/poll duration, per-exchange native SPI duration and raw-touch
collection on hardware. A host model should charge per-exchange overhead and
exercise 800x160 damage. No instrumentation or speculative fix was adopted here.
