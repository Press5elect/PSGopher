#!/usr/bin/env bash
# PSGopher - build the title: dist/PPSA99280/ (eboot.bin, sce_sys/, sce_module/libc.prx).
#
#   ps5/tools/build.sh             the sources' check, Gopher64's core, the PS5 code, the link
#   ps5/tools/build.sh --compile   stop before the link
#
# Built on Mihawk's PS5 stack beside this repository: ../PS5_Vulkan (RADV's release archive, the
# link recipe, the native tool, libc.prx) and ../PS5_PayloadSDK (this title's SDK pin,
# ps5/tools/setup-sdk.sh). PS5_BUILD_GUIDE.md has the whole setup.
#
# SPDX-License-Identifier: GPL-3.0-or-later
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
core="$root/build/core/x86_64-sie-ps5/release/libgopher64.a"

work="$root/build/ps5"
mkdir -p "$work/obj"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }
# the build's identity, first in the boot log: this repository's commit (+dirty) and Gopher64's pin
identity="PSGopher $(git -C "$root" describe --always --dirty --abbrev=12), Gopher64 $(sed -n 's/^revision=//p' "$root/ps5/tools/fetch-gopher64.sh" | cut -c1-12), RADV $(sed -n 's/^revision: //p' "$vulkan/.deps/native/radv-release/PROVENANCE.txt" | cut -c1-11), built $(date -u +%Y-%m-%dT%H:%MZ)"
common=(-DPSGOPHER_BUILD="\"$identity\"" -O2 -g1 -ffunction-sections -fdata-sections -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter
    -I"$root/ps5/host" -I"$root/ps5/sdl3" -I"$root/build/sdl3-include" -I"$root/ps5/third_party/stb"
    -I"$vulkan/.deps/native/radv-release/include" -I"$sdk/target/include")
cc -std=c11 "${common[@]}" -c "$root/ps5/host/platform.c" -o "$work/obj/platform.o"
cc -std=c11 "${common[@]}" -c "$root/ps5/host/rust_stubs.c" -o "$work/obj/rust_stubs.o"
cc -std=c11 "${common[@]}" -c "$root/ps5/host/bench.c" -o "$work/obj/bench.o"
cc -std=c++20 "${common[@]}" -c "$root/ps5/host/main.cpp" -o "$work/obj/main.o"
cc -std=c++20 "${common[@]}" -Wno-unused-function -Wno-missing-field-initializers \
    -c "$root/ps5/sdl3/psgopher_sdl3.cpp" -o "$work/obj/psgopher_sdl3.o"
echo "==> the PS5 code compiled"
$compile_only && exit 0

"$root/ps5/tools/link-title.sh" "$work" "$vulkan" "$sdk" \
    "$work/obj/main.o" "$work/obj/platform.o" "$work/obj/psgopher_sdl3.o" "$work/obj/rust_stubs.o" "$work/obj/bench.o" "$core"
