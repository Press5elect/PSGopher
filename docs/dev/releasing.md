# Releases and the in-title updater

What a release of PSGopher is, and what the updater (`ps5/host/update.cpp`) expects of it. Nothing
here is published without the owner's word, each time ([`workflow.md`](workflow.md), rule 4).

## The release ZIP (the contract)

- Repository `Press5elect/PSGopher`, tag `vX.Y.Z` (semantic, three numbers). Pre-releases are
  offered as well; drafts never.
- One asset named **`PSGopher-vX.Y.Z.zip`** (the tag in the name), made by
  `ps5/tools/make-release-zip.py vX.Y.Z` from `dist/PPSA99280/` after a release build:
  every entry below the folder **`PPSA99280/`**, folders included, deflate, plain files and folders
  only (no links), Unix mode 777, no encryption. It must hold `PPSA99280/eboot.bin` and
  `PPSA99280/sce_sys/param.json` with `"titleId": "PPSA99280"`.
- Beside it, any of: GitHub's own SHA-256 digest of the asset (automatic), `PSGopher-vX.Y.Z.zip.sha256`
  and `SHA256SUMS` (the script writes both). Each one present must match the download.
- A release build is `PSGOPHER_VERSION=vX.Y.Z ps5/tools/build.sh`: the version is compiled into the
  title (`-DPSGOPHER_VERSION`), and the updater compares it with the releases.

## What an update replaces

Only the program's own files (`IsManaged` in `update.cpp`): `eboot.bin`, `sandbox-elevator.elf`,
`write-broker.elf`, `NOTICE.txt`, `README.md`, `CHANGELOG.md` (and `.txt` / `LICENSE.txt` forms),
and the folders `sce_sys/`, `sce_module/`, `art/`, `licenses/`, `gamedata/` (from v0.0.2). Everything else in the ZIP is passed
over, and a file the release does not have is left where it is. The user's files (settings, games,
saves, states, covers, caches: all in `/data/psgopher`; the boot logs and anything else in the title
folder) are never in the set. A file added to the title folder by `link-title.sh` that is to be
updated must be added to the lists in `update.cpp` (and the test's `IsManaged` checks).

## A developer build

A build made without `PSGOPHER_VERSION` has no version: the updater names the newest release
(`Status::developer_build`) and offers nothing, so a developer build never "updates" over itself.
To test an update on a console, put a file `/data/psgopher/update-developer.txt` in place: the build
then counts as older than every release and is offered the newest.

## Steps, files, recovery

`Check()` asks the releases API (one request, no personal data); `Download()` fetches the ZIP into
`<title folder>/.update/`, checks size and SHA-256, examines every entry (a name that leaves the
folder, a link, an encrypted or special entry, a duplicate name, too many or too large: refused)
and unpacks the program's files into `.update/stage/`; `Install()` renames the old files into
`.update/backup/` and the new ones in, with a journal that is safe on exFAT, and the title closes.
`Recover()` at the next start (called from `main.cpp` once the title folder is known) puts the old
files back after an interrupted install. After an install the replaced files are kept in
`.previous/` (one version) for `RollBack()`.

HTTPS, the ZIP and the JSON add no library: `vendor/gopher64/src/psgopher_http.rs` (rustls, IPv4,
redirects to HTTPS only, at most 5), `psgopher_zip.rs` (the `zip` crate) and `update_json.h`.
Tests: `ps5/tests/test_update.cpp` (in `run-host-tests.sh`).
