from io import BytesIO
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_sss import build_masks, foliage_texture, mask_tiles
import linux_hook


class FoliageMaskTests(unittest.TestCase):
    def test_only_leaf_textures_are_eligible(self):
        self.assertTrue(foliage_texture("Common/Blocks/Foliage/Leaves/Ball_Textures/Oak.png"))
        for name in ("Common/BlockTextures/Soil_Leaves_Side.png", "Common/BlockTextures/Wood_Oak.png",
                     "Common/Blocks/Foliage/Leaves/Hanging_Textures/Roots.png",
                     "Common/Blocks/Foliage/Leaves/Ball_Textures/Spider_Web_Full.png",
                     "Common/Blocks/Foliage/Leaves/../../Other.png"):
            self.assertFalse(foliage_texture(name))

    def test_dark_veins_and_transparent_pixels(self):
        image = Image.new("RGBA", (32, 32), (80, 150, 40, 255))
        for row in range(32):
            image.putpixel((4, row), (5, 10, 2, 255))
            image.putpixel((5, row), (200, 230, 100, 255))
        image.putpixel((0, 0), (255, 255, 255, 0))
        fingerprint, mask = next(mask_tiles(image))
        self.assertEqual(mask[0], 0)
        self.assertLess(mask[4], mask[5])
        image.putpixel((0, 0), (0, 0, 0, 0))
        self.assertEqual(next(mask_tiles(image))[0], fingerprint)

    def test_authored_masks_and_dimensions(self):
        image = Image.new("RGBA", (64, 32), (80, 150, 40, 255))
        image.putpixel((32, 0), (100, 150, 40, 255))
        mask = Image.new("RGBA", image.size, (0, 0, 0, 255))
        mask.putpixel((32, 0), (255, 255, 255, 255))
        tiles = list(mask_tiles(image, mask))
        self.assertEqual(len(tiles), 2)
        self.assertEqual(max(tiles[0][1]), 0)
        self.assertEqual(tiles[1][1][0], 255)
        with self.assertRaises(ValueError):
            list(mask_tiles(image, Image.new("L", (32, 32))))

    def test_binary_and_override_roundtrip(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = Image.new("RGBA", (32, 32), (80, 150, 40, 255))
            stream = BytesIO()
            image.save(stream, format="PNG")
            assets = root / "Assets.zip"
            texture = "Blocks/Foliage/Leaves/Ball_Textures/Oak.png"
            with zipfile.ZipFile(assets, "w") as archive:
                archive.writestr("Common/" + texture, stream.getvalue())
                archive.writestr("Common/BlockTextures/Wood.png", stream.getvalue())
            output = build_masks(assets, root / "sss")
            self.assertEqual(output[:8], b"AUSSS001")
            self.assertEqual(struct.unpack_from("<I", output, 8)[0], 1)
            self.assertEqual(len(output), 12 + 1028)
            self.assertEqual(set(output[16:]), {192})
            custom = root / "sss" / texture
            custom.parent.mkdir(parents=True)
            Image.new("L", (32, 32), 0).save(custom)
            self.assertEqual(set(build_masks(assets, root / "sss")[16:]), {0})

    @patch.object(linux_hook, "require_stopped")
    def test_installed_overrides_are_preserved_and_rebuilt(self, stopped):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            client = root / "Client"
            client.mkdir()
            original = b"\x7fELF\x02\x01" + b"\0" * 12 + b"\x3e\x00" + b"original SDL library"
            (client / "libSDL3.so").write_bytes(original)
            proxy = root / "proxy.so"
            proxy.write_bytes(original + linux_hook.MARKER)
            pack = root / "pack"
            (pack / "include").mkdir(parents=True)
            (pack / "include/AU_Settings.glsl").write_text("#define AU_SSS 1\n")
            image = Image.new("RGBA", (32, 32), (80, 150, 40, 255))
            stream = BytesIO()
            image.save(stream, format="PNG")
            texture = "Blocks/Foliage/Leaves/Ball_Textures/Oak.png"
            with zipfile.ZipFile(root / "Assets.zip", "w") as archive:
                archive.writestr("Common/" + texture, stream.getvalue())
            (pack / "sss-masks.bin").write_bytes(build_masks(root / "Assets.zip", pack / "sss"))
            installed = linux_hook.install(client, proxy, pack)
            custom = installed / "sss" / texture
            custom.parent.mkdir(parents=True)
            Image.new("L", (32, 32), 0).save(custom)
            linux_hook.install(client, proxy, pack)
            self.assertTrue(custom.is_file())
            self.assertEqual(set((installed / "sss-masks.bin").read_bytes()[16:]), {0})


if __name__ == "__main__":
    unittest.main()
