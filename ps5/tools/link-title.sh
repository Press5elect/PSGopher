#!/usr/bin/env bash
# PSGopher - link the title through PS5_Vulkan's RADV recipe, sign it, and lay out dist/<title id>/.
#
#   ps5/tools/link-title.sh WORK VULKAN SDK OBJECTS...      (build.sh calls it)
#
# PS5 Vulkan Template's link-title.sh (MIT) without its samples: the CRT, the AGC stubs RADV
# needs, radv_link_recipe (the heap and thread wraps, the libc bindings, RADV whole), the check that
# no import is null at run time, then the native tool's ELF-to-SELF.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
work=$1 vulkan=$2 sdk=$3
shift 3
objects=("$@")
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# RADV is Crux2's: PS5_Vulkan's PS5 Mesa pin with Crux2's driver patches (ps5/patches/mesa-27 in
# ../Crux2), built by Crux2's ps5/tools/build-radv-crux2.sh into PS5_Vulkan's .deps/native/radv-crux2.
# PS5_Vulkan's plain release archive is never linked by mistake: without radv-crux2 the link stops.
archive=${RADV_ARCHIVE:-$vulkan/.deps/native/radv-crux2/lib/libvulkan_radeon.ps5.a}
[[ -f $archive ]] || { echo "link-title.sh: no Crux2 RADV at $archive: run ../Crux2/ps5/tools/build-radv-crux2.sh" >&2; exit 2; }
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
tool="$vulkan/build/host/ps5-native-tool"
native="$vulkan/tooling/native"
param="$root/ps5/sce_sys/param.json"
for file in "$archive" "$tool" "$param" "$sdk/bin/prospero-lld" "$vulkan/tools/radv-link.sh" "$vulkan/runtime/libc.prx"; do
    [[ -e $file ]] || { echo "missing: $file" >&2; exit 2; }
done

mkdir -p "$work/link/obj" "$work/link/stubs"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c "$native/app_crt.cpp" -o "$work/link/obj/app_crt.o"
stub() {
    local library=$1 source=$2
    cc -std=c11 -O2 -fPIC -c "$vulkan/$source" -o "$work/link/obj/${library}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "${library}.prx" -o "$work/link/stubs/${library}.so" \
        "$work/link/obj/${library}_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c

source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$archive" || exit 2

# The network shares' name lookups (ps5/host/libc_gaps.c, Crux2's and OGBox's ps5_libc.c): the
# console's resolver in place of the platform layer's getaddrinfo, which refuses every lookup and
# which libsmb2 and libnfs call even for a numeric address
for i in "${!radv_link_flags[@]}"; do
    case ${radv_link_flags[$i]} in
    --defsym=getaddrinfo=ps5_getaddrinfo) radv_link_flags[$i]=--defsym=getaddrinfo=ps5px_getaddrinfo ;;
    --defsym=freeaddrinfo=ps5_freeaddrinfo) radv_link_flags[$i]=--defsym=freeaddrinfo=ps5px_freeaddrinfo ;;
    esac
done
# lstat answered by stat where the sandbox refuses it, poll restarted after a signal, TCP buffers
# made large before a share's connection (OGBox's measured 18 MB/s limit): OGBox's wraps
radv_link_flags+=(--wrap=lstat --wrap=poll --wrap=setsockopt)

# PSGopher's own bindings, the recipe's way (kept local, so nothing is exported): names Rust's std
# and its crates import that no console module exports, to the platform layer's functions or to
# ps5/host/rust_stubs.c's (docs/dev/dependencies.md)
bindings=(accept4=ps5_accept4 gai_strerror=ps5_gai_strerror realpath=ps5_realpath
    getrandom=psgopher_getrandom dl_iterate_phdr=psgopher_dl_iterate_phdr
    pthread_setname_np=psgopher_pthread_setname_np recvmmsg=psgopher_recvmmsg getprotobyname=ps5px_getprotobyname)
{
    printf '{\n    local:\n'
    for binding in "${bindings[@]}"; do
        printf '        %s;\n' "${binding%%=*}"
        radv_link_flags+=("--defsym=$binding")
    done
    printf '        __wrap_lstat;\n        __wrap_poll;\n        __wrap_setsockopt;\n'
    printf '};\n'
} > "$work/link/psgopher-local.map"
radv_link_flags+=(--version-script "$work/link/psgopher-local.map")

# The core's profiling build (PSGOPHER_PGO=generate, build-core.sh): its counters and the records
# that point at them are written while the title runs, so they go with the writable data (Crux2's
# link-title.sh: left to the linker they followed .got.plt into the region the native tool keeps
# for the process parameters)
if [[ ${PSGOPHER_PGO:-} == generate ]]; then
    cat > "$work/link/pgo-sections.ld" <<'EOF_LD'
SECTIONS {
    __llvm_prf_cnts : { KEEP(*(__llvm_prf_cnts)) } : data
    __llvm_prf_bits : { KEEP(*(__llvm_prf_bits)) } : data
    __llvm_prf_data : { KEEP(*(__llvm_prf_data)) } : data
    __llvm_prf_vals : { KEEP(*(__llvm_prf_vals)) } : data
    __llvm_prf_vnds : { KEEP(*(__llvm_prf_vnds)) } : data
} INSERT AFTER .data;
EOF_LD
    radv_linker_script+=(-T "$work/link/pgo-sections.ld")
    # and LLVM's profile runtime itself (rustc's profiler_builtins does not bundle it into the core)
    objects+=("$("$root/ps5/tools/build-profile-runtime.sh" "$sdk")")
