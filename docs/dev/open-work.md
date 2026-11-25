# Open work

What is unfinished, untested on a console, or known to be wrong, in one place. Update it in the
same commit as the change that adds or settles an item ([`workflow.md`](workflow.md#the-loop-for-a-change)).

## Known problems

| Problem | Evidence | Next step |
| --- | --- | --- |
| A demanding game runs below full speed at any upscale | one NTSC game at 82-84% at 2x with parallel-rdp at 1.7 ms of 20 ms a VI, shading 0% general ([`performance.md`](performance.md#first-console-observations)): Gopher64's interpreters take the time | the next log's `RSP` share against the rest (the CPU's interpreter), with the fat-LTO core; then PGO of the core (Crux2's method) |
| The front end's remaining defects | [`../frontend-audit/BUGS.md`](../frontend-audit/BUGS.md) (FE-001 to FE-004 and FE-031 fixed, see `CHANGELOG.md`) | in the order of [`../frontend-audit/ROADMAP.md`](../frontend-audit/ROADMAP.md), FE-005 next |
| The USB search uses its whole budget (60,000 entries) at every start | boot logs 2026-11-25 (5.7-34.7 s before the share is searched) | measure which folders take it; skip known non-game folders or search places in parallel |
| The first search of a large share is slow (1,424 ROMs: 55.6 s, headers read over SMB) | boot log 2026-11-25 | later starts read `library.tsv`; measure again before changing anything |
| The boot log does not name the game's ID when it starts | boot log 2026-11-25 | log the header's game code with `starting Gopher64` (needed for [`compatibility.md`](compatibility.md)) |

## Not yet run on a console

| What | Since | How it will show |
| --- | --- | --- |
| The cover fixes (pool size, upload checks, RACK, after a game) | `93a4309` (no crash in one session) | covers in HOME after RACK and after a game; no SIGABRT; `about:` lines |
| The two-controller tap | `93a4309` | hold the touch pad with a second controller connected: no quick menu |
| A failed listing keeping the list | `3e13b39` | a share that times out: the list keeps its games (the share woke at once in the run since) |
| Descriptions of the N64's years only | this commit | the 1997 game's description for its cartridge |
| Shader compiles on 4 threads or at once; the driver's own thread | this commit | the `video:` line's shading share and speed at 8x with each |
| The configurable hotkeys and their new actions, the quick menu tap switch | this commit | each action in a game with the activator held; `hotkeys:` line at a game's start |
| Binding a profile's input by pressing it, the Transfer Pak's file picker, port controller assignment | this commit | Profiles and Controllers in BENCH, then a game |
| Cheats applied to a game | `33c8907` | a cheat on, the game started from the board |
| The stats overlay | `33c8907` | the quick menu's Stats overlay in a game (the `video:` and `rdp:` lines have run) |
| The quick menu's reset, and loading a probe from it | `d7010d1` | the quick menu in a game (resume, eject, taking a probe and starting from one on the board have run) |
| 119.88 Hz, variable refresh, HDR10 (off by default) | before the front end | BENCH settings, the boot log's `display:` line |
| The console probe (`probe.txt`) and the benchmark mode (`benchmark.txt`) | the first builds | `probe:` and `bench:` lines ([`performance.md`](performance.md)) |
| parallel-rdp's conformance suites on RADV | — | needs the tool built as a test title ([`compatibility.md`](compatibility.md)) |

## Unfinished

- What Gopher64's desktop GUI sets with file pickers or editors has no place in the front end yet: the
  Transfer Pak's Game Boy ROM and save, input profiles, RetroAchievements and netplay. They stay in
  `config.json` and `retroachievements.json` as Gopher64 keeps them (netplay and RetroAchievements
  also need Tokio's I/O driver: [`architecture.md`](architecture.md) D4, D10).
- The benchmark baseline and the per-game compatibility record ([`performance.md`](performance.md),
  [`compatibility.md`](compatibility.md)).
- Release packaging: notices for every Rust crate and C library linked (OGBox's `rust-licences.py`
  approach), the ZIP and `SHA256SUMS`. Nothing is released; no release without the owner asking.
- A host-side benchmark of the core (emulated cycles a second on the PC for a ROM) to compare builds
  without a console.

## Decisions on record

- Covers come from libretro-thumbnails on GitHub, not thumbnails.libretro.com (404 for every N64 name,
  2026-11-25). Descriptions are Wikipedia's summaries, credited on the panel (CC BY-SA).
- The core's network code is blocking `std::net` with rustls; Tokio's sockets and reqwest are not used
  on the console (D4, D18).
- `CLAUDE.md` stays local (gitignored); the agent guidance that is shared is
  [`workflow.md`](workflow.md).
