#!/usr/bin/env bash
# PSGopher - ps5/shaders/*.vert|frag to SPIR-V, as the headers parallel-rdp's interface includes
# (vendor/gopher64/parallel-rdp/psgopher_*_spirv.hpp, carried by ps5.patch). Run after changing a
# shader, then ps5/tools/fetch-gopher64.sh --export.
#
#   psgopher_hdr.frag                          -> psgopher_hdr_spirv.hpp      (HDR10 packing, D14)
#   psgopher_overlay.vert, psgopher_overlay.frag -> psgopher_overlay_spirv.hpp (the quick menu)
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
dir="$root/vendor/gopher64/parallel-rdp"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
tool=$(glslangValidator --version | head -1)
header() { # out, then name=spv pairs
    local out=$1
    shift
    python3 -I - "$out" "$tool" "$@" <<'PY'
import struct, sys
out, tool, pairs = sys.argv[1], sys.argv[2], sys.argv[3:]
lines = ['// PSGopher: ps5/shaders compiled by ps5/tools/make-shaders.sh (%s).' % tool,
         '// Generated: do not edit. SPDX-License-Identifier: GPL-3.0-or-later', '#pragma once', '']
for pair in pairs:
    name, spv = pair.split('=', 1)
    data = open(spv, 'rb').read()
    words = struct.unpack('<%dI' % (len(data) // 4), data)
    lines.append('static const uint32_t %s[] = {' % name)
    for i in range(0, len(words), 8):
        lines.append('    ' + ', '.join('0x%08x' % w for w in words[i:i + 8]) + ',')
    lines += ['};', '']
open(out, 'w').write('\n'.join(lines))
print('==> %s' % out)
PY
}
compile() { glslangValidator --quiet -V --target-env vulkan1.1 -o "$tmp/$2.spv" "$root/ps5/shaders/$1"; }
compile psgopher_hdr.frag hdr
header "$dir/psgopher_hdr_spirv.hpp" psgopher_hdr_fragment_spirv="$tmp/hdr.spv"
compile psgopher_overlay.vert overlay_vert
compile psgopher_overlay.frag overlay_frag
header "$dir/psgopher_overlay_spirv.hpp" psgopher_overlay_vertex_spirv="$tmp/overlay_vert.spv" \
    psgopher_overlay_fragment_spirv="$tmp/overlay_frag.spv"
