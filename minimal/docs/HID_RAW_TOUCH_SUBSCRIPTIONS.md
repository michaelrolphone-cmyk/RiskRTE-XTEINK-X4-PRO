# HID raw-touch startup correction

GT911 0.1.8 supports the canonical four raw-touch subscribers with independent
bounded queues. The shared application adapter owns one subscriber for UI,
Home and Quick Controls. Bluetooth Touchpad and Bluetooth Buttons open another
subscriber for pairing confirmation and HID reports. GT911 0.1.7 accepted only
one, so the second subscribe returned zero and both apps reported
`Raw touch start failed` before opening the Bluetooth session.

The provider now copies each event into each live subscriber's queue. Consuming,
unsubscribing or overflowing one queue cannot drain or corrupt another. Tokens
remain monotonic across stop/start; stale/foreign tokens fail. No allocation or
new hardware authority is added. Existing mutex/owner checks remain. Power
preparation and unload still refuse while any subscription is live, leaving
all subscriptions usable after refusal. Ambiguous hardware reports invalidate
all current queues as before; consumers must observe a fresh neutral state.

The original provider reproduces the second-subscription rejection when linked
to the real HID app and shared adapter. The corrected source passes both apps'
reconnect, pair accept/reject/move, mouse/key reports, Home/Back and cleanup
scenarios, 32 process runs across normal and ASan/UBSan configurations. The
strict provider suite covers 77 scenarios in each mode, including independent
queue overflow, stale tokens, ownership, reset/probe, retained cleanup and sleep.

Run `minimal/test/run_hid_gt911_test.py --utilities <Utilities checkout>
--system <System checkout> --sdk <canonical generated SDK>` for the app/adapter/
provider composition. Physical GPIO, I2C reports and time are simulated; this
is not hardware pairing qualification. The wrapper corrects the canonical
fixture's failure injection order: a refused unsubscribe must not first
unsubscribe the real provider and discard its token. It changes no production
application code and retains the original assertions.
