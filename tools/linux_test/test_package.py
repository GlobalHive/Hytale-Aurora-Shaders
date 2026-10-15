from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from package_release import package


class ReleaseProfileTests(unittest.TestCase):
    def make_stage(self, root):
        stage = root / "Aurora-Shaders"
        (stage / "shaderpack/include").mkdir(parents=True)
        settings = "#define AU_PRESET 2\n#define AU_DLSS 0\n#define AU_GI_DENOISER 1\n#define AU_PATH_TRACING 0\n"
        (stage / "shaderpack/include/AU_Settings.glsl").write_text(settings)
        (stage / "shaderpack/presets").mkdir()
        (stage / "shaderpack/presets/default.glsl").write_text("// Aurora preset: Default\n" + settings)
        return stage

    def test_frozen_default_is_bundled(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = self.make_stage(root)
            destination = root / "release.zip"
            package(stage, destination)
            with zipfile.ZipFile(destination) as archive:
                self.assertIn("Aurora-Shaders/shaderpack/presets/default.glsl", archive.namelist())

    def test_custom_profiles_still_cannot_be_published(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = self.make_stage(root)
            (stage / "shaderpack/presets/preset-00.glsl").write_text("private settings")
            with self.assertRaisesRegex(ValueError, "Private/generated path"):
                package(stage, root / "release.zip")

    def test_missing_default_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = self.make_stage(root)
            (stage / "shaderpack/presets/default.glsl").unlink()
            with self.assertRaisesRegex(ValueError, "Missing frozen Default"):
                package(stage, root / "release.zip")


if __name__ == "__main__":
    unittest.main()
