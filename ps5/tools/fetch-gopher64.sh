#!/usr/bin/env bash
# PSGopher - Gopher64 at the pinned revision in vendor/gopher64, with ps5/patches/gopher64/ps5.patch.
#
#   ps5/tools/fetch-gopher64.sh            fetch (or reset) vendor/gopher64 and apply the patch
#   ps5/tools/fetch-gopher64.sh --export   write vendor/gopher64's changes back to ps5.patch (and
#                                          the parallel-rdp submodule's to parallel-rdp.patch)
#   ps5/tools/fetch-gopher64.sh --check    apply the patches to a fresh checkout at the pin (no vendor change)
#
# Gopher64 (GPL-3.0, github.com/gopher64/gopher64) is not modified on GitHub: the port's changes
# to it are one patch, kept small and cfg-gated, so moving the pin is a rebase of one file.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
upstream=https://github.com/gopher64/gopher64.git
# master of 2026-11-26 (pull request 1453: the per-game ROM patches moved to cart/patches.rs)
revision=1cb0ff2518e0ce6291e1677a4dd36e4d15a5e8ab
patch="$root/ps5/patches/gopher64/ps5.patch"
vendor="$root/vendor/gopher64"
# parallel-rdp is a submodule of Gopher64's (its own repository, at the commit Gopher64's pin
# records): its changes are a patch of their own, applied inside it
sub=parallel-rdp/parallel-rdp-standalone
rdp_patch="$root/ps5/patches/gopher64/parallel-rdp.patch"
sub_commit() { git -C "$1" ls-tree "$revision" "$sub" | awk '{ print $3 }'; }

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
        echo "==> $(git -C "$vendor" diff --stat "$revision" -- . ':!'"$sub" | tail -1) -> ${patch#$root/}"
        git -C "$vendor/$sub" add -A -N
        git -C "$vendor/$sub" diff --binary "$(sub_commit "$vendor")" -- . > "$rdp_patch"
        if [[ -s $rdp_patch ]]; then
            echo "==> $(git -C "$vendor/$sub" diff --stat "$(sub_commit "$vendor")" -- . | tail -1) -> ${rdp_patch#$root/}"
        else
            rm -f "$rdp_patch"
        fi
        ;;
    --check)
        tmp=$(mktemp -d)
        trap 'rm -rf "$tmp"' EXIT
        checkout "$tmp/gopher64"
        git -C "$tmp/gopher64" apply --check "$patch" && echo "==> ps5.patch applies to $revision"
        if [[ -s $rdp_patch ]]; then
            git -C "$tmp/gopher64/$sub" apply --check "$rdp_patch" &&
                echo "==> parallel-rdp.patch applies to its submodule at $(sub_commit "$tmp/gopher64" | cut -c1-12)"
        fi
        ;;
    "")
        mkdir -p "$root/vendor"
        checkout "$vendor"
        if [[ -s $patch ]]; then
            git -C "$vendor" apply "$patch"
        fi
        if [[ -s $rdp_patch ]]; then
            git -C "$vendor/$sub" apply "$rdp_patch"
        fi
        echo "==> vendor/gopher64 at ${revision:0:12}$([[ -s $patch ]] && echo " + ps5.patch")$([[ -s $rdp_patch ]] && echo " + parallel-rdp.patch")"
        ;;
    *) echo "usage: ${0##*/} [--export|--check]" >&2; exit 2 ;;
esac
