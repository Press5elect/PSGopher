#!/usr/bin/env bash
# PSGopher - lockstep correctness harness: Gopher64's VR4300 and RSP interpreters, the pristine
# pin ("original") against vendor/gopher64 ("candidate"), on the host (x86-64 Linux).
#
#   ps5/tests/run-lockstep.sh                          self-test: rsp-unit, then compare on the
#                                                      built-in synthetic ROM (no ROM needed)
#   ps5/tests/run-lockstep.sh compare --rom PATH --vis 600 [--rsp-every 1] [--state FILE]
#   ps5/tests/run-lockstep.sh rsp-unit [--trials 20000] [--seed N]
#   ps5/tests/run-lockstep.sh trace --core original --rom PATH --vis 300 --out FILE
#   ps5/tests/run-lockstep.sh diff-traces A B
#   ps5/tests/run-lockstep.sh make-rom OUT.z64        write the synthetic ROM
#
# Options before the command:
#   --candidate DIR      the candidate tree (default vendor/gopher64)
#   --original-ref REV   the original's revision (default the pin in fetch-gopher64.sh), taken with
#                        git archive from vendor/gopher64's repository, so it is the clean pin
#   --original DIR       a tree as the original instead (e.g. a second working tree)
#   --work DIR           scratch (default $LOCKSTEP_WORK, else /tmp/psgopher-lockstep); never the
#                        repository
#
# Each build is a small crate made in the scratch folder: the tree's src/device.rs, src/device/,
# src/cheats.rs and src/savestates.rs, copied unchanged, plus ps5/tests/lockstep/core (the stubs
# for the front end: no window, GPU, audio or input). The one change to the copies: a call to
# lockstep_hook::rsp_task after each RSP burst, injected into both builds alike. vendor/gopher64
# itself is not touched, so the PS5 build is unaffected. The harness links both builds into one
# binary (ps5/tests/lockstep/harness). No ROM is committed or downloaded.
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
here="$root/ps5/tests/lockstep"
candidate="$root/vendor/gopher64"
repo="$root/vendor/gopher64" # where the original revision is read from
original_ref=$(sed -n 's/^revision=\([0-9a-f]*\).*/\1/p' "$root/ps5/tools/fetch-gopher64.sh")
original_dir=
work=${LOCKSTEP_WORK:-/tmp/psgopher-lockstep}
toolchain=${PSGOPHER_HOST_RUST:-1.100.0}

while [[ $# -gt 0 ]]; do
    case $1 in
        --candidate) candidate=$(cd "$2" && pwd); shift 2 ;;
        --original-ref) original_ref=$2; shift 2 ;;
        --original) original_dir=$(cd "$2" && pwd); shift 2 ;;
        --work) work=$2; shift 2 ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) break ;;
    esac
done
mkdir -p "$work"
work=$(cd "$work" && pwd)
case $work/ in "$root"/*) echo "run-lockstep.sh: --work must be outside the repository" >&2; exit 2 ;; esac
[[ -d $candidate/src/device ]] || { echo "run-lockstep.sh: no Gopher64 tree at $candidate (ps5/tools/fetch-gopher64.sh)" >&2; exit 1; }

# the interpreter sources of a tree, at trees/<name>
take_tree() { # $1 name, $2 source dir or "", $3 git ref (when $2 is "")
    local dst="$work/trees/$1"
    rm -rf "${dst:?}"; mkdir -p "$dst"
    if [[ -n $2 ]]; then
        (cd "$2" && tar -cf - src/device.rs src/device src/cheats.rs src/savestates.rs data/cheats.json) | tar -xf - -C "$dst"
    else
        git -C "$repo" archive "$3" src/device.rs src/device src/cheats.rs src/savestates.rs data/cheats.json |
            tar -xf - -C "$dst"
    fi
}
take_tree candidate "$candidate"
if [[ -n $original_dir ]]; then take_tree original "$original_dir"; else take_tree original "" "$original_ref"; fi

# a core crate per tree
ws="$work/ws"
mkdir -p "$ws/.cargo"
for name in original candidate; do
    core="$ws/core-$name"
    rm -rf "${core:?}"; mkdir -p "$core/src" "$core/data"
    sed "s/@NAME@/$name/" "$here/core/Cargo.toml.in" > "$core/Cargo.toml"
    cp "$here"/core/*.rs "$core/src/"
    cp -r "$work/trees/$name/src/." "$core/src/"
    cp "$work/trees/$name/data/cheats.json" "$core/data/"
    python3 -I - "$core/src/device/rsp_interface.rs" <<'EOF'
import sys, re
path = sys.argv[1]
src = open(path).read()
pat = re.compile(r'^([ \t]*)let timer = device::rsp_cpu::run\(device\);[ \t]*\n', re.M)
found = pat.findall(src)
if len(found) != 1:
    sys.exit(f"run-lockstep.sh: expected one 'let timer = device::rsp_cpu::run(device);' in {path}, found {len(found)}: update the injection")
src = pat.sub(lambda m: m.group(0) + m.group(1) + "crate::lockstep_hook::rsp_task(device, timer); // (lockstep harness)\n", src)
open(path, "w").write(src)
EOF
done
rsync -a --delete --exclude target "$here/harness/" "$ws/harness/"
cat > "$ws/Cargo.toml" <<'EOF'
[workspace]
resolver = "3"
members = ["core-original", "core-candidate", "harness"]

[profile.release]
debug = "line-tables-only"
EOF
# the console's (and upstream Gopher64's) x86-64 code generation level
cat > "$ws/.cargo/config.toml" <<'EOF'
[target.'cfg(target_arch = "x86_64")']
rustflags = ["-C", "target-cpu=x86-64-v3"]
EOF
[[ -f $ws/Cargo.lock ]] || cp "$candidate/Cargo.lock" "$ws/Cargo.lock"

export CARGO_TARGET_DIR="$work/target"
echo "==> building original ($( [[ -n $original_dir ]] && echo "$original_dir" || echo "${original_ref:0:12}")) and candidate ($candidate)" >&2
(cd "$ws" && cargo "+$toolchain" build -q --release -p lockstep-harness)
bin="$CARGO_TARGET_DIR/release/lockstep"

if [[ $# -eq 0 ]]; then
    "$bin" rsp-unit
    "$bin" compare --synthetic --vis 120
else
    exec "$bin" "$@"
fi
