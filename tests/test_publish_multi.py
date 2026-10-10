import tempfile
import unittest
from pathlib import Path
from firmware.scripts.publish_multi import prepare, TARGETS

class MultiReleaseTests(unittest.TestCase):
    def write_inputs(self, folder, version="0.6.38"):
        for key, spec in TARGETS.items():
            sub = folder / spec["dir"]
            sub.mkdir(parents=True, exist_ok=True)
            (sub / spec["ota"].format(version=version)).write_bytes(b"A" * 700_000 + version.encode() + key.encode())
            (sub / spec["factory"].format(version=version)).write_bytes(b"B" * 1_500_000)

    def test_release_contains_both_profiles(self):
        with tempfile.TemporaryDirectory() as work:
            p = Path(work)
            self.write_inputs(p / "inputs")
            release = prepare(p / "inputs", p / "repo", "0.6.38")
            self.assertEqual(set(release["targets"]), set(TARGETS))
            self.assertTrue((p / "repo/docs/manifest-wt32.json").is_file())
            self.assertTrue((p / "repo/docs/manifest-waveshare.json").is_file())
            self.assertTrue((p / "repo/firmware/targets.json").is_file())
            import json
            legacy = json.loads((p / "repo/firmware/version.json").read_text())
            self.assertEqual(legacy["version"], "0.6.38")
            self.assertTrue(legacy["bin"].endswith("/firmware/Mira_Panel.bin"))
            self.assertEqual((p / "repo/firmware/Mira_Panel.bin").read_bytes(),
                             (p / "inputs/wt32/Mira_Panel_WT32_SC01_Plus_0.6.38.bin").read_bytes())
            self.assertEqual((p / "repo/docs/manifest.json").read_bytes(),
                             (p / "repo/docs/manifest-wt32.json").read_bytes())

    def test_missing_target_does_not_publish_partial_release(self):
        with tempfile.TemporaryDirectory() as work:
            p = Path(work)
            self.write_inputs(p / "inputs")
            spec = TARGETS["waveshare-esp32s3-touch-lcd-4.3c"]
            (p / "inputs" / spec["dir"] / spec["factory"].format(version="0.6.38")).unlink()
            with self.assertRaises(FileNotFoundError):
                prepare(p / "inputs", p / "repo", "0.6.38")
            self.assertFalse((p / "repo/firmware/targets.json").exists())

    def test_ota_wrong_version_rejected(self):
        with tempfile.TemporaryDirectory() as work:
            p = Path(work)
            self.write_inputs(p / "inputs", "0.6.38")
            spec = TARGETS["wt32-sc01-plus"]
            (p / "inputs" / spec["dir"] / spec["ota"].format(version="0.6.38")).write_bytes(b"z" * 600_000)
            with self.assertRaises(ValueError):
                prepare(p / "inputs", p / "repo", "0.6.38")
            self.assertFalse((p / "repo/firmware/targets.json").exists())

if __name__ == "__main__":
    unittest.main()
