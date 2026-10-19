from io import BytesIO
import json
from pathlib import Path
import sys
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_emitters import build_emitters, texture_tiles
from prepare_shaders import make_blocky_model_fs


class EmitterClassificationTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cpp"), "C preprocessor unavailable")
    def test_entity_fullbright_does_not_enter_world_light_cache(self):
        # Exercise generated deferred and forward variants, as installation does.
        source = ('#version 410 core\n#include "ModelVFX_inc.glsl"\n'
                  '#if DEFERRED\n'
                  '    writeToGBuffer(color.rgb, normalWS, defLight, staticLightColor.a, hasSSAO, hasBloom, false, outColor0, outColor1, outColor2);\n'
                  '#else\n    color.rgb *= computeSimpleLightColor(normalWS, shadingMode);\n#endif\n')
        generated = make_blocky_model_fs(source)
        code = re.sub(r"^#(?:version|extension|include).*\n", "", generated, flags=re.M)
        for deferred in (0, 1):
            for entity in (0, 1):
                result = subprocess.run(["cpp", "-P", "-undef", "-DAU_VOXEL_WORLD=1",
                                         f"-DDEFERRED={deferred}", f"-DUSE_ENTITY_DATA_BUFFER={entity}"],
                                        input=code, text=True, capture_output=True, check=True).stdout
                self.assertEqual("AU_RecordEmitterAt" in result, not entity)
                self.assertIn("writeToGBuffer" if deferred else "computeSimpleLightColor", result)

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
