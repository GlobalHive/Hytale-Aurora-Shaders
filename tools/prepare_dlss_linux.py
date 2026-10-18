"""Fetch pinned official Linux DLSS headers/runtimes and Vulkan headers.

Downloads are verified by SHA-256. NVIDIA's license is saved with the SDK and
runtime; review it before redistributing. No driver or game files are modified.
"""
from pathlib import Path
import hashlib
import shutil
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
# Immutable upstream revisions and file hashes. Runtime: NGX 310.9.16, x86_64 release.
FILES = [
    ('NVIDIA/DLSS', '9981c6350dc58c0ce21f024fd65e31e851c31689', 'LICENSE.txt',
     'third_party/DLSS/LICENSE.txt', 'd4216e39ebef5f9b50a6712ebb37beeb5379862a67733a9999c651f21592aaf0'),
    ('NVIDIA/DLSS', '9981c6350dc58c0ce21f024fd65e31e851c31689', 'include/nvsdk_ngx_defs.h',
     'third_party/DLSS/include/nvsdk_ngx_defs.h', 'ea23f33497cd274860d1c25a97644fce807dcb0037c594547203343103fad03e'),
    ('NVIDIA/DLSS', '9981c6350dc58c0ce21f024fd65e31e851c31689', 'include/nvsdk_ngx_defs_vk.h',
     'third_party/DLSS/include/nvsdk_ngx_defs_vk.h', '0a24d0861ace7d6b9362a67b7f08bea1b33ea123c5ce730b19133de8a891d031'),
    ('NVIDIA/DLSS', '9981c6350dc58c0ce21f024fd65e31e851c31689', 'include/nvsdk_ngx_defs_dlssd.h',
     'third_party/DLSS/include/nvsdk_ngx_defs_dlssd.h', '2e97fe1595b1334f71b51724f2cb67496e8c296a7a2eec5606da53055882e716'),
    ('NVIDIA/DLSS', '9981c6350dc58c0ce21f024fd65e31e851c31689', 'include/nvsdk_ngx_params.h',
     'third_party/DLSS/include/nvsdk_ngx_params.h', '943bc8cc5cdae03b6303016fbad3183636f2335ae27a2d18776798c3b4efabbc'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'LICENSE.md',
     'third_party/vulkan/LICENSE.md', '95ad366d23fadf701d355bc45fb8b82ae2d700239471d35d41286ac3b08ff903'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vulkan/vulkan.h',
     'third_party/vulkan/include/vulkan/vulkan.h', 'ace769f4cd4c69a846c1778be95bfcd326038c8adc7630584ba33bcadd2f70a5'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vulkan/vk_platform.h',
     'third_party/vulkan/include/vulkan/vk_platform.h', 'a2cd9085c66776845d2524c3b0e73ccf4827ba046aa6f676c4b8ddbed47d6552'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vulkan/vulkan_core.h',
     'third_party/vulkan/include/vulkan/vulkan_core.h', 'f331fc8209888d4a6b24d23a6a7baef94d534491ecd3aa0493e4583e505578f0'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codec_av1std.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codec_av1std.h', '83bc45ed585e664ccd57f112ad0eaf2a83c12276bf5a30081ae9e742b8ffeb6c'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codec_av1std_decode.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codec_av1std_decode.h', 'e37184b12c11fe5c4990133040158c0e5ecdbafc52285880276cddee6aecb8d5'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codec_av1std_encode.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codec_av1std_encode.h', 'c0388defe123f7e5fc02908d6ba868f58479a323da31d8e825b5fe2ac0523acb'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codec_h264std.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codec_h264std.h', '072e863dd05e8151b9e0f5b18a4cb52acbe618079b4ace71e3b60cce1ec9ccbc'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codec_h264std_decode.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codec_h264std_decode.h', 'e401c6aea14f2e5d33cc8215d6a6725a9a6659e266a4f0b371c91d2a4cbde505'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codec_h264std_encode.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codec_h264std_encode.h', '30ef5fc7be25aec0fbebde8c4908d631690f2063d33c10ccea9edaa2ed0d728a'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codec_h265std.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codec_h265std.h', 'ba1668e412d3483d945fc3707e3b25b30c0c43ffd347d9b8b536a1cc622000a8'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codec_h265std_decode.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codec_h265std_decode.h', '2da3a4c697f9b0b75a11181880e082ab13553ed5d915782c841e01be4793dfb0'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codec_h265std_encode.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codec_h265std_encode.h', 'bbc4a232434a2268c3689a676a488a13bf1912bfa4ff9aeb3a54077905316bd1'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codec_vp9std.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codec_vp9std.h', '1c33ed11eda1225ba52250221378930129b9c78c4aecc91bb368fa3d00f9cf36'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codec_vp9std_decode.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codec_vp9std_decode.h', '37d5b7847a9dda12480ea6b27dfedb4c509eb61b91090b5b119a12bbb5c5b190'),
    ('KhronosGroup/Vulkan-Headers', 'acbb9391c0a597272800cd3f4363661c00a27c77', 'include/vk_video/vulkan_video_codecs_common.h',
     'third_party/vulkan/include/vk_video/vulkan_video_codecs_common.h', '3fc937e75411bf82fd8dfaa721b07b2bb53307c891a347cc010d4747ce28f971'),
    ('NVIDIA/DLSS', '9981c6350dc58c0ce21f024fd65e31e851c31689', 'lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.310.9.16',
     'shaderpack/dlss/libnvidia-ngx-dlss.so.310.9.16', '30982c937afed85c96b8d313323de9a650a2eec6de1bb0f714b21750f4ce98f6'),
    ('NVIDIA/DLSS', '9981c6350dc58c0ce21f024fd65e31e851c31689', 'lib/Linux_x86_64/rel/libnvidia-ngx-dlssd.so.310.9.16',
     'shaderpack/dlss/libnvidia-ngx-dlssd.so.310.9.16', 'a5d59de18841f15779039dda567a15dd723ba737915f489271fdb5f109fbfb96'),
]


def fetch(repo, revision, source, destination, expected):
    destination = ROOT / destination
    if destination.is_file() and hashlib.sha256(destination.read_bytes()).hexdigest() == expected:
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    url = f"https://raw.githubusercontent.com/{repo}/{revision}/{source}"
    print(f"Downloading {source}", flush=True)
    # A temporary file in the destination directory permits an atomic rename.
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
        pending = Path(output.name)
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                shutil.copyfileobj(response, output)
            output.close()
            if hashlib.sha256(pending.read_bytes()).hexdigest() != expected:
                raise ValueError(f"Checksum mismatch: {source}")
            pending.replace(destination)
        finally:
            pending.unlink(missing_ok=True)


def main():
    for item in FILES:
        fetch(*item)
    shutil.copy2(ROOT / "third_party/DLSS/LICENSE.txt", ROOT / "shaderpack/dlss/NVIDIA-DLSS-LICENSE.txt")
    print("Linux DLSS dependencies ready.")


if __name__ == "__main__":
    main()
