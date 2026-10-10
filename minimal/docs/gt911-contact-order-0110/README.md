# GT911 0.1.10 contact-order candidate

Production checkpoint: public `6590cbdcd7203ac1c2e58c84225e34d8df8602a3`,
tree `f7591b8d9e444fe9b569c1783be08793483ea057` (local `6a4ab814`).
The delivered X4 0.1.57 remains frozen on GT911 0.1.9.

## Failure and bounded repair

An atomic GT911 report could publish an existing contact's MOVE before a newly
added second contact's DOWN. PortableTouch's snapshot timestamp check cannot
reliably identify report membership. A later unchanged ready report advances
that timestamp; slow-display capture can also put later reports ahead of
logical dispatch. The actual production Clock could launch Springboard from
the MOVE before it received the second-DOWN cancellation.

The provider now emits removed-contact UPs, new-contact DOWNs, retained-contact
MOVEs, then Home. IDs remain sorted within each phase; every edge, coordinate,
timestamp, subscriber stream and cross-report order is preserved. Validation,
sequence-capacity checks, one-report polling, GPIO/I2C operations, ACK handling,
sleep lifecycle and retention behavior are unchanged. No extra queue or heap
allocation is introduced. No shared app/reducer production change is required.

## Source-bound results

- 87 provider cases in normal and ASan/UBSan; the 20,000-report single-contact
  source-equivalence oracle remains unchanged. Both added ordering controls
  fail against the original driver and pass against the candidate.
- 87 ordered consumer runs (normal, ASan, UBSan); repeated reports, all contacts
  released before dispatch, independent earlier moves/taps and same-tick
  recontacts are covered. Original 0.1.9 retains its separate 75-run control.
- Actual selected sparse Home, with every .57 target-receipt production define:
  36 cases across normal and ASan/UBSan; fast, 200 ms and permanently busy
  displays. Busy display still permits approximately 1 ms logical dispatch;
  at the existing 10-second deadline custody is retained without handoff.
  Submitted pixels stay immutable. Independent earlier swipes, rapid Points
  block taps and explicit launch-refusal retry remain valid.
- Legacy Clock and actual-provider rapid-tap controls: 40 cases. Six original
  0.1.9 failures are retained rather than counted as passes.
- Real Runtime/scene/shared-keyboard: 96 cases per normal/sanitized mode,
  20,808 contact results per mode, identical outputs. Includes overlapping
  contacts, repeated keys and 200/133/80/60 ms cadence; 1/17/2300 ms displays;
  explicit virtual CPU cost. Maximum modeled capture gap: 5.632 ms.
- Actual Touchpad/Buttons: 20 cases per normal/sanitized mode, including
  horizontal, vertical and free two-finger scrolling. Baseline/candidate action
  and cleanup logs and all 106 captured frames are byte-identical.
- Clean Xtensa build: 22,284 bytes, SHA256
  `dd42e5cd78e40cfca380dcfc4ac28e0df7f43874fdf21e8655d68ace82ae22c1`;
  181 relative targets validated, only memcpy/memset/strcmp imports,
  t5_driver_get export, no s32c1i instruction.

System test source and original failures are published at
`e9576b3b02d01cde1a5b6d040d7f3b8270c70131`, branch
`test/gt911-contact-order-0110`, in RiscRTE-System-Apps. See its
`docs/recovery/clock-gt911-0110` for exact commands, target flags and traces.
Local test/custody details are in `qualification.json` and adjacent receipts.
`rebuild-target.sh` takes explicit clean SOURCE, SDK, READER, CC, PYTHON and
new OUTPUT paths; target SDK header hashes are retained in `target.json`.

No physical device test, product assembly, default switch, tag or release is
claimed. The Watch FT6336U ordering defect is separately reproduced and is not
fixed by this X4 candidate. No workflow run was associated with this isolated
source checkpoint; the focused host/sanitizer/target gates above are the proof.
