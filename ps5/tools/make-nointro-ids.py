#!/usr/bin/env python3
# PSGopher - the N64 game IDs and their No-Intro names, for covers and descriptions looked up by the
# cartridge's ID rather than the ROM file's name.
#
#   make-nointro-ids.py OUT.tsv [DB.xml]
#
# Reads No-Intro's N64 database export that Gopher64 carries (vendor/gopher64/data/ui/*.xml) and
# writes one line per (game ID, No-Intro name): "ID<TAB>retail<TAB>name", retail 1 for a release
# of the "Games" category with no Beta, Proto, Demo, Kiosk, Sample or Debug tag, else 0. The title
# (library: nointro.cpp) picks the name for a cartridge by its ID and revision. libretro's box art
# is named by these names.
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
import glob
import html
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
NOT_RETAIL = re.compile(r"\((Beta|Proto|Demo|Kiosk|Sample|Debug)[^)]*\)", re.I)


def main():
    if len(sys.argv) < 2:
        print(__doc__ or "usage: make-nointro-ids.py OUT.tsv [DB.xml]", file=sys.stderr)
        return 2
    out = sys.argv[1]
    db = sys.argv[2] if len(sys.argv) > 2 else (sorted(glob.glob(os.path.join(ROOT, "vendor/gopher64/data/ui/*.xml"))) or [""])[-1]
    if not db or not os.path.exists(db):
        print("make-nointro-ids.py: no No-Intro database (vendor/gopher64/data/ui/*.xml)", file=sys.stderr)
        return 1
    # (the export has two top-level elements, so it is read by its patterns, not as one XML document)
    text = open(db, encoding="utf-8").read()
    rows = set()
    for name, body in re.findall(r'<game name="([^"]*)">(.*?)</game>', text, re.S):
        name = html.unescape(name)
        if "\t" in name or "\n" in name:
            continue
        categories = re.search(r'categories="([^"]*)"', body)
        retail = (categories is None or categories.group(1) == "Games") and not NOT_RETAIL.search(name)
        for serial in set(re.findall(r'<file [^>]*serial="([A-Z0-9]{4})"', body)):
            rows.add((serial, 1 if retail else 0, name))
    with open(out + ".new", "w", encoding="utf-8") as f:
        f.write("# PSGopher: N64 game IDs and No-Intro names, from %s (No-Intro, no-intro.org)\n" % os.path.basename(db))
        for serial, retail, name in sorted(rows):
            f.write("%s\t%d\t%s\n" % (serial, retail, name))
    os.replace(out + ".new", out)
    print("make-nointro-ids.py: %d names of %d IDs -> %s" % (len(rows), len({r[0] for r in rows}), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
