# Future desk-clock foundation integration

This isolated development branch composes completed typed mechanisms. It does
not replace the delivered 0.1.6 image or yet activate a deep-sleep Clock app.
Product identity 0.1.7 is reserved for the future integrated build, not a hardware
acceptance claim. Current app artifacts remain separately pinned until the
Clock and Settings integration is complete.

## Exact component custody

- Base product: f190c1192ce3cee97c0ede01cf9d57654df6508d.
- Panel 0.1.20: 5c709fdc8e97d1fce867c0b17ea3986aeca3b146 (PR5).
- SD 0.2.6: 10132268a126ad3b14a9e6464404aa001df9c176 (PR6).
- GT911 0.1.7: cc89c6d871a2f36195e1babef0a1d3e83865e332 (PR7).
- Power 0.1.1: cf0120d78453fb9985b6334b0e7d2a8c53432e49 (PR8).
- Runtime 0.1.47: 3c39aa7ac50ecd7f6da0296a62a2d7af066f82d1 (Runtime PR37).
- Combined canonical SDK/helper: aac8c06d3221139084acd0cfc64f7b0ba194a97a
  (Reader PR445), composed from PR442/443/444 on the frozen shared cccfa baseline.
- Future app policy/renderer: System Apps 3702b9a594196bb5cb546732a5d4aa668c88ff91.
- Shared store-admission helper: Watch d2cce9f9c5119cd59e08f73f7f573d2eba91d75d.

The integration preserves the component parent commits and source code. Only
shared pins, CI composition and this product-development identity are reconciled.
Original migrated sources and the paused Reader PR350 remain unchanged.

## Required transaction

Only an explicitly authorized Clock receives power and retained-record grants.
Close touch subscriptions and all SD file/directory handles, finish presentation,
and preserve the exact confirmed visible scene. Prepare display, touch and the
new zero-handle SD transaction with their typed methods. Do not mix the old
terminal storage API into the reversible transaction. Keep the owned retained
record staged but uncommitted until native terminal entry. Recompute the minute
deadline after preparation and restore/repaint if it crossed a boundary.

A successful deep call never returns. Ordinary refusal requires checked reverse
restoration; MEDIA_UNAVAILABLE must remain visibly unavailable rather than
pretending a filesystem is ready. Retained/uncertain cleanup forbids normal I/O
or resource release. A new boot reacquires fresh capabilities; only a classified
timer wake and validated same-app/version/cohort record may resume desk mode.
GPIO/other/reset wakes must return to normal UI without replaying a held contact.

The app orchestration, selected face/settings, alarm and radio policy, qualified
time sampling and timer-boot efficiency are still integration work. The current
Runtime starts the full admitted graph; this checkpoint does not claim Reader's
minimal timer-only initialization or physical power parity.

## Verification

The combined Runtime/SDK passes panel, SD, GT911, power and navigation suites
under ASan/UBSan, plus the existing real Clock light-sleep tests. Both panel
profiles and default/sleep graphs admit through production Runtime before I/O;
invalid ownership/dependency graphs are rejected. Settings' existing light-only
negative regression remains enforced. CI builds all selected ordinary Xtensa
packages with exact locked dependencies. Full app/cohort validation is required
again once the new Clock is wired. Physical wake, screen retention and current
measurement remain unqualified.
