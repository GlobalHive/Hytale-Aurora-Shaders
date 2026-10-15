import json
from io import BytesIO
from pathlib import Path
import re
import zipfile


def merge_objects(base, overlay):
    result = dict(base or {})
    for name, value in (overlay or {}).items():
        result[name] = merge_objects(result[name], value) if isinstance(value, dict) and isinstance(result.get(name), dict) else value
    return result


def hex_color(value):
    if not isinstance(value, str):
        return None
    value = value.lstrip("#")
    if len(value) == 3:
        value = "".join(channel * 2 for channel in value)
    return value.lower() if re.fullmatch(r"[0-9a-fA-F]{6}", value) else None


def texture_tiles(data):
    from PIL import Image
    result = []
    with Image.open(BytesIO(data)) as image:
        if image.width % 32 or image.height % 32:
            return result
        image = image.convert("RGBA")
        for top in range(0, image.height, 32):
            for left in range(0, image.width, 32):
                fingerprint = 2166136261
                totals = [0, 0, 0]
                weight = 0
                for pixel in image.crop((left, top, left + 32, top + 32)).getdata():
                    channels = pixel if pixel[3] else (0, 0, 0, 0)
                    for channel in channels:
                        fingerprint = ((fingerprint ^ channel) * 16777619) & 0xffffffff
                    for channel in range(3):
                        totals[channel] += channels[channel] * channels[3]
                    weight += channels[3]
                if weight:
                    average = "".join(f"{value // weight:02x}" for value in totals)
                    result.append((fingerprint, average))
    return result


def build_emitters(assets):
    records, report, pixels, resolved = set(), [], {}, {}
    with zipfile.ZipFile(assets) as archive:
        entries = set(archive.namelist())
        definitions = {Path(name).stem: json.loads(archive.read(name).decode("utf-8-sig"))
                       for name in sorted(entries) if name.startswith("Server/Item/Items/") and name.endswith(".json")}

        def resolve(name, chain=()):
            if name in resolved:
                return resolved[name]
            if name in chain or len(chain) > 64 or name not in definitions:
                return {}
            item = definitions[name]
            resolved[name] = merge_objects(resolve(item.get("Parent"), chain + (name,)), item)
            return resolved[name]

        for name in sorted(definitions):
            base = resolve(name).get("BlockType")
            if not isinstance(base, dict):
                continue
            blocks = [base]
            for state in base.get("State", {}).get("Definitions", {}).values():
                block = merge_objects(base, state)
                if state.get("DrawType", base.get("DrawType")) != base.get("DrawType") and "Light" not in state:
                    block.pop("Light", None)
                blocks.append(block)
            for block in blocks:
                color = hex_color((block.get("Light") or {}).get("Color"))
                if not color or color == "000000":
                    continue
                textures = set()
                texture_sets = [] if block.get("DrawType") == "Model" else block.get("Textures") or []
                if isinstance(texture_sets, dict):
                    texture_sets = [texture_sets]
                for texture_set in texture_sets:
                    textures.update("Common/" + value for value in texture_set.values()
                                    if isinstance(value, str) and value.endswith(".png"))
                model_textures = (block.get("CustomModelTexture") or []) if block.get("DrawType") == "Model" else []
                if isinstance(model_textures, dict):
                    model_textures = [model_textures]
                for texture in model_textures:
                    if texture.get("Texture"):
                        textures.add("Common/" + texture["Texture"])
                tints = block.get("Tint") or []
                if isinstance(tints, str):
                    tints = [tints]
                tints = [hex_color(tint) for tint in tints if hex_color(tint)] or ["ffffffff"]
                matched = 0
                for texture in sorted(textures & entries):
                    if texture not in pixels:
                        pixels[texture] = texture_tiles(archive.read(texture))
                    for fingerprint, average in pixels[texture]:
                        records.update(f"{fingerprint:08x} {tint} {color} {average}" for tint in tints)
                        matched += 1
                report.append({"Item": name, "Color": color, "Tint": tints, "Textures": sorted(textures), "Tiles": matched})
    return records, report
