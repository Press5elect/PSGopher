#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
"""Recompile parallel-rdp shaders into its precompiled bank, shaders/slangmosh.hpp.

parallel-rdp ships its shaders as SPIR-V in slangmosh.hpp, written by Granite's slangmosh
(`slangmosh --strip -O --namespace RDP parallel-rdp/shaders/slangmosh.json --output ...`, from
generate_standalone_codebase.sh), which compiles with shaderc. PSGopher does not build Granite, so
this tool does the same job for the shaders PSGopher changes, with glslangValidator and spirv-opt
(`apt-get install glslang-tools spirv-tools`):

    glslangValidator -V --target-env vulkan1.1 -S <stage> -I<shaders> -D<VARIANT>=<n> ...
    spirv-opt --target-env=vulkan1.1 -O --strip-debug

It replaces only the named shaders' SPIR-V (every variant of each) and rewrites the offsets and sizes
in the constructor; every other shader's words stay exactly as slangmosh wrote them. The reflection
bank is not rewritten, so a replaced shader must keep its interface: the tool compares the
specialization constants, the bindings and the push-constant block's size of the old and the new
module and stops when they differ.

    ps5/tools/make-rdp-shaders.py                  # the shaders PSGopher's patch changes
    ps5/tools/make-rdp-shaders.py --shaders a,b    # others
    ps5/tools/make-rdp-shaders.py --dump DIR       # also leave every module's .spv in DIR

The same sources and the same tool versions give the same file; the versions are printed.
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHADERS = os.path.join(ROOT, "vendor/gopher64/parallel-rdp/parallel-rdp-standalone/parallel-rdp/shaders")
# The shaders that index the upscaled RDRAM (rdram_layout.h).
DEFAULT = "ubershader,depth_blend,extract_vram,update_upscaled_domain_pre,update_upscaled_domain_resolve"

ENTRY = re.compile(
    r"(?:if \(((?:[^{]|\n)*?)\)\s*\{\s*)?layout\.unserialize\(reflection_bank \+ (\d+), (\d+)\);\s*"
    r"this->(\w+)(\[\d+\])? = device\.request_(?:program|shader)\(spirv_bank \+ (\d+), (\d+)")


def run(cmd):
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        sys.exit("%s\n%s" % (" ".join(cmd), result.stdout))
    return result.stdout


def interface(words):
    """What the reflection bank describes: spec constants, bindings, the push-constant block's size."""
    spec, sets, bindings, offsets, push = set(), {}, {}, {}, set()
    types, pointers, sizes = {}, {}, {}
    i = 5
    while i < len(words):
        op, count = words[i] & 0xFFFF, words[i] >> 16
        w = words[i + 1:i + count]
        if op == 71:  # OpDecorate
            if w[1] == 1:
                spec.add(w[2])
            elif w[1] == 34:
                sets[w[0]] = w[2]
            elif w[1] == 33:
                bindings[w[0]] = w[2]
        elif op == 72 and w[2] == 35:  # OpMemberDecorate Offset
            offsets.setdefault(w[0], {})[w[1]] = w[3]
        elif op == 21 or op == 22:  # OpTypeInt, OpTypeFloat
            sizes[w[0]] = w[1] // 8
        elif op == 23:  # OpTypeVector
            sizes[w[0]] = sizes.get(w[1], 0) * w[2]
        elif op == 30:  # OpTypeStruct
            types[w[0]] = w[1:]
        elif op == 32:  # OpTypePointer
            pointers[w[0]] = (w[1], w[2])
        elif op == 59 and w[2] == 9:  # OpVariable, PushConstant
            struct_id = pointers[w[0]][1]
            members = types[struct_id]
            last = len(members) - 1
            push.add(offsets[struct_id][last] + sizes.get(members[last], 0))
        i += count
    resources = sorted((sets[v], bindings[v]) for v in sets if v in bindings)
    return sorted(spec), sorted(set(resources)), sorted(push)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dir", default=SHADERS, help="parallel-rdp's shaders folder")
    parser.add_argument("--shaders", default=DEFAULT, help="names from slangmosh.json, comma separated")
    parser.add_argument("--output", help="where to write (default: slangmosh.hpp in --dir)")
    parser.add_argument("--dump", help="a folder for each module's old and new .spv")
    args = parser.parse_args()

    header = os.path.join(args.dir, "slangmosh.hpp")
    text = open(header).read()
    desc = {s["name"]: s for s in json.load(open(os.path.join(args.dir, "slangmosh.json")))["shaders"]}
    wanted = [n for n in args.shaders.split(",") if n]
    for name in wanted:
        if name not in desc:
            sys.exit("no shader named %s in slangmosh.json" % name)

    begin = text.index("static const uint32_t spirv_bank[] =")
    end = text.index("};", begin) + 2
    words = [int(x, 16) for x in re.findall(r"0x([0-9a-f]{8})u", text[begin:end])]
    ctor = text.index("::Shaders(Device", end)
    entries = list(ENTRY.finditer(text, ctor))
    if not entries:
        sys.exit("no shaders found in %s" % header)

    print(run(["glslangValidator", "--version"]).splitlines()[0])
    print(run(["spirv-opt", "--version"]).splitlines()[0])

    bank, edits, rebuilt = [], [], 0
    with tempfile.TemporaryDirectory() as tmp:
        for m in entries:
            cond, name, index, offset, size = m.group(1), m.group(4), m.group(5) or "", int(m.group(6)), int(m.group(7))
            old = words[offset:offset + size // 4]
            new = old
            defines = re.findall(r'resolver\("\w+", "(\w+)"\) == (\d+)', cond or "")
            tag = name + index + "".join(".%s%s" % d for d in defines)
            if name in wanted:
                shader = desc[name]
                path = os.path.join(args.dir, shader["path"])
                stage = "comp" if shader.get("compute") else os.path.splitext(path)[1][1:]
                if not defines and shader.get("variants"):
                    # Variants without "resolve" are an array ([n]): the index counts through them.
                    number, defines = int(index.strip("[]") or 0), []
                    for variant in shader["variants"]:
                        defines.append((variant["define"], str(number % variant["count"])))
                        number //= variant["count"]
                raw, opt = os.path.join(tmp, "raw.spv"), os.path.join(tmp, "opt.spv")
                run(["glslangValidator", "--quiet", "-V", "--target-env", "vulkan1.1", "-S", stage,
                     "-I" + args.dir, "-P#extension GL_GOOGLE_include_directive : require"] +
                    ["-D%s=%s" % d for d in defines] + ["-o", raw, path])
                run(["spirv-opt", "--target-env=vulkan1.1", "-O", "--strip-debug", raw, "-o", opt])
                data = open(opt, "rb").read()
                new = list(struct.unpack("<%dI" % (len(data) // 4), data))
                if interface(new) != interface(old):
                    sys.exit("%s: the interface changed, and the reflection bank is not rewritten:\n  old %s\n  new %s"
                             % (tag, interface(old), interface(new)))
                rebuilt += 1
                print("%-48s %7d -> %7d bytes" % (tag, size, len(new) * 4))
            if args.dump:
                os.makedirs(args.dump, exist_ok=True)
                for suffix, module in ((".old.spv", old), (".spv", new)):
                    with open(os.path.join(args.dump, tag + suffix), "wb") as out:
                        out.write(struct.pack("<%dI" % len(module), *module))
            edits.append((m.start(6), m.end(7), "%d, %d" % (len(bank), len(new) * 4)))
            bank += new

    lines = ["static const uint32_t spirv_bank[] =", "{"]
    for i in range(0, len(bank), 8):
        lines.append("\t" + ", ".join("0x%08xu" % w for w in bank[i:i + 8]) + ",")
    lines.append("};")
    for start, stop, replacement in reversed(edits):
        text = text[:start] + replacement + text[stop:]
    text = text[:begin] + "\n".join(lines) + text[end:]
    with open(args.output or header, "w") as out:
        out.write(text)
    print("%s: %d of %d modules recompiled, %d words" % (args.output or header, rebuilt, len(entries), len(bank)))


if __name__ == "__main__":
    main()
