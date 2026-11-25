# PSGopher: compatibility

Games are named only by their cartridge ID (the ROM header's game code at 0x3B) and region.
No game has run on PSGopher yet.

## Principle

Compatibility is upstream Gopher64's: PSGopher does not change the emulated machine (D1). A game
that works in Gopher64 at the pin and fails on the PS5 is a PSGopher bug until shown otherwise; it
is first checked on the desktop build of the same pin.

## Benchmark and compatibility set

Thirteen commercial games chosen for their workloads (the list is the owner's and stays out of
the repository: games are named only by the ID read from each ROM header when it is tested), and
freely licensed test ROMs for accuracy.

| Slot | Workload it stands for | ID |
| --- | --- | --- |
| G1 | baseline, light | from the ROM header |
| G2 | mixed, large world | |
| G3 | Expansion Pak, framebuffer effects | |
| G4 | CPU-heavy, TLB | |
| G5 | CPU+RSP heavy, Expansion Pak, framebuffer reads | |
| G6 | RSP-heavy audio, large ROM | |
| G7 | RDP-heavy | |
| G8 | 60 fps game | |
| G9 | RDP/texture-heavy | |
| G10 | custom microcode, Expansion Pak | |
| G11 | large ROM, video playback, Expansion Pak | |
| G12 | framebuffer effects (2D/3D mix) | |
| G13 | mixed, multiplayer split screen | |
| T | n64-systemtest, krom's and Peter Lemon's CPU/RSP/RDP tests | accuracy |

No ROM is in this repository or will be; I test with my own dumps.

## Renderer accuracy: parallel-rdp's conformance suites

parallel-rdp's own tests (`rdp-conformance`, `vi-conformance` from gopher64/parallel-rdp at `748144e`,
the commit PSGopher's renderer was generated from), built with the precompiled shaders PSGopher ships
(`slangmosh.hpp`, byte-identical to the build's), compare the GPU's RDRAM and TMEM with
angrylion-rdp-plus's, byte for byte.

| Where | Driver | Result |
| --- | --- | --- |
| PC, 2026-11-25 [here] | lavapipe (Mesa 26.0.8, llvmpipe, Vulkan 1.4.335) | **257 of 257 passed** (ctest, 24 min on 4 cores) |
| PS5 | RADV, Crux2's (PS5 Mesa `1ba9874` + Crux2's patches) | not run yet: needs the tool built as a test title |

The PC run shows the shader binaries and the renderer at the pin are correct on a conformant
driver; it says nothing about RADV on the console, which is what the console run is for.

## Per-game record

| ID | Boots | Plays | Saves | States | Full speed | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| (none yet) | | | | | | |

## Checks per release

- Each set game: boot to gameplay, 10 minutes played, save in game, quit, boot, the save is there.
- Savestate save and load in one session and across sessions.
- 30 minutes of one game for audio drift (no underruns in the counters) and frame pacing.
- Controller unplugged and replugged (a user signing out and in) during play.
