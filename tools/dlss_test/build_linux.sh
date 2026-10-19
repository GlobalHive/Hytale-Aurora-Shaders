#!/usr/bin/env bash
# Requires libegl1-mesa-dev and libgl-dev in addition to the main build dependencies.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/build"
"${CC:-cc}" -std=gnu11 -O1 -g \
    -I"${DLSS_SDK_DIR:-$root/third_party/DLSS}/include" \
    -I"${VULKAN_HEADERS_DIR:-$root/third_party/vulkan}/include" \
    "$root/tools/dlss_test/dlss_test.c" "$root/hook/aud_dlss.c" \
    -o "$root/build/dlss_test" -lEGL -lGL -ldl
printf 'Built %s/build/dlss_test. Run from shaderpack/dlss; exit 77 means hardware/display unavailable.\n' "$root"
