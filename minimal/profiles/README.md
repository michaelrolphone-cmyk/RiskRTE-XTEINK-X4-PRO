# Explicit hardware profiles

Generate with `minimal/scripts/generate_profile.py --panel ssd1677|uc8279` and
`--output DIR`. Neither panel is the default: the recovered source supports
both, but does not identify the controller fitted to this user's physical board.
The produced hardware-only graph includes all nine X4 providers with exact
instance bindings. It is a composition fixture, not a runnable product store:
application ELFs/policies and ordinary shared services must still be supplied.

Touch requires the shared Runtime touch.i2c@2 extension pinned in the source
lock. Runtime code stays in its own repository. The host profile test feeds
actual materialized records into the full panel and SD driver test suites,
covering zero-filled unused ABI slots rather than handwritten assumptions.

Panel is native800x480 MONO1 with portrait application rotation90; touch reports
logical480x800. The declared SPI frequency is metadata, not measured GPIO-driven
cadence. The native one-bit SD transport uses its separate GPIO bank. RTC
protocol identity is explicit without inventing an unidentified silicon vendor.
Battery and RTC are mandatory selected providers, not optional stage omissions.
