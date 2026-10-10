X4 completed controls test cohort, product 0.1.16

Includes the 0.1.15 early peripheral-rail startup candidate, reset breadcrumbs
and automatic plain diagnostics. Battery-only cold/RST boot remains physically
unqualified; this build does not establish the hardware failure's root cause.

Home now uses the corrected native fonts and functional Points view, with no
NOVA-7 wordmark. Clock/Springboard use completed-image crossfade; Quick Controls
follows the drag and keeps its state through pending frames. Backlight has an
explicit OFF/ON control with last-level restoration. Both landscape desk-clock
directions are persistent and independent of portrait UI flip.

The top-right key on Home waits for release, locks into the typed deep desk-clock
lifecycle, and a later physical key press wakes to Home. Timer wakes retain the
locked clock. The Settings row reports the actual fixed key action; it does not
expose a conflicting manual Light/Deep selector. Other apps retain Home/Back.
Desk rendering uses normal QUALITY partial updates and periodic CLEAN refresh,
without high-speed settling. Interactive UI explicitly selects LOW_LATENCY.

Fast scenes retain accepted pixel polarity and 2.3-second resident settling.
While awake in fast mode, one resident pulse runs 30 seconds after settling or
maintenance completes, with no pixel retransmission or app rerasterization.
New frames supersede it after the active pulse; quality/sleep cancels it. This
may help the observed gray drift; it is not a repair of physical panel damage.

Settings timezone/region and Wi-Fi lists have bounded touch/momentum scrolling.
Other list ports continue separately. Files now uses the real SD volume at 9,
with matching compiled selector, manifest and grant; text/hex preview, nested
paths and file operations are checked. General Open requires an installed
handler; this bundle adds none. Wi-Fi OFF/airplane policy is enforced by the
native client independently of whether radio toggles are shown. Missing radio
preferences still mean Bluetooth OFF and Wi-Fi awaiting explicit Scan/Connect.
Provider ready is not a network connection. SDK failures log exact codes.

All 16 selected app targets and the complete provider cohort are source-bound.
Focused normal/sanitized controller, gesture, lifecycle, path/operation, waveform
state-machine and target checks passed before full bundle admission. Physical
key wake/current, normal-waveform quality, scrolling and radio connectivity need
device confirmation. Hosted CI was not awaited for this test image.

Not yet included: BLE telemetry controls, product OTA/App Store, Serial Monitor,
automatic low-battery/idle policy and remaining scrolling/language adoption.
Audio and LoRa applications require hardware absent from the verified X4 map.

UC8279 only. Flash full 16 MiB BIN at 0x0; firmware, NVS and app-data are replaced.
