# Open work

What is unfinished, untested on a console, or known to be wrong, in one place. Update it in the
same commit as the change that adds or settles an item ([`workflow.md`](workflow.md#the-loop-for-a-change)).

## Known problems

| Problem | Evidence | Next step |
| --- | --- | --- |
| Games run below full speed and the sound is choppy | a PAL game at 51-85% of 50 VIs a second ([`performance.md`](performance.md#first-console-observations)) | a boot log from `33c8907` or later with a game played a few minutes (and the stats overlay on): its `video:` lines say whether CPU, present wait or RDRAM syncs take the time; no change before that |
| The front end's verified defects | [`../frontend-audit/BUGS.md`](../frontend-audit/BUGS.md) | in the order of [`../frontend-audit/ROADMAP.md`](../frontend-audit/ROADMAP.md) |
| The first search of a large share is slow (1,424 ROMs: 55.6 s, headers read over SMB) | boot log 2026-11-25 | later starts read `library.tsv`; measure again before changing anything |
| The boot log does not name the game's ID when it starts | boot log 2026-11-25 | log the header's game code with `starting Gopher64` (needed for [`compatibility.md`](compatibility.md)) |

## Not yet run on a console

| What | Since | How it will show |
| --- | --- | --- |
| Covers and descriptions (rustls over `std::net`, webpki-roots) | `33c8907` | `covers:` and `about:` lines in the boot log; pictures on the board |
| Cheats applied to a game | `33c8907` | a cheat on, the game started from the board |
| The stats overlay, the fuller `video:` line, the `rdp:` RDRAM path line | `33c8907` | the boot log and the overlay in a game |
| The quick menu's probes (save and load a state) and reset | `d7010d1` | the quick menu in a game (resume and eject have run) |
| 119.88 Hz, variable refresh, HDR10 (off by default) | before the front end | BENCH settings, the boot log's `display:` line |
| The console probe (`probe.txt`) and the benchmark mode (`benchmark.txt`) | the first builds | `probe:` and `bench:` lines ([`performance.md`](performance.md)) |
| parallel-rdp's conformance suites on RADV | — | needs the tool built as a test title ([`compatibility.md`](compatibility.md)) |
| `std::net` and rustls at all (the first network use from Rust) | `33c8907` | as the covers above |

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
