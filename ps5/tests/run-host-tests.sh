#!/usr/bin/env bash
# PSGopher - the host tests (on the PC, no console): ps5/sdl3 against SDL's headers, with the PS5
# layer's host build. Needs build/sdl3-include (ps5/tools/build-core.sh makes it) and the RADV
# archive's Vulkan headers.
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
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

# the freeze watchdog (bench.c): a fake emulation thread blocked in a named place
${CC:-gcc} -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -I"$root/ps5/host" -I"$root/ps5/sdl3" \
    "$root/ps5/host/bench.c" "$root/ps5/tests/test_stall.c" -o "$out/test_stall" -lpthread
"$out/test_stall"
# the sandbox helper's connection asked again while the payload loader comes up (from Crux2)
${CXX:-g++} -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$root/ps5/elevation" \
    "$root/ps5/elevation/elevation.cpp" "$root/ps5/tests/elevation/elevation_retry_test.cpp" -o "$out/elevation_retry"
"$out/elevation_retry"

# the game list's search, the title's settings file and the shares file (ps5vfs stood in for)
${CXX:-g++} -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -DPSGOPHER_SHARES_HOST_TEST \
    -I"$root/ps5/host" -I"$root/ps5/vfs" "$root/ps5/tests/test_library.cpp" "$root/ps5/host/library.cpp" "$root/ps5/host/romhack.cpp" "$root/ps5/host/nointro.cpp" \
    "$root/ps5/host/title_settings.cpp" "$root/ps5/host/game_settings.cpp" "$root/ps5/host/expansion_pak.cpp" "$root/ps5/host/shares.cpp" "$root/ps5/host/write_broker.cpp" \
    "$root/ps5/broker/server.cpp" -o "$out/test_library" -lpthread
"$out/test_library" "$out/library"

# covers and descriptions by the game ID: the No-Intro table made from Gopher64's database, and the lookup
python3 "$root/ps5/tools/make-nointro-ids.py" "$out/nointro-ids.tsv"
${CXX:-g++} -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$root/ps5/host" \
    "$root/ps5/tests/test_nointro.cpp" "$root/ps5/host/nointro.cpp" -o "$out/test_nointro"
"$out/test_nointro" "$out/nointro-ids.tsv"

# romhack patches (romhack.cpp): IPS, BPS and UPS, byte orders, malformed patches
${CXX:-g++} -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I"$root/ps5/host" \
    "$root/ps5/tests/test_romhack.cpp" "$root/ps5/host/romhack.cpp" -o "$out/test_romhack"
"$out/test_romhack"

# a probe's picture (probe_picture.h): the PNG, its atomic write and replacement
${CXX:-g++} -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I"$root/ps5/host" \
    -I"$root/ps5/third_party/stb" "$root/ps5/tests/test_probe_picture.cpp" -o "$out/test_probe_picture"
"$out/test_probe_picture" "$out/probe-picture"

# the updater (ps5/host/update.cpp): its ZIP reading is the very code the title links
# (vendor/gopher64/src/psgopher_zip.rs, built as ps5/tests/update_zip); GitHub is played by the test
if command -v cargo >/dev/null && [[ -f $root/vendor/gopher64/src/psgopher_zip.rs ]]; then
    (cd "$root/ps5/tests/update_zip" && CARGO_TARGET_DIR="$root/build/update-zip" cargo build -q --release)
    ${CXX:-g++} -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined \
        -fno-sanitize-recover=undefined -fno-omit-frame-pointer -pthread -DPSGOPHER_UPDATE_TEST -I"$root/ps5/host" \
        "$root/ps5/tests/test_update.cpp" "$root/ps5/host/update.cpp" "$root/build/update-zip/release/libupdate_zip.a" \
        -ldl -lm -o "$out/test_update"
    rm -rf "$out/update"
    ASAN_OPTIONS=detect_leaks=1 "$out/test_update" "$out/update" 2> "$out/test_update.log" || { cat "$out/test_update.log"; exit 1; }
else
    echo "test_update: skipped (needs cargo and vendor/gopher64: ps5/tools/fetch-gopher64.sh)"
fi

# the write broker (ps5/broker: the payload's request handler) and its client, over sockets, with the
# sanitizers (the copy of a share's ROM through it is in test_library above)
bash "$root/ps5/tests/broker/run-broker-tests.sh" "$out/broker"

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
        "$root/ps5/tests/test_menu.cpp" "$root/ps5/host/menu.cpp" "$root/ps5/host/library.cpp" "$root/ps5/host/romhack.cpp" "$root/ps5/host/nointro.cpp" \
        "$root/ps5/host/frontend.cpp" "$root/ps5/host/board.cpp" "$root/ps5/host/image.cpp" "$root/ps5/host/web.cpp" \
        "$root/ps5/host/title_settings.cpp" "$root/ps5/host/game_settings.cpp" "$root/ps5/host/expansion_pak.cpp" "$root/ps5/host/shares.cpp" "$root/ps5/host/sandbox.cpp" \
        "$root/ps5/host/write_broker.cpp" \
        "$out/volk.o" "$out"/imgui_*.o -o "$out/test_menu" -lpthread -ldl
    [[ -f $lavapipe ]] && export VK_ICD_FILENAMES=$lavapipe
    "$out/test_menu" "$out/menu-work" "$out/menu-pictures"
    # the words of BENCH's rows (the test dumped them): Gopher64's labels exact, the banned words out
    python3 "$root/ps5/tools/settings-words.py" check "$out/menu-work/settings-words.jsonl"
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

# the picture's shaders (ps5/shaders): compiled and validated (glslangValidator, spirv-val) as
# make-shaders.sh does, the headers the title carries checked to be theirs; then the final filters
# (Sharp Bilinear, Bicubic, Lanczos), Post Anti-Aliasing (FXAA) and Sharpening (CAS) drawn through the PC's
# Vulkan (lavapipe) on small pictures and compared with CPU references (test_picture_filters.cpp)
if [[ -f $rdp/volk/volk.c ]] && command -v glslangValidator >/dev/null && command -v spirv-val >/dev/null; then
    rm -rf "$out/shaders" && mkdir -p "$out/shaders"
    PSGOPHER_SHADER_OUT="$out/shaders" bash "$root/ps5/tools/make-shaders.sh" >/dev/null
    for header in "$out"/shaders/*.hpp; do
        cmp -s "$header" "$root/vendor/gopher64/parallel-rdp/${header##*/}" ||
            { echo "${header##*/}: not made from ps5/shaders (run ps5/tools/make-shaders.sh)" >&2; exit 1; }
    done
    [[ -f $out/volk.o ]] || ${CC:-gcc} -O1 -g -fsanitize=address,undefined -I"$rdp/vulkan-headers/include" -c "$rdp/volk/volk.c" -o "$out/volk.o"
    ${CXX:-g++} -std=c++20 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fsanitize=address,undefined \
        -I"$rdp/volk" -I"$rdp/vulkan-headers/include" -I"$root/vendor/gopher64/parallel-rdp" \
        "$root/ps5/tests/test_picture_filters.cpp" "$out/volk.o" -o "$out/test_picture_filters" -ldl
    [[ -f $lavapipe ]] && export VK_ICD_FILENAMES=$lavapipe
    mkdir -p "$out/picture-filters"
    "$out/test_picture_filters" "$out/picture-filters"
else
    echo "test_picture_filters: skipped (needs vendor/gopher64, glslang-tools and spirv-tools)"
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
