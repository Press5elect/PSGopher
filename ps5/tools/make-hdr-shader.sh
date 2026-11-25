#!/usr/bin/env bash
# PSGopher - ps5/shaders/psgopher_hdr.frag to SPIR-V, as the header parallel-rdp's interface includes
# (vendor/gopher64/parallel-rdp/psgopher_hdr_spirv.hpp, carried by ps5.patch). Run after changing the
# shader, then ps5/tools/fetch-gopher64.sh --export.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
src="$root/ps5/shaders/psgopher_hdr.frag"
out="$root/vendor/gopher64/parallel-rdp/psgopher_hdr_spirv.hpp"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
glslangValidator --quiet -V --target-env vulkan1.1 -S frag -o "$tmp/hdr.spv" "$src"
python3 -I - "$tmp/hdr.spv" "$out" "$(glslangValidator --version | head -1)" <<'PY'
import struct, sys
spv, out, tool = sys.argv[1], sys.argv[2], sys.argv[3]
words = struct.unpack('<%dI' % (len(open(spv, 'rb').read()) // 4), open(spv, 'rb').read())
lines = ['// PSGopher: ps5/shaders/psgopher_hdr.frag compiled by ps5/tools/make-hdr-shader.sh (%s).' % tool,
         '// Generated: do not edit. SPDX-License-Identifier: GPL-3.0-or-later', '#pragma once', '',
         'static const uint32_t psgopher_hdr_fragment_spirv[] = {']
for i in range(0, len(words), 8):
    lines.append('    ' + ', '.join('0x%08x' % w for w in words[i:i + 8]) + ',')
lines.append('};')
open(out, 'w').write('\n'.join(lines) + '\n')
print('==> %d words -> %s' % (len(words), out))
PY
