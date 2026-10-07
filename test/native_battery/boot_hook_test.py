#!/usr/bin/env python3
"""Protect the optional battery's first-render integration ordering."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BatteryBootHook(unittest.TestCase):
    def test_prime_after_storage_and_input_before_home(self):
        source = (ROOT / "src/platform/X4DiagnosticBoot.cpp").read_text()
        setup = source.split("void x4DiagnosticSetup(bool deskClockUserWake) {", 1)[1].split("bool x4DiagnosticLoop()", 1)[0]
        prime = setup.index("nativeBatteryTick();")
        for dependency in ("Storage.bindVolume(volume)", "nativeNavigationTick();", "nativeTouchTick();"):
            self.assertLess(setup.index(dependency), prime)
        self.assertLess(prime, setup.index("prepareReaderApplication(deskClockUserWake);"))
        # An absent optional provider must never become a Home readiness gate.
        line = next(line for line in setup.splitlines() if "nativeBatteryTick();" in line)
        self.assertEqual(line.strip(), "nativeBatteryTick();")


if __name__ == "__main__":
    unittest.main()
