#!/usr/bin/env python3
from pathlib import Path
import unittest
TEXT = (Path(__file__).resolve().parents[1] / "Drivers/x4pro_panel/driver.c").read_text()

# Actual full/partial waveform bytes, source-plane inversion, windows and
# shutdown ordering are checked by x4pro_panel_state_test.c against the complete
# provider through the SPI-pin trace. Keep only stable geometry/probe constants
# here; equivalent runtime branch expressions must not break a text-only gate.
class PanelSequence(unittest.TestCase):
    def test_pinned_ssd1677_window_and_booster(self):
        self.assertIn("command(0x0C);", TEXT)
        self.assertIn("{0x00, 0x00, 0x1F, 0x03}", TEXT)
        self.assertIn("{0xDF, 0x01, 0x00, 0x00}", TEXT)
        self.assertIn("data1(0xDF); data1(0x01);", TEXT)
        self.assertIn("transfer_plane(0x24, deadline_ms)", TEXT)
        self.assertIn("transfer_plane(0x26, deadline_ms)", TEXT)
        self.assertIn("FRAME_BYTES", TEXT)
        self.assertIn("timeout_ms > 0", TEXT)
        self.assertIn("PRESENT_FAILED", TEXT)

    def test_uc8279_full_refresh_and_identity_gate(self):
        self.assertIn("ver[2] == 0x68", TEXT)
        self.assertIn("confirm_ver[i] == ver[i]", TEXT)
        self.assertIn("uc_transfer_plane(0x13, false, deadline_ms)", TEXT)
        self.assertIn("uc_transfer_plane(0x10, true, deadline_ms)", TEXT)
        self.assertIn("command(0x61); data1(0x03); data1(0x20); data1(0x02); data1(0x58)", TEXT)
        self.assertIn("command(0x00); data1(0x17); data1(0x4D)", TEXT)
        self.assertIn("command(0x12);", TEXT)

if __name__ == "__main__":
    unittest.main()
