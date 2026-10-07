# Sparse Clock product integration boundary

This branch starts from the published sparse sleep client PR12 at
`1cc18a9c998a3d041cf25364fb15c98449fe99d3`, whose parent is the immutable
X4 desk-clock0.1.7 product `f6c12e00e54a85fb2c09517ffad77c67c5b1851b`.

The first packaging slice adds an explicit, currently unselected
`sparse_clock` argument to the grant mapper. It does not add a command-line
option, change a source lock, alter a boot document, or produce a new firmware.
Current Light and full-graph desk-clock builds keep their existing grants and
twelve declared-requirement ceiling.

When the complete future profile is selected deliberately:

- Only the default Clock may receive `runtime.provider-promotion@1`, instance0.
- Only Clock and Settings may receive `runtime.realtime-control@1`, instance0.
- The default Clock must declare both, in addition to its existing twelve
  typed requirements, including retained wake and power.
- Declared requirements may then use Runtime's sixteen-entry bound. Live
  capability grants still have a maximum of sixteen; this does not enlarge it.
- No current app acquires additional authority merely because the mapper
  knows these names. Missing selections, wrong versions and excess capacity
  fail closed. Promotion never accepts a caller-provided provider list.

Eight focused grant tests plus the existing five grant and six desk-bundle
tests pass. They cover every current app, exact instance0 mapping, required
profile flags, wrong native versions, fourteen/sixteen/seventeen boundaries,
duplicate handling, and unchanged existing app policies.

## Remaining activation gates

The future boot policy must remain eager until the actual sparse Clock,
checked Settings time policy, native invocation-retention fix, SDK source
custody and complete-store admission are integrated and tested together.
The native candidate must be the exact locked source; a version string alone
is insufficient proof of the required APIs. Timer boot must retain the alarm
dependency closure and measure its actual provider starts. A partial promotion
cannot be described as sparse or automatically return to timer-only sleep.

This checkpoint is packaging preparation, not a bootable sparse product or
evidence of physical wake reliability or battery current. Existing0.1.6 and
0.1.7 BIN bytes remain unchanged.
