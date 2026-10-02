import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("prepare", Path(__file__).parents[1] / "tools/prepare.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class PatchTests(unittest.TestCase):
    def test_exact_anchor(self):
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "source.c"
            f.write_text("before ANCHOR after")
            m.replace_once(f, "ANCHOR", "NEW")
            self.assertEqual(f.read_text(), "before NEW after")
    def test_missing_or_duplicate_anchor_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "source.c"
            for text in ("", "ANCHOR ANCHOR"):
                f.write_text(text)
                with self.assertRaises(RuntimeError):
                    m.replace_once(f, "ANCHOR", "NEW")
                self.assertEqual(f.read_text(), text)
    def test_existing_destination_not_overwritten(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(RuntimeError):
                m.prepare(None, Path(d))

    def test_build_profile_exists_and_safe_defaults(self):
        root = Path(__file__).parents[1]
        script = (root / "tools/build.sh").read_text()
        self.assertIn("devices/sdkconfig.stopwatch", script)
        config = (root / "overlay/esp32/devices/sdkconfig.stopwatch").read_text()
        for setting in ("CONFIG_HOMEHUB_BUTTON_GPIO=1", "CONFIG_HOMEHUB_OTA_ENABLED=n",
                        "CONFIG_HOMEHUB_TUNNEL=n", "CONFIG_HOMEHUB_SUPPORT_BUG_REPORT=n"):
            self.assertIn(setting, config)
