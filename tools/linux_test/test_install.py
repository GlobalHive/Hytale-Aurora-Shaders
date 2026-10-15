from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import linux_hook
from prepare_shaders import classify


class LinuxInstallTests(unittest.TestCase):
    def test_settings_merge_preserves_new_options_and_disables_windows_features(self):
        defaults = "#define AU_SCREEN_BRIGHTNESS 1.0\n#define AU_LIGHT_CACHE_SECONDS 1.0\n#define AU_DLSS 0\n#define AU_GI_DENOISER 1\n"
        previous = "#define AU_SCREEN_BRIGHTNESS 1.4\n#define AU_DLSS 1\n#define AU_GI_DENOISER 2\n"
        merged = linux_hook.merge_settings(defaults, previous)
        self.assertIn("AU_SCREEN_BRIGHTNESS 1.4", merged)
        self.assertIn("AU_LIGHT_CACHE_SECONDS 1.0", merged)
        self.assertIn("AU_DLSS 0", merged)
        self.assertIn("AU_GI_DENOISER 1", merged)

    def test_ice_material_has_transmission_tag(self):
        self.assertEqual(classify("Ice_Blue"), (1, 6))
        self.assertEqual(classify("Glass_Clear"), (1, 7))
        self.assertEqual(classify("Wood_Oak_Planks"), (0, 0))

    @patch.object(linux_hook, "require_stopped")
    def test_install_update_uninstall_and_game_update(self, stopped):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client = root / "Client"
            client.mkdir()
            original = b"\x7fELF\x02\x01" + b"\0" * 12 + b"\x3e\x00" + b"original SDL library"
            library = client / "libSDL3.so"
            library.write_bytes(original)
            proxy = root / "proxy.so"
            proxy.write_bytes(original + linux_hook.MARKER)
            pack = root / "pack"
            (pack / "include").mkdir(parents=True)
            defaults = "#define AU_SCREEN_BRIGHTNESS 1.0\n#define AU_LIGHT_CACHE_SECONDS 1.0\n#define AU_DLSS 0\n"
            (pack / "include/AU_Settings.glsl").write_text(defaults)
            installed = linux_hook.install(client, proxy, pack)
            self.assertTrue(linux_hook.installed(client))
            self.assertEqual((client / linux_hook.BACKUP).read_bytes(), original)
            settings = installed / "include/AU_Settings.glsl"
            settings.write_text("#define AU_SCREEN_BRIGHTNESS 1.5\n#define AU_DLSS 1\n")
            presets = installed / "presets"
            presets.mkdir()
            custom = "// Aurora preset: Cave\n#define AU_SCREEN_BRIGHTNESS 1.5\n"
            (presets / "preset-00.glsl").write_text(custom)
            masks = installed / "sss/Blocks/Foliage/Leaves"
            masks.mkdir(parents=True)
            (masks / "Oak.png").write_bytes(b"custom mask fixture")
            linux_hook.install(client, proxy, pack)
            self.assertIn("AU_SCREEN_BRIGHTNESS 1.5", settings.read_text())
            self.assertIn("AU_LIGHT_CACHE_SECONDS 1.0", settings.read_text())
            self.assertIn("AU_DLSS 0", settings.read_text())
            self.assertEqual((presets / "preset-00.glsl").read_text(), custom)
            self.assertEqual((masks / "Oak.png").read_bytes(), b"custom mask fixture")
            linux_hook.uninstall(client)
            self.assertEqual(library.read_bytes(), original)
            self.assertTrue(settings.is_file())
            linux_hook.install(client, proxy, pack)
            updated = original + b"newer game version"
            library.write_bytes(updated)
            linux_hook.uninstall(client)
            self.assertEqual(library.read_bytes(), updated)


if __name__ == "__main__":
    unittest.main()
