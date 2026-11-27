#!/usr/bin/env bash
# PSGopher - build the title: dist/PPSA99280/ (eboot.bin, sce_sys/, sce_module/libc.prx).
#
#   ps5/tools/build.sh             the sources' check, Gopher64's core, the PS5 code, the link
#   ps5/tools/build.sh --compile   stop before the link
#
# The libraries the game list links (libsmb2, libnfs, Dear ImGui) are fetched and built by
# ps5/tools/build-deps.sh into .deps/, the sandbox helper by ps5/tools/build-elevator.sh.
#
# Built on Mihawk's PS5 stack beside this repository: ../PS5_Vulkan (RADV's release archive, the
# link recipe, the native tool, libc.prx) and ../PS5_PayloadSDK (this title's SDK pin,
# ps5/tools/setup-sdk.sh). docs/dev/building.md has the whole setup.
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
vulkan=${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}
compile_only=false
[[ ${1:-} == --compile ]] && compile_only=true

if [[ -x $root/ps5/tools/check-sources.sh && -z ${PSGOPHER_ALLOW_STALE:-} ]]; then
    "$root/ps5/tools/check-sources.sh" || {
        echo "build.sh: a source is behind its upstream (above); PSGOPHER_ALLOW_STALE=1 builds anyway" >&2
        exit 1
    }
fi

sdk=$("$root/ps5/tools/setup-sdk.sh")
[[ -d $root/vendor/gopher64 ]] || "$root/ps5/tools/fetch-gopher64.sh"
"$root/ps5/tools/build-core.sh"
core="$root/build/core/x86_64-sie-ps5/${PSGOPHER_PROFILE:-psgopher}/libgopher64.a"

work="$root/build/ps5"
mkdir -p "$work/obj"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }
# the build's identity, first in the boot log: this repository's commit (+dirty) and Gopher64's pin
identity="PSGopher $(git -C "$root" describe --always --dirty --abbrev=12), Gopher64 $(sed -n 's/^revision=//p' "$root/ps5/tools/fetch-gopher64.sh" | cut -c1-12), RADV Crux2 $(git -C "${CRUX2_DIR:-$root/../Crux2}" rev-parse --short=12 HEAD 2>/dev/null) on PS5 Mesa $(sed -n 's/^Crux2: PS5 Mesa \([0-9a-f]*\).*/\1/p' "$vulkan/.deps/native/radv-crux2/PROVENANCE.txt" | cut -c1-11), built $(date -u +%Y-%m-%dT%H:%MZ)"
# (a release is built with PSGOPHER_VERSION=vX.Y.Z; a build without one is a developer build, which the updater never offers an update)
common=(-DPSGOPHER_BUILD="\"$identity\"" -DPSGOPHER_VERSION="\"${PSGOPHER_VERSION:-}\"" -O2 -g1 -ffunction-sections -fdata-sections -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter
    -I"$root/ps5/host" -I"$root/ps5/sdl3" -I"$root/build/sdl3-include" -I"$root/ps5/third_party/stb"
    -I"$vulkan/.deps/native/radv-crux2/include" -I"$sdk/target/include")
# the game list's libraries (libsmb2, libnfs, Dear ImGui: ps5/tools/build-deps.sh) and the sandbox
# exit's helper (ps5/tools/build-elevator.sh) and the write broker (ps5/tools/build-broker.sh), and the list's font as a header
ports=$("$root/ps5/tools/build-deps.sh")
imgui="$root/.deps/src/imgui"
"$root/ps5/tools/build-elevator.sh" > /dev/null
"$root/ps5/tools/build-broker.sh" > /dev/null # (the write broker, ps5/host/write_broker.cpp)
mkdir -p "$work/gen"
# the Live Board's faces as headers (SIL Open Font License 1.1; build-deps.sh fetches them): Bricolage
# Grotesque ExtraBold for its titles, JetBrains Mono for everything else
font_header() { # ttf, header, symbol
    [[ -f $2 && ! $1 -nt $2 ]] && return
    python3 - "$1" "$2" "$3" <<'PY'
import sys, os
data = open(sys.argv[1], 'rb').read()
name = sys.argv[3]
lines = ["/* %s (SIL Open Font License 1.1). Made by ps5/tools/build.sh. */" % os.path.basename(sys.argv[1]),
         "#pragma once", "static const unsigned int %s_size = %d;" % (name, len(data)), "static const unsigned char %s[] = {" % name]
lines += ["  " + ",".join(str(b) for b in data[i:i + 32]) + "," for i in range(0, len(data), 32)]
open(sys.argv[2], "w").write("\n".join(lines + ["};", ""]))
PY
}
fonts="$root/.deps/src"
font_header "$fonts/bricolage/fonts/ttf/BricolageGrotesque-ExtraBold.ttf" "$work/gen/font_bricolage.h" font_bricolage
font_header "$fonts/jetbrains-mono/fonts/ttf/JetBrainsMono-Regular.ttf" "$work/gen/font_mono.h" font_mono
font_header "$fonts/jetbrains-mono/fonts/ttf/JetBrainsMono-SemiBold.ttf" "$work/gen/font_mono_bold.h" font_mono_bold
font_header "$fonts/jetbrains-mono/fonts/ttf/JetBrainsMono-ExtraBold.ttf" "$work/gen/font_mono_heavy.h" font_mono_heavy
rdp="$root/vendor/gopher64/parallel-rdp/parallel-rdp-standalone"
list=(-I"$root/ps5/vfs" -I"$ports/include" -I"$imgui" -I"$imgui/backends" -I"$work/gen" -I"$rdp/volk"
    -I"$rdp/vulkan-headers/include" -DIMGUI_IMPL_VULKAN_USE_VOLK -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS
    -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS) # (its default opens links with fork, which a title has not)
