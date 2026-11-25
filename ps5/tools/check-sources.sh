#!/usr/bin/env bash
# PSGopher - is every source at its upstream's newest? (the owner's rule: nightly sources)
#
#   ps5/tools/check-sources.sh     exit 1 when a pin is behind (build.sh runs it; PSGOPHER_ALLOW_STALE=1)
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -uo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
vulkan=${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}
behind=0
check() { # name pinned newest
    if [[ -z $3 ]]; then
        printf '  ?       %-22s %s (upstream not reachable)\n' "$1" "${2:0:12}"
    elif [[ $2 == "$3"* || $3 == "$2"* ]]; then
        printf '  ok      %-22s %s\n' "$1" "${2:0:12}"
    else
        printf '  behind  %-22s %s, upstream %s\n' "$1" "${2:0:12}" "${3:0:12}"
        behind=1
    fi
}
head_of() { git ls-remote "$1" "${2:-HEAD}" 2>/dev/null | head -1 | cut -f1; }

check gopher64 "$(sed -n 's/^revision=//p' "$root/ps5/tools/fetch-gopher64.sh")" \
    "$(head_of https://github.com/gopher64/gopher64.git refs/heads/main)$(head_of https://github.com/gopher64/gopher64.git refs/heads/master)"
check PS5_PayloadSDK "$(sed -n 's/^sdk_revision=\([0-9a-f]*\).*/\1/p' "$root/ps5/tools/setup-sdk.sh")" \
    "$(head_of https://github.com/mihawk-99/PS5_PayloadSDK.git)"
check PS5_Vulkan "$(git -C "$vulkan" rev-parse HEAD 2>/dev/null)" "$(head_of https://github.com/mihawk-99/PS5_Vulkan.git)"
# RADV: Crux2's archive, built on PS5_Vulkan's Mesa pin with exactly the patches Crux2 has now
crux2=${CRUX2_DIR:-$root/../Crux2}
mesa_pin=$(sed -n 's/^mesa_revision=//p' "$vulkan/tools/build-radv.sh" 2>/dev/null | grep -o '[0-9a-f]\{40\}')
provenance="$vulkan/.deps/native/radv-crux2/PROVENANCE.txt"
built=$(sed -n 's/^Crux2: PS5 Mesa \([0-9a-f]*\).*/\1/p' "$provenance" 2>/dev/null)
check "RADV (Crux2's, Mesa pin)" "$built" "$mesa_pin"
have=$(sed -n 's/^  \([0-9][0-9][0-9][0-9]-.*\.patch\)$/\1/p' "$provenance" 2>/dev/null | sort | tr '\n' ' ')
want=$(ls "$crux2/ps5/patches/mesa-27/" 2>/dev/null | grep '\.patch$' | sort | tr '\n' ' ')
if [[ -z $want ]]; then
    printf '  ?       %-22s no Crux2 checkout at %s\n' "RADV (Crux2's patches)" "$crux2"
elif [[ $have == "$want" ]]; then
    printf '  ok      %-22s %s patches, Crux2 %s\n' "RADV (Crux2's patches)" "$(wc -w <<< "$want")" "$(git -C "$crux2" rev-parse --short=12 HEAD)"
else
    printf '  behind  %-22s the archive was not built with Crux2'"'"'s current patches: run %s\n' "RADV (Crux2's patches)" "$crux2/ps5/tools/build-radv-crux2.sh"
    behind=1
fi
stb=$(sed -n 's/.*nothings\/stb at \([0-9a-f]*\).*/\1/p' "$root/ps5/third_party/stb/README.txt")
check stb_truetype "$stb" "$(head_of https://github.com/nothings/stb.git refs/heads/master)"
echo "  note    PS5 Mesa main is $(head_of https://github.com/mihawk-99/PS5_Mesa.git | cut -c1-12); PSGopher follows PS5_Vulkan's pin of it"
echo "  note    Rust: $(rustc +nightly --version 2>/dev/null) (rustup update nightly for the newest)"
exit $behind
