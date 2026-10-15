from pathlib import Path
import re
import sys
import zipfile


def package(stage, destination):
    files = sorted(path for path in stage.rglob("*") if path.is_file())
    forbidden = {".git", "third_party", "original", "presets", "__pycache__", "backups"}
    for path in files:
        relative = path.relative_to(stage)
        if path.is_symlink() or forbidden.intersection(relative.parts):
            raise ValueError(f"Private/generated path in release: {relative}")
        if path.name in {"install.config", "hook.log", "shot.bmp", "materials.txt", "emitters.txt", "emitters.json", "index.txt"}:
            raise ValueError(f"Private/generated file in release: {relative}")
        if path.suffix == ".sh" and b"\r" in path.read_bytes():
            raise ValueError(f"Linux script contains Windows line endings: {relative}")
    defaults = (stage / "shaderpack/include/AU_Settings.glsl").read_text(encoding="utf-8-sig")
    for name, expected in {"AU_PRESET": 2, "AU_DLSS": 0, "AU_GI_DENOISER": 1, "AU_PATH_TRACING": 0}.items():
        match = re.search(rf"^#define\s+{name}\s+(\d+)\b", defaults, re.MULTILINE)
        if not match or int(match[1]) != expected:
            raise ValueError(f"Unexpected shipped default: {name}")
    temporary = destination.with_suffix(".pending.zip")
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in files:
                relative = Path(stage.name) / path.relative_to(stage)
                entry = zipfile.ZipInfo.from_file(path, arcname=relative.as_posix())
                entry.create_system = 3
                entry.external_attr = (0o100755 if path.suffix == ".sh" else 0o100644) << 16
                archive.writestr(entry, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
        with zipfile.ZipFile(temporary) as archive:
            damaged = archive.testzip()
            if damaged:
                raise ValueError(f"Corrupt archive entry: {damaged}")
            for entry in archive.infolist():
                if "\\" in entry.filename:
                    raise ValueError(f"Non-portable archive path: {entry.filename}")
                if entry.filename.endswith(".sh") and (entry.external_attr >> 16) & 0o777 != 0o755:
                    raise ValueError(f"Missing executable permissions: {entry.filename}")
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"PASS: {len(files)} release files; portable paths, executable Linux scripts and shipped defaults checked")


if __name__ == "__main__":
    package(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
