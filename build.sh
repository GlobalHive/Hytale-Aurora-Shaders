#!/usr/bin/env bash
# Native Ubuntu/Linux build. Dependencies: build-essential pkg-config libcairo2-dev
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo 'The native build currently targets x86_64 Linux (glibc).' >&2
    exit 1
fi
if ! command -v "${CC:-cc}" >/dev/null || ! command -v pkg-config >/dev/null || ! pkg-config --exists cairo; then
    echo 'Install build dependencies: sudo apt install build-essential pkg-config libcairo2-dev fonts-dejavu-core python3 python3-pil' >&2
    exit 1
fi
mkdir -p "$root/build"
read -r -a cairo_cflags <<< "$(pkg-config --cflags cairo)"
read -r -a cairo_libs <<< "$(pkg-config --libs cairo)"
"${CC:-cc}" -std=gnu11 -O2 -fPIC -shared -Wl,-z,defs -Wl,-Bsymbolic-functions \
    -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -DAU_HAS_DLSS=0 \
    "${cairo_cflags[@]}" "$root/hook/hook.c" -o "$root/build/libaurora.so" \
    "${cairo_libs[@]}" -ldl -lpthread -lm
echo "Built $root/build/libaurora.so"
