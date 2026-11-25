#!/usr/bin/env bash
# PSGopher - the core's profiles from the console merged into ps5/pgo/core.profdata, which every
# build then uses (PSGOPHER_PGO=use, build-core.sh). Crux2's method (its merge-profiles.sh).
#
#   ps5/tools/merge-profiles.sh <.profraw files or folders>...
#
# The counts come from a profiling build (PSGOPHER_PGO=generate ps5/tools/build.sh): played on the
# console, each game's counts are /data/psgopher/pgo/core-<time>-<n>.profraw. llvm-profdata is
# rustc's own (rustup's llvm-tools), of the LLVM release that wrote them.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
toolchain=${PSGOPHER_RUST:-nightly}
(($#)) || { echo "usage: ${0##*/} <.profraw files or folders>..." >&2; exit 2; }
profdata="$(rustc "+$toolchain" --print sysroot)/lib/rustlib/x86_64-unknown-linux-gnu/bin/llvm-profdata"
[[ -x $profdata ]] || rustup component add llvm-tools --toolchain "$toolchain"
files=()
for arg in "$@"; do
    if [[ -d $arg ]]; then
        while IFS= read -r -d '' f; do files+=("$f"); done < <(find "$arg" -name '*.profraw' -print0)
    else
        files+=("$arg")
    fi
done
((${#files[@]})) || { echo "merge-profiles.sh: no .profraw files" >&2; exit 1; }
mkdir -p "$root/ps5/pgo"
"$profdata" merge -o "$root/ps5/pgo/core.profdata.new" "${files[@]}"
mv "$root/ps5/pgo/core.profdata.new" "$root/ps5/pgo/core.profdata"
"$profdata" show "$root/ps5/pgo/core.profdata" | tail -4
echo "==> ${#files[@]} profile(s) -> ps5/pgo/core.profdata"
