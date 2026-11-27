#!/usr/bin/env bash
# PSGopher - Gopher64's core (vendor/gopher64, the library) as a static library for the PS5.
#
#   ps5/tools/build-core.sh            -> build/core/x86_64-sie-ps5/psgopher/libgopher64.a (fat LTO)
#   PSGOPHER_PROFILE=release-with-debug ps5/tools/build-core.sh
#
# Rust's standard library is built again for the console's target (ps5/rust/x86_64-sie-ps5.json:
# FreeBSD's ABI and libc, emulated thread-locals as the console's clang makes them, no dynamic
# linking; OGBox's recipe for its DSP56300 JIT, which runs on the console), so the build takes a
# nightly toolchain with rust-src. The C and C++ that Gopher64's build script compiles
# (parallel-rdp, volk, rcheevos) and the crates' own C go through the payload SDK's compilers.
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
toolchain=${PSGOPHER_RUST:-nightly}
profile=${PSGOPHER_PROFILE:-psgopher}
sdk=$("$root/ps5/tools/setup-sdk.sh")
spec="$root/ps5/rust/x86_64-sie-ps5.json"
src="$root/vendor/gopher64"
out="$root/build/core"
[[ -d $src ]] || "$root/ps5/tools/fetch-gopher64.sh"

# SDL3's and SDL3_ttf's headers, at the release sdl3-sys's bindings are for
registry=$(dirname "$(cargo "+$toolchain" metadata --format-version 1 --manifest-path "$src/Cargo.toml" 2>/dev/null |
    python3 -c 'import json,sys; m=json.load(sys.stdin); print([p["manifest_path"] for p in m["packages"] if p["name"]=="sdl3-src"][0])')")
ttf_src=$(ls -d "$(dirname "$registry")"/sdl3-ttf-src-*/ | sort -V | tail -1)
include="$root/build/sdl3-include"
mkdir -p "$include"
rsync -a --delete "$registry/SDL/include/SDL3/" "$include/SDL3/"
rsync -a --delete "$ttf_src/SDL_ttf/include/SDL3_ttf/" "$include/SDL3_ttf/"

t=x86_64_sie_ps5
# (ZSTD_TRACE=0: zstd-sys would leave weak tracing hooks undefined, which the native tool refuses)
flags="-fno-omit-frame-pointer -ffunction-sections -fdata-sections -DZSTD_TRACE=0"
export PSGOPHER_SDK="$sdk" "CC_$t=$root/ps5/tools/sdk-cc" "CXX_$t=$root/ps5/tools/sdk-cxx" \
    "AR_$t=$sdk/bin/llvm-ar" "RANLIB_$t=$sdk/bin/llvm-ranlib" \
    "CFLAGS_$t=$flags" "CXXFLAGS_$t=$flags" \
    "BINDGEN_EXTRA_CLANG_ARGS_$t=--target=x86_64-sie-ps5 -isysroot $sdk -isystem $sdk/target/include/c++/v1 -isystem $sdk/target/include -isystem $sdk/target/include_common -femulated-tls"
export PSGOPHER_SDL3_INCLUDE="$include"
export CARGO_TARGET_DIR="$out"
# (build scripts and proc macros run on this machine, with its own compilers)
export CC=clang CXX=clang++ AR=llvm-ar

# Profile-guided optimisation (Crux2's method, docs/dev/building.md): PSGOPHER_PGO=generate builds
# a profiling core (the title writes its counts to /data/psgopher/pgo after each game), =use one
# laid out by ps5/pgo/core.profdata (the default when it is there), =off neither. The flags are
# RUSTFLAGS, which replace .cargo/config.toml's for the target: its target-cpu is kept here.
pgo=${PSGOPHER_PGO:-}
profdata="$root/ps5/pgo/core.profdata"
[[ -z $pgo ]] && { [[ -f $profdata ]] && pgo=use || pgo=off; }
std=std,panic_unwind
rustflags=(-Ctarget-cpu=x86-64-v3)
# (Gopher64's crate alone is instrumented and laid out, through cargo's per-package profile flags:
# instrumenting the standard library's own crates fails, core cannot link the profiler)
pgo_args=()
case $pgo in
    generate)
        export LLVM_PROFILER_RT_LIB=$("$root/ps5/tools/build-profile-runtime.sh" "$sdk")
        pgo_args=(-Zprofile-rustflags --config "profile.$profile.package.gopher64.rustflags=['-Cprofile-generate=/data/psgopher/pgo']")
        std+=,profiler_builtins ;;
    use)
        [[ -f $profdata ]] || { echo "build-core.sh: PSGOPHER_PGO=use without $profdata" >&2; exit 1; }
        pgo_args=(-Zprofile-rustflags --config
            "profile.$profile.package.gopher64.rustflags=['-Cprofile-use=$profdata', '-Cllvm-args=-pgo-warn-mismatch=false']") ;;
    off) ;;
    *) echo "build-core.sh: PSGOPHER_PGO is generate, use or off" >&2; exit 2 ;;
esac
export CARGO_ENCODED_RUSTFLAGS=$(IFS=$'\x1f'; echo "${rustflags[*]}")
echo "==> the core's PGO: $pgo" >&2

cd "$src"
cargo "+$toolchain" rustc --lib --profile "$profile" --crate-type staticlib ${pgo_args[@]+"${pgo_args[@]}"} \
    -Zjson-target-spec -Zbuild-std=$std --target "$spec" --no-default-features "$@"
lib="$out/x86_64-sie-ps5/${profile/dev/debug}/libgopher64.a"
# Rust's libc is FreeBSD 12's, the console's FreeBSD 11's: the core's calls of the four functions
# whose structures differ go to ps5/host/rust_abi.c's converters (no C object in the core calls
# them; the check below stops the build if one ever does, or if a call is left)
abi=(stat fstat readdir kevent)
nm="$sdk/bin/llvm-nm"
callers=$("$nm" -A -u "$lib" 2>/dev/null | awk -v names=" ${abi[*]} " 'index(names, " " $NF " ") { split($1, a, ":"); print a[2] }' | sort -u)
if [[ -n $callers ]] && grep -qv 'rcgu\.o$' <<< "$callers"; then
    echo "build-core.sh: a C object in the core calls ${abi[*]}: $(grep -v 'rcgu\.o$' <<< "$callers" | tr '\n' ' ')" >&2
    exit 1
fi
if [[ -n $callers ]]; then
    # member by member: the archive also holds LLVM bitcode (volk, parallel-rdp: thin LTO), which
    # llvm-objcopy does not take; Rust's member names are unique, so each is replaced in place
    renames=()
    for name in "${abi[@]}"; do renames+=(--redefine-sym "$name=psgopher_fb12_$name"); done
    members="$out/abi-members"
    rm -rf "$members" && mkdir -p "$members"
    while read -r member; do
        (cd "$members" && "$sdk/bin/llvm-ar" x "$lib" "$member")
        "$sdk/bin/llvm-objcopy" "${renames[@]}" "$members/$member"
        (cd "$members" && "$sdk/bin/llvm-ar" r "$lib" "$member")
    done <<< "$callers"
    rm -rf "$members"
fi
left=$("$nm" -u "$lib" 2>/dev/null | awk -v names=" ${abi[*]} " 'index(names, " " $NF " ")' | wc -l)
[[ $left == 0 ]] || { echo "build-core.sh: $left calls of ${abi[*]} left in the core" >&2; exit 1; }
echo "==> $(du -h "$lib" | cut -f1) ${lib#$root/} (stat, fstat, readdir, kevent: FreeBSD 12 converters)"
rustc "+$toolchain" --version > "$out/toolchain.txt"