jobs=()
compile() { # language output source flags...
    local language=$1 output=$2 source=$3
    shift 3
    cc "$language" "${common[@]}" "$@" -c "$source" -o "$work/obj/$output" &
    jobs+=($!)
}
compile -std=c11 platform.o "$root/ps5/host/platform.c"
compile -std=c11 rust_stubs.o "$root/ps5/host/rust_stubs.c"
compile -std=c11 bench.o "$root/ps5/host/bench.c"
compile -std=c11 libc_gaps.o "$root/ps5/host/libc_gaps.c"
compile -std=gnu11 crash.o "$root/ps5/host/crash.c"
compile -std=gnu11 rust_abi.o "$root/ps5/host/rust_abi.c" # (sys/event.h takes the BSD types)
compile -std=c++20 main.o "$root/ps5/host/main.cpp" -I"$root/ps5/vfs"
compile -std=c++20 videoout_hooks.o "$root/ps5/host/videoout_hooks.cpp"
compile -std=c++20 psgopher_sdl3.o "$root/ps5/sdl3/psgopher_sdl3.cpp" -Wno-unused-function -Wno-missing-field-initializers
compile -std=c++20 sandbox.o "$root/ps5/host/sandbox.cpp" -Wno-missing-field-initializers
compile -std=c++20 title_settings.o "$root/ps5/host/title_settings.cpp"
compile -std=c++20 game_settings.o "$root/ps5/host/game_settings.cpp"
compile -std=c++20 expansion_pak.o "$root/ps5/host/expansion_pak.cpp"
compile -std=c++20 library.o "$root/ps5/host/library.cpp" "${list[@]}"
compile -std=c++20 romhack.o "$root/ps5/host/romhack.cpp"
compile -std=c++20 write_broker.o "$root/ps5/host/write_broker.cpp"
compile -std=c++20 shares.o "$root/ps5/host/shares.cpp" "${list[@]}"
compile -std=c++20 menu.o "$root/ps5/host/menu.cpp" "${list[@]}" -Wno-missing-field-initializers
compile -std=c++20 quickmenu.o "$root/ps5/host/quickmenu.cpp" "${list[@]}" -Wno-missing-field-initializers
compile -std=c++20 board.o "$root/ps5/host/board.cpp" "${list[@]}"
compile -std=c++20 frontend.o "$root/ps5/host/frontend.cpp" "${list[@]}" -Wno-missing-field-initializers
compile -std=c++20 image.o "$root/ps5/host/image.cpp" -w
compile -std=c++20 web.o "$root/ps5/host/web.cpp" "${list[@]}"
compile -std=c++20 update.o "$root/ps5/host/update.cpp"
compile -std=c++20 update_net.o "$root/ps5/host/update_net.cpp"
for vfs in ps5_vfs ps5_vfs_smb ps5_vfs_nfs ps5_vfs_ftp; do
    compile -std=c++20 "$vfs.o" "$root/ps5/vfs/$vfs.cpp" "${list[@]}" -Wno-missing-field-initializers -Wno-sign-compare
done
for source in imgui imgui_draw imgui_tables imgui_widgets backends/imgui_impl_vulkan; do
    compile -std=c++20 "imgui_${source##*/}.o" "$imgui/$source.cpp" "${list[@]}" -w
done
failed=0
for job in "${jobs[@]}"; do wait "$job" || failed=1; done
[[ $failed == 0 ]] || { echo "build.sh: the PS5 code did not compile (above)" >&2; exit 1; }
echo "==> the PS5 code compiled"
$compile_only && exit 0

objects=()
for object in main platform psgopher_sdl3 rust_stubs bench libc_gaps rust_abi crash videoout_hooks sandbox title_settings game_settings expansion_pak library romhack shares write_broker menu quickmenu board frontend image web update update_net \
    ps5_vfs ps5_vfs_smb ps5_vfs_nfs ps5_vfs_ftp imgui_imgui imgui_imgui_draw imgui_imgui_tables imgui_imgui_widgets \
    imgui_imgui_impl_vulkan; do
    objects+=("$work/obj/$object.o")
done
"$root/ps5/tools/link-title.sh" "$work" "$vulkan" "$sdk" \
    "${objects[@]}" "$ports/lib/libsmb2.a" "$ports/lib/libnfs.a" "$ports/lib/libdcerpc.a" "$core"
