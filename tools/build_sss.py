import argparse
from io import BytesIO
from pathlib import Path
import struct
import tempfile
import zipfile


def foliage_texture(name):
    return (name.startswith("Common/Blocks/Foliage/Leaves/") and name.endswith(".png")
            and not any(word in Path(name).stem.lower() for word in ("root", "spider", "web"))
            and ".." not in Path(name).parts)


def mask_tiles(image, override=None):
    image = image.convert("RGBA")
    if image.width % 32 or image.height % 32:
        raise ValueError("Foliage textures must contain complete 32x32 atlas tiles")
    if override is not None:
        if override.size != image.size:
            raise ValueError("SSS override dimensions must match the original foliage texture")
        override = override.convert("RGBA")
    for top in range(0, image.height, 32):
        for left in range(0, image.width, 32):
            pixels = list(image.crop((left, top, left + 32, top + 32)).getdata())
            luminance = [(54 * red + 183 * green + 19 * blue + 128) >> 8 for red, green, blue, alpha in pixels]
            visible = sorted(value for value, pixel in zip(luminance, pixels) if pixel[3])
            if not visible:
                continue
            lower, upper = visible[(len(visible) - 1) // 10], visible[(len(visible) - 1) * 9 // 10]
            if upper - lower < 16 and visible[-1] - visible[0] >= 16:
                lower, upper = visible[0], visible[-1]
            authored = list(override.crop((left, top, left + 32, top + 32)).getdata()) if override is not None else None
            fingerprint = 2166136261
            mask = bytearray(1024)
            for index, pixel in enumerate(pixels):
                for channel in pixel if pixel[3] else (0, 0, 0, 0):
                    fingerprint = ((fingerprint ^ channel) * 16777619) & 0xffffffff
                if not pixel[3]:
                    continue
                if authored is not None:
                    mask[index] = authored[index][0] * authored[index][3] // 255
                elif upper - lower < 16:
                    mask[index] = 192
                else:
                    weight = min(1.0, max(0.0, (luminance[index] - lower) / (upper - lower)))
                    mask[index] = round(weight * weight * (3.0 - 2.0 * weight) * 255.0)
            yield fingerprint, bytes(mask)


def build_masks(assets, overrides):
    from PIL import Image
    records, authored = {}, set()
    with zipfile.ZipFile(assets) as archive:
        for name in sorted(filter(foliage_texture, archive.namelist())):
            override_path = overrides / name.removeprefix("Common/")
            custom = None
            if override_path.is_file():
                with Image.open(override_path) as source:
                    custom = source.convert("RGBA")
            with Image.open(BytesIO(archive.read(name))) as image:
                for fingerprint, mask in mask_tiles(image, custom):
                    if custom is not None:
                        if fingerprint in authored and records[fingerprint] != mask:
                            raise ValueError(f"Conflicting SSS overrides for identical atlas tile {fingerprint:08x}")
                        records[fingerprint] = mask
                        authored.add(fingerprint)
                    elif fingerprint not in authored:
                        previous = records.get(fingerprint)
                        records[fingerprint] = bytes(map(min, previous, mask)) if previous is not None else mask
    if len(records) > 2047:
        raise ValueError("Too many distinct SSS mask tiles for the supported texture array")
    return b"AUSSS001" + struct.pack("<I", len(records)) + b"".join(
        struct.pack("<I", fingerprint) + records[fingerprint] for fingerprint in sorted(records))


def main():
    parser = argparse.ArgumentParser(description="Generate foliage SSS masks from the installed game's textures")
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--overrides", type=Path, required=True)
    arguments = parser.parse_args()
    data = build_masks(arguments.assets, arguments.overrides)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    pending = None
    try:
        with tempfile.NamedTemporaryFile(dir=arguments.output.parent, delete=False) as temporary:
            pending = Path(temporary.name)
            temporary.write(data)
        pending.replace(arguments.output)
    finally:
        if pending is not None:
            pending.unlink(missing_ok=True)
    print(f"Foliage SSS masks: {struct.unpack_from('<I', data, 8)[0]} tiles")


if __name__ == "__main__":
    main()
