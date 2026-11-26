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
    -I"$root/build/sdl3-include" -I"$vulkan/.deps/native/radv-crux2/include" -I"$root/ps5/host" -I"$root/ps5/sdl3"
    -I"$root/ps5/third_party/stb" -fsanitize=address,undefined)
${CC:-gcc} -O2 -g -fsanitize=address,undefined -c "$root/ps5/host/platform_host.c" -o "$out/platform_host.o"
${CXX:-g++} "${flags[@]}" "$root/ps5/sdl3/psgopher_sdl3.cpp" "$root/ps5/tests/test_sdl3.cpp" \
    "$out/platform_host.o" -o "$out/test_sdl3" -lpthread -lm
"$out/test_sdl3" "$root/vendor/gopher64/data/ui/RobotoMono-Regular.ttf"

# the benchmark window's accounting (bench.c) against known work
${CC:-gcc} -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -I"$root/ps5/host" -I"$root/ps5/sdl3" \
    "$root/ps5/host/bench.c" "$root/ps5/tests/test_bench.c" -o "$out/test_bench" -lpthread
"$out/test_bench"

# the game list's search, the title's settings file and the shares file (ps5vfs stood in for)
${CXX:-g++} -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -DPSGOPHER_SHARES_HOST_TEST \
    -I"$root/ps5/host" -I"$root/ps5/vfs" "$root/ps5/tests/test_library.cpp" "$root/ps5/host/library.cpp" \
    "$root/ps5/host/title_settings.cpp" "$root/ps5/host/shares.cpp" -o "$out/test_library" -lpthread
"$out/test_library" "$out/library"

# the game list itself, drawn offscreen through the PC's Vulkan (lavapipe when it is there) with a
# scripted pad; pictures in build/host-tests/menu-pictures (PNG with python3-pil)
lavapipe=${PSGOPHER_LAVAPIPE_ICD:-/opt/lavapipe/lvp_icd.json}
imgui="$root/.deps/src/imgui"
rdp="$root/vendor/gopher64/parallel-rdp/parallel-rdp-standalone"
if [[ -f $imgui/imgui.cpp && -f $rdp/volk/volk.c && -f $root/build/ps5/gen/font_mono.h ]]; then
    menu_flags=(-std=c++20 -O1 -g -fsanitize=address,undefined -DPSGOPHER_MENU_HOST -DPSGOPHER_SHARES_HOST_TEST
        -DIMGUI_IMPL_VULKAN_USE_VOLK -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS
        -I"$root/ps5/host" -I"$root/ps5/vfs"
        -I"$imgui" -I"$imgui/backends" -I"$rdp/volk" -I"$rdp/vulkan-headers/include" -I"$root/build/ps5/gen"
        -I"$root/ps5/third_party/stb" -DPSGOPHER_SOURCE="\"$root\"")
    ${CC:-gcc} -O1 -g -fsanitize=address,undefined -I"$rdp/vulkan-headers/include" -c "$rdp/volk/volk.c" -o "$out/volk.o"
    for source in imgui imgui_draw imgui_tables imgui_widgets backends/imgui_impl_vulkan; do
        [[ $out/imgui_${source##*/}.o -nt $imgui/$source.cpp ]] ||
            ${CXX:-g++} "${menu_flags[@]}" -w -c "$imgui/$source.cpp" -o "$out/imgui_${source##*/}.o"
    done
    ${CXX:-g++} "${menu_flags[@]}" -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function \
        -Wno-unused-variable \
        "$root/ps5/tests/test_menu.cpp" "$root/ps5/host/menu.cpp" "$root/ps5/host/library.cpp" \
        "$root/ps5/host/frontend.cpp" "$root/ps5/host/board.cpp" "$root/ps5/host/image.cpp" "$root/ps5/host/web.cpp" \
        "$root/ps5/host/title_settings.cpp" "$root/ps5/host/shares.cpp" "$root/ps5/host/sandbox.cpp" \
        "$out/volk.o" "$out"/imgui_*.o -o "$out/test_menu" -lpthread -ldl
    [[ -f $lavapipe ]] && export VK_ICD_FILENAMES=$lavapipe
    "$out/test_menu" "$out/menu-work" "$out/menu-pictures"
    # the in-game quick menu, over a stand-in frame by a small software rasterizer
    ${CXX:-g++} -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
        -fsanitize=address,undefined -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -I"$root/ps5/host" -I"$root/ps5/sdl3" -I"$root/build/sdl3-include" -I"$imgui" \
        -I"$root/build/ps5/gen" "$root/ps5/tests/test_quickmenu.cpp" "$root/ps5/host/quickmenu.cpp" \
        "$root/ps5/host/title_settings.cpp" \
        "$out"/imgui_imgui.o "$out"/imgui_imgui_draw.o "$out"/imgui_imgui_tables.o "$out"/imgui_imgui_widgets.o \
        -o "$out/test_quickmenu"
    "$out/test_quickmenu" "$out/quickmenu-work" "$out/menu-pictures/quickmenu"
    if python3 -c 'import PIL' 2>/dev/null; then
        python3 -c 'import sys, glob, PIL.Image as I
for p in glob.glob(sys.argv[1] + "/**/*.ppm", recursive=True): I.open(p).save(p[:-4] + ".png")' "$out/menu-pictures"
    fi
else
    echo "test_menu: skipped (needs .deps/src/imgui, vendor/gopher64 and a build's build/ps5/gen/menu_font.h)"
fi

# the FreeBSD 12 converters for Rust (ps5/host/rust_abi.c) against Rust's libc's own offsets, which
# a no_std library built for x86_64-unknown-freebsd reports (rustup target add x86_64-unknown-freebsd)
if rustup +nightly target list --installed 2>/dev/null | grep -qx x86_64-unknown-freebsd; then
    (cd "$root/ps5/tests/rust_abi_offsets" && CARGO_TARGET_DIR="$root/build/rust-abi-offsets" \
        cargo +nightly build -q --release --target x86_64-unknown-freebsd)
    ${CC:-gcc} -std=c11 -O1 -g -Wall -Wextra -fsanitize=address,undefined "$root/ps5/tests/test_rust_abi.c" \
        "$root/build/rust-abi-offsets/x86_64-unknown-freebsd/release/librust_abi_offsets.a" -o "$out/test_rust_abi"
    "$out/test_rust_abi"
else
    echo "test_rust_abi: skipped (rustup target add x86_64-unknown-freebsd --toolchain nightly)"
fi
