#!/usr/bin/env bash
# PSGopher - Gopher64 at the pinned revision in vendor/gopher64, with ps5/patches/gopher64/ps5.patch.
#
#   ps5/tools/fetch-gopher64.sh            fetch (or reset) vendor/gopher64 and apply the patch
#   ps5/tools/fetch-gopher64.sh --export   write vendor/gopher64's changes back to ps5.patch
#   ps5/tools/fetch-gopher64.sh --check    apply the patch to a fresh checkout at the pin (no vendor change)
#
# Gopher64 (GPL-3.0, github.com/gopher64/gopher64) is not modified on GitHub: the port's changes
# to it are one patch, kept small and cfg-gated, so moving the pin is a rebase of one file.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
upstream=https://github.com/gopher64/gopher64.git
# master of 2026-11-24 ("better handling of VI_V_INTR_REG (#1441)"), v1.2.4 + 0
revision=439e6c11f69d7aab7ca1e5cb217a3c62eee00921
patch="$root/ps5/patches/gopher64/ps5.patch"
vendor="$root/vendor/gopher64"

checkout() { # $1: directory
    local dir=$1
    if [[ ! -d $dir/.git ]]; then
        git clone -q --filter=blob:none "$upstream" "$dir"
    fi
    git -C "$dir" fetch -q origin "$revision" 2>/dev/null || git -C "$dir" fetch -q origin
    git -C "$dir" checkout -q --force "$revision"
    git -C "$dir" clean -qfdx
    git -C "$dir" submodule update -q --init --recursive --force
}

case ${1:-} in
    --export)
        [[ -d $vendor/.git ]] || { echo "no vendor/gopher64: run ${0##*/} first" >&2; exit 1; }
        git -C "$vendor" add -A -N
        git -C "$vendor" diff --binary "$revision" -- . ':!parallel-rdp/parallel-rdp-standalone' > "$patch"
        echo "==> $(git -C "$vendor" diff --stat "$revision" -- . | tail -1) -> ${patch#$root/}"
        ;;
    --check)
        tmp=$(mktemp -d)
        trap 'rm -rf "$tmp"' EXIT
        checkout "$tmp/gopher64"
        git -C "$tmp/gopher64" apply --check "$patch" && echo "==> ps5.patch applies to $revision"
        ;;
    "")
        mkdir -p "$root/vendor"
        checkout "$vendor"
        if [[ -s $patch ]]; then
            git -C "$vendor" apply "$patch"
        fi
        echo "==> vendor/gopher64 at ${revision:0:12}$([[ -s $patch ]] && echo " + ps5.patch")"
        ;;
    *) echo "usage: ${0##*/} [--export|--check]" >&2; exit 2 ;;
esac
