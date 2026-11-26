#!/usr/bin/env python3
# PSGopher - pack the built title folder as the release ZIP the in-title updater reads.
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attributions "OGBox by Press5elect" and
# "Crux2 by Press5elect"; see NOTICE.
#
# PSGopher: from OGBox db63d46 (ps5/tools/make-release-zip.py, the title folder's ZIP only).
#
#   ps5/tools/make-release-zip.py vX.Y.Z [--date YYYY-MM-DD] [--out DIR] [--app DIR]
#
# Writes into DIR (default dist/release-<tag>):
#
#   PSGopher-<tag>.zip          dist/PPSA99280/ as one archive: every entry below the folder
#                               "PPSA99280/" (folders included), deflate, Unix mode 777 (the console
#                               starts a title only when its files are open to every user), every
#                               timestamp the date's, so the same folder gives the same file. Tested,
#                               and unpacked into a scratch folder to see eboot.bin and
#                               sce_sys/param.json come out as they went in.
#   PSGopher-<tag>.zip.sha256   "<digest>  PSGopher-<tag>.zip", the file the updater reads beside it
#   SHA256SUMS                  the same line (GitHub also computes its own digest of an upload)
#
# The contract with the updater (ps5/host/update.h): the name "PSGopher-vX.Y.Z.zip" with the tag
# in it, one top folder named after the title, nothing but plain files and folders in it.
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Pack the title folder as the release ZIP.")
    parser.add_argument("tag", help="the release's tag, vX.Y.Z")
    parser.add_argument("--date", help="the timestamp of every entry (default: today, UTC)")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--app", type=Path, help="the built title folder (default dist/<title id>)")
    args = parser.parse_args()
    parts = args.tag[1:].split(".") if args.tag.startswith("v") else []
    if len(parts) != 3 or not all(part.isdigit() for part in parts):
        print(f"the tag {args.tag!r} is not vX.Y.Z", file=sys.stderr)
        return 2

    title_id = json.loads((ROOT / "ps5/sce_sys/param.json").read_text())["titleId"]
    app = args.app or ROOT / "dist" / title_id
    for needed in ("eboot.bin", "sce_sys/param.json"):
        if not (app / needed).is_file():
            print(f"no {app / needed}: run ps5/tools/build.sh first", file=sys.stderr)
            return 2
    date = args.date or subprocess.run(["date", "-u", "+%Y-%m-%d"], capture_output=True, text=True).stdout.strip()
    year, month, day = (int(part) for part in date.split("-"))
    stamp = (year, month, day, 0, 0, 0)
    out = args.out or ROOT / "dist" / f"release-{args.tag}"
    out.mkdir(parents=True, exist_ok=True)
    name = f"PSGopher-{args.tag}.zip"
    target = out / name

    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in [app] + sorted(app.rglob("*"), key=lambda p: p.relative_to(app).as_posix()):
            relative = path.relative_to(app).as_posix()
            entry = f"{title_id}/{relative}" if relative != "." else title_id
            if path.is_symlink():
                print(f"{path}: a link is not packed", file=sys.stderr)
                return 1
            if path.is_dir():
                info = zipfile.ZipInfo(entry.rstrip("/") + "/", date_time=stamp)
                info.external_attr = (0o40777 << 16) | 0x10
                info.create_system = 3
                archive.writestr(info, b"")
                continue
            info = zipfile.ZipInfo(entry, date_time=stamp)
            info.external_attr = 0o100777 << 16
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            with path.open("rb") as source, archive.open(info, "w", force_zip64=False) as sink:
                shutil.copyfileobj(source, sink, 1 << 20)

    with zipfile.ZipFile(target) as archive:
        bad = archive.testzip()
        if bad:
            print(f"{name}: damaged entry {bad}", file=sys.stderr)
            return 1
        if any(not entry.startswith(title_id + "/") for entry in archive.namelist()):
            print(f"{name}: an entry outside {title_id}/", file=sys.stderr)
            return 1
        with tempfile.TemporaryDirectory() as scratch:
            archive.extractall(scratch)
            for needed in ("eboot.bin", "sce_sys/param.json"):
                if (Path(scratch) / title_id / needed).read_bytes() != (app / needed).read_bytes():
                    print(f"{name}: {needed} does not come out as it went in", file=sys.stderr)
                    return 1

    line = f"{sha256(target)}  {name}\n"
    (out / f"{name}.sha256").write_text(line)
    (out / "SHA256SUMS").write_text(line)
    print(f"{target} ({target.stat().st_size} bytes)\n{line}", end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
