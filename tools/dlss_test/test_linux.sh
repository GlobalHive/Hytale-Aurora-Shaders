#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
sdk=${DLSS_SDK_DIR:-"$root/third_party/DLSS"}
vulkan=${VULKAN_HEADERS_DIR:-"$root/third_party/vulkan"}
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
flags=(-std=gnu11 -O1 -g -I"$sdk/include" -I"$vulkan/include" -Wno-missing-field-initializers)
"${CC:-cc}" "${flags[@]}" "$root/tools/dlss_test/linux_bridge_test.c" -o "$stage/bridge" -ldl
"$stage/bridge"
"${CC:-cc}" "${flags[@]}" $(pkg-config --cflags cairo) "$root/tools/dlss_test/linux_scale_test.c" \
    "$root/hook/aud_dlss.c" -o "$stage/scale" $(pkg-config --libs cairo) -ldl -lpthread -lm
"$stage/scale" "$root/shaderpack"
"${CC:-cc}" "${flags[@]}" "$root/tools/dlss_test/linux_path_test.c" -o "$stage/path" -ldl
"$stage/path"
