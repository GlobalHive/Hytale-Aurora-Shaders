#!/usr/bin/env python3
"""Install and launch the native Linux shader hook without changing game files."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

from prepare_shaders import ROOT, prepare

APP_ID = "com.hypixel.HytaleLauncher"
CONFIG = ROOT / "build" / "linux-install.json"
CLIENT_SUFFIX = Path("install/release/package/game/latest/Client")


def candidates(home=None, data_home=None):
    home = home or Path.home()
    data_home = data_home or Path(os.environ.get("XDG_DATA_HOME", home / ".local/share"))
    return [
        (data_home / "Hytale" / CLIENT_SUFFIX, "native"),
        (home / ".var/app" / APP_ID / "data/Hytale" / CLIENT_SUFFIX, "flatpak"),
    ]


def is_client(path):
    return (path / "HytaleClient").is_file()


def detect(client_dir=None, backend=None):
    if client_dir:
        client = client_dir.expanduser().resolve()
        if not is_client(client):
            raise ValueError(f"No native HytaleClient found in {client}")
        mode = "flatpak" if APP_ID in client.parts else "native"
        return client, backend or mode
    if CONFIG.is_file():
        saved = json.loads(CONFIG.read_text())
        client = Path(saved["client_dir"])
        if (not backend or saved["backend"] == backend) and is_client(client):
            return client, backend or saved["backend"]
    for client, mode in candidates():
        if (not backend or mode == backend) and is_client(client):
            return client, mode
    raise ValueError("Hytale was not found. Run ./install.sh --client-dir /path/to/Client")


def validate_client(executable):
    with executable.open("rb") as stream:
        header = stream.read(20)
    if len(header) < 20 or header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\x3e\x00":
        raise ValueError("Expected the native x86_64 Linux HytaleClient (ELF), not a Windows/Wine client")


def fingerprint(executable):
    stat = executable.stat()
    return {"size": stat.st_size, "mtime_ns": stat.st_mtime_ns}


def launch_command(config, extra, env):
    """Return argv and environment, preserving launcher arguments without shell parsing."""
    library_dir = str(ROOT / "build")
    pack = str(ROOT / "shaderpack")
    if ":" in library_dir:
        raise ValueError("Move Aurora to a folder without ':' in its path (Linux library search separator)")
    if config["backend"] == "flatpak":
        # Set loader variables inside the sandbox, not on the host flatpak executable.
        # A per-launch mount works for checkouts outside the app's private data directory.
        script = ('export LD_LIBRARY_PATH="$1${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"; '
                  'export LD_PRELOAD="libaurora.so${LD_PRELOAD:+:$LD_PRELOAD}"; '
                  'export AURORA_SHADERPACK="$2"; shift 2; exec /app/bin/hytale-launcher-wrapper "$@"')
        return ["flatpak", "run", f"--filesystem={ROOT}", "--command=sh", APP_ID,
                "-c", script, "aurora", library_dir, pack, *extra], env.copy()
    launcher = config.get("launcher")
    if not launcher or not Path(launcher).is_file():
        raise ValueError("Native launcher not found. Run ./install.sh --launcher /path/to/hytale-launcher")
    result_env = env.copy()
    result_env["LD_LIBRARY_PATH"] = library_dir + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    result_env["LD_PRELOAD"] = "libaurora.so" + (":" + env["LD_PRELOAD"] if env.get("LD_PRELOAD") else "")
    result_env["AURORA_SHADERPACK"] = pack
    return [launcher, *extra], result_env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client-dir", type=Path, help="Folder containing the native HytaleClient")
    parser.add_argument("--backend", choices=("native", "flatpak"), help="Override launcher detection")
    parser.add_argument("--launcher", type=Path, help="Native hytale-launcher executable")
    parser.add_argument("--build", action="store_true", help="Rebuild even if a library is already present")
    parser.add_argument("--uninstall", action="store_true", help="Remove Aurora launch configuration; game files are untouched")
    parser.add_argument("--launch", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--print-command", action="store_true", help="Show launch command without starting the game")
    args, extra = parser.parse_known_args()
    if extra and extra[0] == "--":
        extra = extra[1:]
    if extra and not args.launch:
        parser.error("Unrecognized arguments: " + " ".join(extra))
    try:
        if platform.system() != "Linux" or platform.machine() != "x86_64":
            raise ValueError("Aurora currently targets Ubuntu-based x86_64 Linux systems")
        if args.uninstall:
            CONFIG.unlink(missing_ok=True)
            print("Aurora launch configuration removed. Start Hytale normally for vanilla shaders.\n"
                  "Your shader settings and generated files have been kept in this folder.")
            return
        if args.launch:
            if not CONFIG.is_file():
                raise ValueError("Run ./install.sh before launching Aurora")
            config = json.loads(CONFIG.read_text())
            executable = Path(config["client_dir"]) / "HytaleClient"
            if not executable.is_file() or fingerprint(executable) != config["client_fingerprint"]:
                raise ValueError("Hytale has changed since shader preparation. Close the launcher and run ./install.sh again")
            if not (ROOT / "build/libaurora.so").is_file() or not (ROOT / "shaderpack/index.txt").is_file():
                raise ValueError("Aurora build or shaders are missing. Run ./install.sh --build")
            argv, env = launch_command(config, extra, os.environ)
            if args.print_command:
                import shlex
                print(shlex.join(argv))
                return
            print("Starting Hytale with Aurora. Home opens options; Pause toggles shaders.", flush=True)
            os.execvpe(argv[0], argv, env)
        client, backend = detect(args.client_dir, args.backend)
        executable = client / "HytaleClient"
        validate_client(executable)
        launcher = args.launcher.expanduser().resolve() if args.launcher else None
        if backend == "flatpak":
            if not shutil.which("flatpak"):
                raise ValueError("Flatpak is not installed; select --backend native if using a native launcher")
        else:
            if not launcher:
                launcher = next((p / "hytale-launcher" for p in client.parents if (p / "hytale-launcher").is_file()), None)
            if not launcher or not os.access(launcher, os.X_OK):
                raise ValueError("Supply your native launcher: ./install.sh --launcher /path/to/hytale-launcher")
        if args.build or not (ROOT / "build/libaurora.so").is_file():
            subprocess.run(["bash", str(ROOT / "build.sh")], check=True)
        prepare(executable, ROOT / "shaderpack")
        config = {"client_dir": str(client), "backend": backend,
                  "client_fingerprint": fingerprint(executable), "launcher": str(launcher) if launcher else None}
        CONFIG.parent.mkdir(exist_ok=True)
        pending = CONFIG.with_suffix(".tmp")
        pending.write_text(json.dumps(config, indent=2) + "\n")
        pending.replace(CONFIG)
        print(f"Aurora prepared for {backend}: {client}\n"
              "Close any running Hytale launcher, then run ./launch.sh.\n"
              "Keep this Aurora folder in place. Run ./install.sh again after game updates.")
    except (OSError, ValueError, KeyError, ImportError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Aurora: {error}\n")


if __name__ == "__main__":
    main()
