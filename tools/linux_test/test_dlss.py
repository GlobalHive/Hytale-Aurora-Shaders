from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import linux_dlss
import linux_install
import linux_hook


class DetectionTests(unittest.TestCase):
    @patch.dict(os.environ, {"AU_DLSS": "auto"})
    @patch.object(linux_dlss.prepare_dlss_linux, "main")
    @patch.object(linux_dlss, "nvidia_candidate", return_value=(False, "no RTX GPU"))
    def test_unsupported_does_not_download(self, candidate, prepare):
        self.assertFalse(linux_dlss.configure())
        prepare.assert_not_called()

    @patch.dict(os.environ, {"AU_DLSS": "0"})
    @patch.object(linux_dlss, "nvidia_candidate")
    def test_explicit_disable_skips_detection(self, candidate):
        self.assertFalse(linux_dlss.configure())
        candidate.assert_not_called()

    @patch.dict(os.environ, {"AU_DLSS": "auto"})
    @patch.object(linux_dlss.prepare_dlss_linux, "main")
    @patch.object(linux_dlss, "nvidia_candidate", return_value=(True, "RTX fixture"))
    @patch.object(linux_dlss.subprocess, "run")
    def test_ngx_probe_decides_support(self, run, candidate, prepare):
        run.side_effect = [subprocess.CompletedProcess([], 0),
                           subprocess.CompletedProcess([], 0, "DLSS supported\n")]
        self.assertTrue(linux_dlss.configure())
        prepare.assert_called_once()
        self.assertEqual(run.call_count, 2)
        self.assertEqual(run.call_args.kwargs["timeout"], 60)

    @patch.dict(os.environ, {"AU_DLSS": "auto"})
    @patch.object(linux_dlss.prepare_dlss_linux, "main")
    @patch.object(linux_dlss, "nvidia_candidate", return_value=(True, "RTX fixture"))
    @patch.object(linux_dlss.subprocess, "run")
    def test_candidate_without_ngx_support_falls_back(self, run, candidate, prepare):
        run.side_effect = [subprocess.CompletedProcess([], 0),
                           subprocess.CompletedProcess([], 1, "NGX unsupported")]
        self.assertFalse(linux_dlss.configure())

    @patch.dict(os.environ, {"AU_DLSS": "auto"})
    @patch.object(linux_dlss.prepare_dlss_linux, "main", side_effect=OSError("offline"))
    @patch.object(linux_dlss, "nvidia_candidate", return_value=(True, "RTX fixture"))
    def test_download_failure_keeps_normal_install_available(self, candidate, prepare):
        self.assertFalse(linux_dlss.configure())

    @patch.dict(os.environ, {"AU_DLSS": "1"})
    @patch.object(linux_dlss, "nvidia_candidate", return_value=(False, "driver unavailable"))
    def test_explicit_requirement_reports_failure(self, candidate):
        with self.assertRaisesRegex(ValueError, "unsupported"):
            linux_dlss.configure()

    def test_cached_proxy_rebuilt_when_dlss_support_changes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "build").mkdir(); (root / "hook").mkdir()
            for name in ["hook/aud_dlss.c", "hook/hook.c", "hook/sdl_proxy.map", "build.sh"]:
                (root / name).touch()
            proxy = root / "build/libaurora-sdl.so"
            proxy.write_bytes(linux_hook.MARKER)
            with patch.object(linux_install, "ROOT", root), \
                 patch.object(linux_install.linux_dlss, "configure", return_value=True), \
                 patch.object(linux_install.subprocess, "run") as run:
                self.assertEqual(linux_install.build_proxy(), proxy)
                self.assertEqual(run.call_args.kwargs["env"]["AU_DLSS"], "1")
            proxy.write_bytes(linux_hook.MARKER + linux_hook.DLSS_MARKER)
            with patch.object(linux_install, "ROOT", root), \
                 patch.object(linux_install.linux_dlss, "configure", return_value=False), \
                 patch.object(linux_install.subprocess, "run") as run:
                linux_install.build_proxy()
                self.assertEqual(run.call_args.kwargs["env"]["AU_DLSS"], "0")


if __name__ == "__main__":
    unittest.main()
