"""Detect native DLSS support and prepare an installer build automatically."""
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile

from prepare_shaders import ROOT
import prepare_dlss_linux


def nvidia_candidate():
    """Cheap preflight only; NGX makes the final support decision below."""
    initialized = False
    try:
        nvml = ctypes.CDLL("libnvidia-ml.so.1")
        if nvml.nvmlInit_v2() != 0:
            return False, "NVIDIA driver is not running"
        initialized = True
        count = ctypes.c_uint()
        if nvml.nvmlDeviceGetCount_v2(ctypes.byref(count)) != 0:
            return False, "NVIDIA GPU enumeration failed"
        for index in range(count.value):
            device = ctypes.c_void_p()
            name = ctypes.create_string_buffer(256)
            if (nvml.nvmlDeviceGetHandleByIndex_v2(index, ctypes.byref(device)) == 0
                    and nvml.nvmlDeviceGetName(device, name, len(name)) == 0
                    and b"RTX" in name.value.upper()):
                return True, name.value.decode(errors="replace")
        return False, "no NVIDIA RTX GPU detected"
    except (OSError, AttributeError):
        return False, "NVIDIA driver detection unavailable"
    finally:
        if initialized:
            nvml.nvmlShutdown()


def configure():
    mode = os.environ.get("AU_DLSS", "auto")
    if mode == "0":
        print("DLSS disabled by AU_DLSS=0.", flush=True)
        return False
    candidate, reason = nvidia_candidate()
    if not candidate:
        if mode == "1":
            raise ValueError(f"DLSS requested but unsupported: {reason}")
        print(f"DLSS unavailable: {reason}. Installing with temporal anti-aliasing.", flush=True)
        return False
    print(f"Checking DLSS support on {reason}...", flush=True)
    try:
        prepare_dlss_linux.main()
        sdk = Path(os.environ.get("DLSS_SDK_DIR", ROOT / "third_party/DLSS"))
        vulkan = Path(os.environ.get("VULKAN_HEADERS_DIR", ROOT / "third_party/vulkan"))
        with tempfile.TemporaryDirectory(prefix="aurora-dlss-probe-") as temporary:
            probe = Path(temporary) / "support"
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O2",
                            f"-I{sdk / 'include'}", f"-I{vulkan / 'include'}",
                            str(ROOT / "tools/dlss_test/linux_support.c"),
                            str(ROOT / "hook/aud_dlss.c"), "-o", str(probe), "-ldl"],
                           check=True, capture_output=True, text=True, timeout=120)
            result = subprocess.run([str(probe), str(ROOT / "shaderpack/dlss")],
                                    capture_output=True, text=True, timeout=60)
            if result.returncode:
                raise ValueError(result.stdout.strip() or "DLSS capability probe failed")
            print(result.stdout.strip(), flush=True)
        return True
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        if mode == "1":
            raise ValueError(f"DLSS setup failed: {error}") from error
        print(f"DLSS could not be enabled: {error}. Installing with temporal anti-aliasing.", flush=True)
        return False
