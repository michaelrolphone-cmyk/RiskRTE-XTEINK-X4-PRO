#pragma once
// X4 provider composition for the shared Reader Home/activity runtime.
// Historical entry-point names are retained for the boot diagnostics contract.
void x4DiagnosticSetup(bool deskClockUserWake = false);
// Board readiness/diagnostics only; normal activity execution is in main loop.
bool x4DiagnosticLoop();

// Reuse ordinary verified SD composition for the existing retained clock path.
// Timer resume binds display only, without UI, settings, touch or battery.
bool x4BeginClockDisplay();
bool x4DrainProvidersForSleep();
bool x4ReleaseDisplayForSleep();
bool x4RestoreDisplayAfterSleep();

// True only after all boot prerequisites, before the first Reader frame.
bool x4ReaderStartupReady();
void x4ReaderActivityScheduled();
