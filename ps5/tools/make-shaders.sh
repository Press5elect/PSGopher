#!/usr/bin/env bash
# PSGopher - ps5/shaders/*.vert|frag to SPIR-V, as the headers parallel-rdp's interface includes
# (vendor/gopher64/parallel-rdp/psgopher_*_spirv.hpp, carried by ps5.patch). Run after changing a
# shader, then ps5/tools/fetch-gopher64.sh --export.
#
#   psgopher_hdr.frag                          -> psgopher_hdr_spirv.hpp      (HDR10 packing, D14)
#   psgopher_overlay.vert, psgopher_overlay.frag -> psgopher_overlay_spirv.hpp (the quick menu)
#   psgopher_sharp.frag                        -> psgopher_sharp_spirv.hpp    (Final Filter: Sharp Bilinear)
#   psgopher_resample.frag (twice, -DLANCZOS)  -> psgopher_resample_spirv.hpp (Final Filter: Bicubic, Lanczos)
#   psgopher_fxaa.frag, psgopher_cas.frag      -> psgopher_post_spirv.hpp     (Post Anti-Aliasing: FXAA; Sharpening: CAS)
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
dir=${PSGOPHER_SHADER_OUT:-$root/vendor/gopher64/parallel-rdp} # (the host tests make them elsewhere, to compare)
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
compile() { # source, name, then glslangValidator's own options (-D...); each is validated as the host tests do
    local source=$1 name=$2
    shift 2
    glslangValidator --quiet -V --target-env vulkan1.1 "$@" -o "$tmp/$name.spv" "$root/ps5/shaders/$source"
    spirv-val --target-env vulkan1.1 "$tmp/$name.spv"
}
compile psgopher_hdr.frag hdr
header "$dir/psgopher_hdr_spirv.hpp" psgopher_hdr_fragment_spirv="$tmp/hdr.spv"
compile psgopher_overlay.vert overlay_vert
compile psgopher_overlay.frag overlay_frag
header "$dir/psgopher_overlay_spirv.hpp" psgopher_overlay_vertex_spirv="$tmp/overlay_vert.spv" \
    psgopher_overlay_fragment_spirv="$tmp/overlay_frag.spv"
compile psgopher_sharp.frag sharp
header "$dir/psgopher_sharp_spirv.hpp" psgopher_sharp_fragment_spirv="$tmp/sharp.spv"
compile psgopher_resample.frag bicubic
compile psgopher_resample.frag lanczos -DLANCZOS=1
header "$dir/psgopher_resample_spirv.hpp" psgopher_bicubic_fragment_spirv="$tmp/bicubic.spv" \
    psgopher_lanczos_fragment_spirv="$tmp/lanczos.spv"
compile psgopher_fxaa.frag fxaa
compile psgopher_cas.frag cas
header "$dir/psgopher_post_spirv.hpp" psgopher_fxaa_fragment_spirv="$tmp/fxaa.spv" psgopher_cas_fragment_spirv="$tmp/cas.spv"
