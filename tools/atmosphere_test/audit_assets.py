import argparse
from collections import Counter
from copy import deepcopy
import json
from pathlib import Path
import re
import zipfile


def merge(parent, child):
    result = deepcopy(parent)
    for key, value in child.items():
        result[key] = merge(result[key], value) if isinstance(value, dict) and isinstance(result.get(key), dict) else deepcopy(value)
    return result


def resolve(name, records, trail=()):
    if name in trail:
        raise ValueError("Asset inheritance cycle: " + " -> ".join((*trail, name)))
    data = records[name]
    parent = data.get("Parent")
    return merge(resolve(parent, records, (*trail, name)), data) if parent else deepcopy(data)


def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for item in value.values():
            yield from strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from strings(item)


def audit(assets):
    records = {"Weathers": {}, "Environments": {}}
    worldgen = []
    unparsed = []
    with zipfile.ZipFile(assets) as archive:
        for entry in archive.infolist():
            if not entry.filename.endswith(".json"):
                continue
            parts = Path(entry.filename).parts
            if len(parts) < 3 or parts[0] != "Server":
                continue
            if parts[1] in records:
                name = Path(entry.filename).stem
                if name in records[parts[1]]:
                    raise ValueError("Ambiguous asset ID: " + name)
                records[parts[1]][name] = json.loads(archive.read(entry).decode("utf-8-sig"))
            elif parts[1] in {"World", "HytaleGenerator", "Instances"}:
                text = archive.read(entry).decode("utf-8-sig")
                try:
                    data = json.loads(text)
                except json.JSONDecodeError:
                    unparsed.append(entry.filename)
                    data = re.findall(r'"([^"\\]*)"', text)
                worldgen.append((entry.filename, data))
    environments = {name: resolve(name, records["Environments"]) for name in records["Environments"]}
    weathers = {name: resolve(name, records["Weathers"]) for name in records["Weathers"]}
    fields = ("FogDistance", "FogOptions", "FogDensities", "FogHeightFalloffs", "SunlightDampingMultipliers",
              "SunlightColors", "SunColors", "SkyTopColors", "SkyBottomColors", "SkySunsetColors", "FogColors", "ColorFilters")
    bindings = []
    for path, data in worldgen:
        referenced = sorted(set(strings(data)) & environments.keys())
        if referenced or path.startswith("Server/HytaleGenerator/Biomes/"):
            bindings.append({"path": path, "environments": referenced})
    return {
        "counts": {"generator_biomes": sum(path.startswith("Server/HytaleGenerator/Biomes/") for path, _ in worldgen),
                   "worldgen_and_instance_assets": len(worldgen), "environment_reference_candidates": len(bindings),
                   "environments": len(environments), "weathers": len(weathers)},
        "biomes_and_reference_candidates": bindings,
        "unparsed_world_assets_reference_scan_only": unparsed,
        "environments": {name: sorted(set(strings(data.get("WeatherForecasts", {})))) for name, data in sorted(environments.items())},
        "weathers": {name: {field: data[field] for field in fields if field in data} for name, data in sorted(weathers.items())},
        "weather_field_coverage": dict(Counter(field for data in weathers.values() for field in fields if field in data)),
    }


def main():
    parser = argparse.ArgumentParser(description="Inventory biome/environment/weather atmosphere without copying game assets.")
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    assets = arguments.assets
    if assets is None:
        client = Path((root / "install.config").read_text(encoding="utf-8-sig").strip())
        assets = client.parent / "Assets.zip"
    report = audit(assets)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["counts"], sort_keys=True))
    print("Atmosphere field coverage:", json.dumps(report["weather_field_coverage"], sort_keys=True))


if __name__ == "__main__":
    main()
