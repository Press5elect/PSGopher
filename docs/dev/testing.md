# Testing PSGopher

What can be checked without a console, and how. Building comes first ([`building.md`](building.md));
what only the console shows is in [`debugging.md`](debugging.md) and [`open-work.md`](open-work.md).

## The host tests

```bash
CC=gcc CXX=g++ ps5/tests/run-host-tests.sh
```

Needs a build first (`build/sdl3-include` from `build-core.sh`, `.deps`, `vendor/gopher64`, the font
headers). Everything runs under AddressSanitizer and UBSan. The runner writes `build/host-tests/`:
**never run two at once**; an agent that needs its own run copies the commands into a scratch folder.

| Test | What it checks |
| --- | --- |
| `test_sdl3` | `ps5/sdl3` against SDL's headers with the PS5 layer's host build: audio accounting and resampling (amplitude, spurious tones, images, frequency ratio, underruns), window and controller answers, events, timers, main-thread calls, text rendering |
| `test_library` | the library's search (which files are games, depth, the title's own folders skipped, each file once, sorting, a share's listing and copy, the kept list), `psgopher.ini` (defaults, round trip, values in range), the shares file (accounts encoded, mount names) |
| `test_menu` | the front end itself, drawn offscreen through the PC's Vulkan (lavapipe: `PSGOPHER_LAVAPIPE_ICD`, default `/opt/lavapipe/lvp_icd.json`) with Dear ImGui's Vulkan backend as on the console, a scripted pad through HOME, RACK, BENCH, the probes, the shares and the keyboard. The core's cheats, covers and descriptions are stood in for (the cover through the real fetch thread, decoder and texture upload). Checks what the screens did and leaves pictures in `build/host-tests/menu-pictures/` |
| `test_quickmenu` | the in-game quick menu over a stand-in frame (a small software rasterizer in place of the interface's overlay): each answer (resume, save and load a probe, reset, quit), one step per press, the stats overlay; pictures in `menu-pictures/quickmenu/` |
| `test_rust_abi` | `ps5/host/rust_abi.c`: its FreeBSD 12 structures against Rust's libc's own offsets (`ps5/tests/rust_abi_offsets`, built for `x86_64-unknown-freebsd`) and the converters against FreeBSD 11 answers |

## Looking at the interface

The pictures are the review of any interface change: open the ones the change touches
(`1-home.png`, `3-rack.png`, `5-bench.png`, `13-home-probes.png`, the quick menu's) and compare with
the design, `ps5/art/live-board.html` (open it in a browser). A new screen or state gets a step in
`test_menu.cpp` that draws it.

## The renderer's accuracy

parallel-rdp's conformance suites (`rdp-conformance`, `vi-conformance`) compare the GPU's RDRAM and
TMEM with angrylion-rdp-plus's byte for byte; results and how they were built are in
[`compatibility.md`](compatibility.md#renderer-accuracy-parallel-rdps-conformance-suites).

## The frontend audit

[`../frontend-audit/`](../frontend-audit/MASTER_PLAN.md) holds the audit of the front end: the
inventory, the verified bugs, the coverage of what was inspected and the remediation order. A fix
for one of its bugs names the bug's ID in the commit and updates its status there.
