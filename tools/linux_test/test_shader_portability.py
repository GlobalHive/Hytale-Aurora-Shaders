from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def shader_sources():
    for folder in ("shaderpack/include", "shaderpack/programs", "tools/mapchunk"):
        yield from sorted((ROOT / folder).glob("*.glsl"))


def code_lines(path):
    text = re.sub(r"/\*.*?\*/", "", path.read_text(encoding="utf-8", errors="replace"), flags=re.S)
    for number, line in enumerate(text.splitlines(), 1):
        yield number, line.split("//", 1)[0]


class ShaderPortabilityTests(unittest.TestCase):
    def test_no_image_size(self):
        # AMD's compiler rejects imageSize() in the game's #version 410 shaders even with
        # GL_ARB_shader_image_size enabled, and the whole program then fails (issue #17).
        found = [f"{path.relative_to(ROOT)}:{number}" for path in shader_sources()
                 for number, line in code_lines(path) if re.search(r"\bimageSize\s*\(", line)]
        self.assertEqual(found, [])


if __name__ == "__main__":
    unittest.main()