fi

# Mesa's dispatch tables name entry points through weak references: an undefined weak symbol must
# not become an import the native tool cannot resolve (Crux2's and PS5_RetroArch's flags; the
# FreeBSD target defaults to -z dynamic-undefined-weak, PS5_Vulkan's check-vulkan-runtime.sh)
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --no-dynamic-linker -z nodynamic-undefined-weak --error-limit=0 \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL -Map="$work/link/llvm-pie.map" \
    -e _start -o "$work/link/llvm-pie.elf" \
    "$work/link/obj/app_crt.o" --start-group "${objects[@]}" --end-group \
    "$work/link/stubs/libSceAgc.so" "$work/link/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "$sdk"/target/lib/*.so

# Every import must come from a module a title loads: libkernel_sys's and libScePosixForWebKit's
# stubs define names that are null in a title (PS5_Vulkan's recipe binds those it knows).
null_imports=$(comm -23 \
    <("$sdk/bin/llvm-nm" -D --undefined-only "$work/link/llvm-pie.elf" |
        awk '$1 == "U" { sub(/@.*/, "", $2); print $2 }' | sort -u) \
    <(for library in "$sdk"/target/lib/*.so "$work/link/stubs/libSceAgc.so" "$work/link/stubs/libSceAgcDriver.so"; do
        case ${library##*/} in libkernel_sys.so | libScePosixForWebKit.so) continue ;; esac
        "$sdk/bin/llvm-nm" -D --defined-only "$library" 2>/dev/null | awk '{ print $NF }'
    done | sort -u))
if [[ -n $null_imports ]]; then
    echo "link-title.sh: imports no module a title loads exports (null at run time): ${null_imports//$'\n'/ }" >&2
    exit 1
fi
"$sdk/bin/llvm-nm" -D --undefined-only "$work/link/llvm-pie.elf" | awk '{ sub(/@.*/, "", $2); print $2 }' | sort -u \
    > "$work/imports.txt"

"$tool" link --in "$work/link/llvm-pie.elf" --out "$work/eboot.elf.new" \
    --stub-dir "$sdk/target/lib" --stub "$work/link/stubs/libSceAgc.so" \
    --stub "$work/link/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf

title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$param")
app="$root/dist/$title_id"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$work/eboot.elf.new" --out "$app/eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
cp "$param" "$app/sce_sys/param.json"
cp "$root/ps5/sce_sys/icon0.png" "$app/sce_sys/icon0.png"
# the home screen's pictures: behind the selected title and while it loads (ps5/tools/make-title-art.py)
cp "$root/ps5/sce_sys/pic0.dds" "$root/ps5/sce_sys/pic1.dds" "$app/sce_sys/"
# the sandbox exit's helper, sent to the payload loader from /app0 (ps5/host/sandbox.cpp)
cp "$root/build/sandbox-elevator.elf" "$app/sandbox-elevator.elf"
# the write broker, sent to the payload loader for the copy of a share's ROM (ps5/host/write_broker.cpp)
cp "$root/build/write-broker.elf" "$app/write-broker.elf"
# the front end's pictures (ps5/art: the Live Board's mainboard, made by make-board-art.mjs, and the logo)
mkdir -p "$app/art"
cp "$root/ps5/art/board.png" "$root/ps5/art/logo.png" "$app/art/"
# which games use the Expansion Pak (ps5/host/expansion_pak.h)
mkdir -p "$app/gamedata"
cp "$root/ps5/gamedata/expansion-pak.tsv" "$app/gamedata/"
# the attributions kept and the other authors' parts (NOTICE), and the fonts' licence
cp "$root/NOTICE" "$app/NOTICE.txt"
# the licence the program is under (GPL-3.0: it goes with every copy of the binary), the user's guide
# and the changes (the updater replaces them with each release: ps5/host/update.cpp)
cp "$root/LICENSE" "$app/LICENSE.txt"
cp "$root/README.md" "$app/README.md"
cp "$root/CHANGELOG.md" "$app/CHANGELOG.md"
mkdir -p "$app/licenses"
cp "$root/.deps/src/bricolage/OFL.txt" "$app/licenses/OFL-BricolageGrotesque.txt"
cp "$root/.deps/src/jetbrains-mono/OFL.txt" "$app/licenses/OFL-JetBrainsMono.txt"
cp "$root/ps5/licenses/ISC-ares.txt" "$app/licenses/ISC-ares.txt"
cp "$root/ps5/licenses/MIT-AMD-FidelityFX-CAS.txt" "$app/licenses/MIT-AMD-FidelityFX-CAS.txt"
cp "$root/ps5/licenses/BSD-3-NVIDIA-FXAA.txt" "$app/licenses/BSD-3-NVIDIA-FXAA.txt"
(cd "$vulkan/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
cp "$vulkan/runtime/libc.prx" "$app/sce_module/libc.prx"
mv "$work/eboot.elf.new" "$work/eboot.elf"
archive_revision=$(sed -n 's/^revision: //p' "$(dirname "$(dirname "$archive")")/PROVENANCE.txt" 2>/dev/null || true)
printf '==> %s: %s (eboot.bin %s bytes; RADV %s; %s imports)\n' "$title_id" "$app" \
    "$(stat -c %s "$app/eboot.bin")" "${archive_revision:0:11}" "$(wc -l < "$work/imports.txt")"
