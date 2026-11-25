# Open work

What is unfinished, untested on a console, or known to be wrong, in one place. Update it in the
same commit as the change that adds or settles an item ([`workflow.md`](workflow.md#the-loop-for-a-change)).

## Known problems

| Problem | Evidence | Next step |
| --- | --- | --- |
| Some games run below full speed, and their sound is choppy | one NTSC game at 13-74%: of 14.5-23 ms of CPU a VI, parallel-rdp's 6-15 ms (single-threaded, as Gopher64 runs it for its RDRAM tracking); another at 100% most of the time; underruns follow the speed ([`performance.md`](performance.md#first-console-observations)) | the next log's `RDP y: draw a, sync b, frame c`, at 8x and at 1x; then the part that costs, measured. Candidates: time-stretched sound (Crux2's) for below full speed; PGO |
| The front end's remaining defects | [`../frontend-audit/BUGS.md`](../frontend-audit/BUGS.md) (FE-001 to FE-004 and FE-031 fixed, see `CHANGELOG.md`) | in the order of [`../frontend-audit/ROADMAP.md`](../frontend-audit/ROADMAP.md), FE-005 next |
| The USB search uses its whole budget (60,000 entries) at every start | boot logs 2026-11-25 (5.7-34.7 s before the share is searched) | measure which folders take it; skip known non-game folders or search places in parallel |
| The first search of a large share is slow (1,424 ROMs: 55.6 s, headers read over SMB) | boot log 2026-11-25 | later starts read `library.tsv`; measure again before changing anything |
| The boot log does not name the game's ID when it starts | boot log 2026-11-25 | log the header's game code with `starting Gopher64` (needed for [`compatibility.md`](compatibility.md)) |

## Not yet run on a console

| What | Since | How it will show |
| --- | --- | --- |
| The cover fixes (pool size, upload checks, RACK, after a game) | `93a4309` (no crash in one session) | covers in HOME after RACK and after a game; no SIGABRT; `about:` lines |
| The two-controller tap | `93a4309` | hold the touch pad with a second controller connected: no quick menu |
| Waking the shares, a failed listing keeping the list, the sound buffer setting | this commit | `vfs: share ... woken` and `audio: n VIs` lines; the list after a NAS timeout; underruns near full speed |
| Cheats applied to a game | `33c8907` | a cheat on, the game started from the board |
| The stats overlay | `33c8907` | the quick menu's Stats overlay in a game (the `video:` and `rdp:` lines have run) |
| The quick menu's probes (save and load a state) and reset | `d7010d1` | the quick menu in a game (resume and eject have run) |
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
