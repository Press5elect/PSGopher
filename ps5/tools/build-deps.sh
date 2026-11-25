#!/usr/bin/env bash
# PSGopher - the third-party libraries the PS5 code builds with, into .deps/ (ignored).
#
#   ps5/tools/build-deps.sh       fetch and build what is missing (prints the prefix: .deps/ports)
#
#   libsmb2   SMB2/3 shares for ps5vfs (ps5/vfs): its master, built with the SDK's CMake toolchain
#             (the ports release builds its 6.2 tag the same way, with no patches; Crux2 and OGBox
#             link that one).
#   libnfs    NFS shares for ps5vfs: its master, with Crux2's flags (Crux2 pins 7613236, 8.0.0+4).
#   imgui     Dear ImGui, the game list's drawing (ps5/host/menu.cpp), with its Vulkan backend: master.
#
# Newest of each, as the owner's rule asks; check-sources.sh says when one moved on.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
sdk=$("$root/ps5/tools/setup-sdk.sh")
deps="$root/.deps"
src="$deps/src"
sys="$deps/ports"
bld="$deps/build"
mkdir -p "$src" "$sys/include" "$sys/lib" "$bld"

libsmb2_revision=5fdd128012b384ed2a79293611d7f7dbaaa84cc7   # master, 2026-11-25
libnfs_revision=3fb4f2a299cef533115d1658fe8261144b2f3e18    # master, 2026-11-25
imgui_revision=667c01946451f71f7852aeda8acbdf41d1b9f36a     # master, 2026-11-25 (after v1.93.0)

step() { echo "==> [deps] $1" >&2; }
fetch() { # name url revision
    local name=$1 url=$2 revision=$3
    if [[ $(cat "$src/$name/.psgopher-revision" 2>/dev/null) == "$revision" ]]; then return; fi
    step "fetching $name ${revision:0:12}"
    rm -rf "${src:?}/$name"
    mkdir -p "$src/$name"
    git -C "$src/$name" init -q
    git -C "$src/$name" remote add origin "$url"
    git -C "$src/$name" fetch -q --depth 1 origin "$revision"
    git -C "$src/$name" -c advice.detachedHead=false checkout -q FETCH_HEAD
    echo "$revision" > "$src/$name/.psgopher-revision"
    rm -f "$bld/$name.done"
}
fetch libsmb2 https://github.com/sahlberg/libsmb2 "$libsmb2_revision"
fetch libnfs https://github.com/sahlberg/libnfs "$libnfs_revision"
fetch imgui https://github.com/ocornut/imgui "$imgui_revision"

common=(-G Ninja -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$sys" -DCMAKE_PREFIX_PATH="$sys" -DCMAKE_FIND_ROOT_PATH="$sys"
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
build() { # name cmake-arguments...
    local name=$1
    shift
    [[ -f $bld/$name.done ]] && return
    step "building $name"
    rm -rf "${bld:?}/$name"
    cmake "${common[@]}" -B "$bld/$name" "$@" > "$bld/$name.log" 2>&1 &&
        cmake --build "$bld/$name" >> "$bld/$name.log" 2>&1 &&
        cmake --install "$bld/$name" >> "$bld/$name.log" 2>&1 ||
        { tail -30 "$bld/$name.log" >&2; exit 1; }
    touch "$bld/$name.done"
}
flags="-ffunction-sections -fdata-sections -fno-omit-frame-pointer"
build libsmb2 -S "$src/libsmb2" -DENABLE_EXAMPLES=OFF -DBUILD_TESTING=OFF "-DCMAKE_C_FLAGS=$flags"
# (Crux2's flags: the coarse clock the console lacks, no portmapper scan, and the three calls its
# libc refuses answered by ps5vfs's NFS backend)
nfs_flags="$flags -DCLOCK_MONOTONIC_COARSE=CLOCK_MONOTONIC -DNO_SRV_AUTOSCAN"
nfs_flags+=" -Dgetservbyport=ps5vfs_nfs_getservbyport -Dgetuid=ps5vfs_nfs_getuid -Dgetgid=ps5vfs_nfs_getgid"
build libnfs -S "$src/libnfs" -DENABLE_TLS=OFF -DENABLE_MULTITHREADING=OFF -DENABLE_UTILS=OFF \
    -DENABLE_EXAMPLES=OFF -DENABLE_TESTS=OFF -DENABLE_DOCUMENTATION=OFF "-DCMAKE_C_FLAGS=$nfs_flags"
rm -rf "$sys/lib/cmake" "$sys/lib/pkgconfig"
echo "$sys"
