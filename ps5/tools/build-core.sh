#!/usr/bin/env bash
# PSGopher - Gopher64's core (vendor/gopher64, the library) as a static library for the PS5.
#
#   ps5/tools/build-core.sh            -> build/core/x86_64-sie-ps5/release/libgopher64.a
#   PSGOPHER_PROFILE=release-with-debug ps5/tools/build-core.sh
#
# Rust's standard library is built again for the console's target (ps5/rust/x86_64-sie-ps5.json:
# FreeBSD's ABI and libc, emulated thread-locals as the console's clang makes them, no dynamic
# linking; OGBox's recipe for its DSP56300 JIT, which runs on the console), so the build takes a
# nightly toolchain with rust-src. The C and C++ that Gopher64's build script compiles
# (parallel-rdp, volk, rcheevos) and the crates' own C go through the payload SDK's compilers.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
toolchain=${PSGOPHER_RUST:-nightly}
profile=${PSGOPHER_PROFILE:-release}
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

cd "$src"
cargo "+$toolchain" rustc --lib --profile "$profile" --crate-type staticlib \
    -Zjson-target-spec -Zbuild-std=std,panic_unwind --target "$spec" --no-default-features "$@"
lib="$out/x86_64-sie-ps5/${profile/dev/debug}/libgopher64.a"
echo "==> $(du -h "$lib" | cut -f1) ${lib#$root/}"
rustc "+$toolchain" --version > "$out/toolchain.txt"
