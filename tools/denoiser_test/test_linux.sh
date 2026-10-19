#!/usr/bin/env bash
# Native rendering regression tests. Requires the main build dependencies plus libegl1-mesa-dev.
# Exit 77 means no usable EGL/OpenGL context. Uses the selected EGL driver (including Mesa software rendering).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
read -r -a cairo_cflags <<< "$(pkg-config --cflags cairo)"
read -r -a cairo_libs <<< "$(pkg-config --libs cairo)"
"${CC:-cc}" -std=gnu11 -O1 -g -DAU_HAS_DLSS=0 "${cairo_cflags[@]}" \
    "$root/tools/denoiser_test/test.c" -o "$stage/test" \
    "${cairo_libs[@]}" -lEGL -ldl -lpthread -lm
AU_LOCAL_LIGHT_TEST_ONLY=1 "$stage/test" "$root/shaderpack"
