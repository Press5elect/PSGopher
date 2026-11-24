#!/usr/bin/env bash
# PSGopher - the payload SDK's compiler for crates' build scripts (cc-rs): cc-rs adds a --target
# made from the Rust target's name (x86_64-sie-freebsd), which would override the wrapper's own
# -target x86_64-sie-ps5. It is dropped; everything else goes through unchanged.
# Usage: SDK_CC=<prospero-clang or prospero-clang++> sdk-cc.sh <args>
# SPDX-License-Identifier: GPL-3.0-or-later
args=()
for a in "$@"; do [[ $a == --target=* ]] || args+=("$a"); done
exec "${SDK_CC:?}" "${args[@]}"
