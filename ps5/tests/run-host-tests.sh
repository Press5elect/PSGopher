#!/usr/bin/env bash
# PSGopher - the host tests (on the PC, no console): ps5/sdl3 against SDL's headers, with the PS5
# layer's host build. Needs build/sdl3-include (ps5/tools/build-core.sh makes it) and the RADV
# archive's Vulkan headers.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
vulkan=${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}
out="$root/build/host-tests"
mkdir -p "$out"
[[ -d $root/build/sdl3-include/SDL3 ]] || { echo "no build/sdl3-include: run ps5/tools/build-core.sh once" >&2; exit 2; }
flags=(-std=c++20 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers
    -I"$root/build/sdl3-include" -I"$vulkan/.deps/native/radv-release/include" -I"$root/ps5/host" -I"$root/ps5/sdl3"
    -I"$root/ps5/third_party/stb" -fsanitize=address,undefined)
${CC:-gcc} -O2 -g -fsanitize=address,undefined -c "$root/ps5/host/platform_host.c" -o "$out/platform_host.o"
${CXX:-g++} "${flags[@]}" "$root/ps5/sdl3/psgopher_sdl3.cpp" "$root/ps5/tests/test_sdl3.cpp" \
    "$out/platform_host.o" -o "$out/test_sdl3" -lpthread -lm
"$out/test_sdl3" "$root/vendor/gopher64/data/ui/RobotoMono-Regular.ttf"
