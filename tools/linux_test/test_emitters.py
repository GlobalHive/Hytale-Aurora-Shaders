from io import BytesIO
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_emitters import build_emitters, texture_tiles


class EmitterClassificationTests(unittest.TestCase):
    def test_model_fallback_does_not_make_sandy_gravel_emissive(self):
        with tempfile.TemporaryDirectory() as temporary:
            assets = Path(temporary) / "Assets.zip"
            lamp = {"BlockType": {"DrawType": "Model", "Light": {"Color": "#987"},
                    "CustomModelTexture": [{"Texture": "Blocks/Lantern.png"}],
                    "State": {"Definitions": {"Block": {"DrawType": "Cube", "Textures": [{"All": "BlockTextures/Soil_Gravel_Sand.png"}]}}}}}
            gravel = {"BlockType": {"DrawType": "Cube", "Textures": [{"All": "BlockTextures/Soil_Gravel_Sand.png"}]}}
            with zipfile.ZipFile(assets, "w") as archive:
                for name, data in {"Lantern": lamp, "SandyGravel": gravel}.items():
                    archive.writestr(f"Server/Item/Items/{name}.json", json.dumps(data))
                for name, colour in {"Blocks/Lantern.png": (250, 220, 80), "BlockTextures/Soil_Gravel_Sand.png": (100, 90, 60)}.items():
                    stream = BytesIO()
                    Image.new("RGB", (32, 32), colour).save(stream, format="PNG")
                    archive.writestr("Common/" + name, stream.getvalue())
            records, report = build_emitters(assets)
            self.assertTrue(records)
            self.assertEqual(len(report), 1)
            self.assertEqual(report[0]["Textures"], ["Common/Blocks/Lantern.png"])
            with zipfile.ZipFile(assets) as archive:
                fingerprint = texture_tiles(archive.read("Common/BlockTextures/Soil_Gravel_Sand.png"))[0][0]
            self.assertFalse(any(record.startswith(f"{fingerprint:08x} ") for record in records))

    def test_explicit_light_on_new_draw_type_remains_supported(self):
        with tempfile.TemporaryDirectory() as temporary:
            assets = Path(temporary) / "Assets.zip"
            item = {"BlockType": {"DrawType": "Model", "Light": {"Color": "#987"},
                    "CustomModelTexture": [{"Texture": "Blocks/Lantern.png"}],
                    "State": {"Definitions": {"Block": {"DrawType": "Cube", "Light": {"Color": "#00f"},
                                                         "Textures": [{"All": "BlockTextures/Blue.png"}]}}}}}
            with zipfile.ZipFile(assets, "w") as archive:
                archive.writestr("Server/Item/Items/Lantern.json", json.dumps(item))
                for name in ("Blocks/Lantern.png", "BlockTextures/Blue.png"):
                    stream = BytesIO()
                    Image.new("RGB", (32, 32), (20, 40, 200)).save(stream, format="PNG")
                    archive.writestr("Common/" + name, stream.getvalue())
            _, report = build_emitters(assets)
            self.assertEqual(len(report), 2)
            self.assertEqual(report[1]["Color"], "0000ff")
            self.assertEqual(report[1]["Textures"], ["Common/BlockTextures/Blue.png"])


if __name__ == "__main__":
    unittest.main()
