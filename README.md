# PSGopher

A native PlayStation 5 port of [Gopher64](https://github.com/gopher64/gopher64), the Nintendo 64
emulator, for jailbroken consoles running homebrew. Gopher64's emulation is used unchanged:
its VR4300 and RSP interpreters and parallel-rdp, the Vulkan compute RDP, running on RADV
linked into the title.

**Status: in development. Nothing has run on a console yet.** The title builds, links and is
signed; the first console runs are next. No release exists.

| Document | What |
| --- | --- |
| [PS5_PORTING_ANALYSIS.md](PS5_PORTING_ANALYSIS.md) | The assessment: what Gopher64 is, what the console offers, the open questions |
| [PS5_ARCHITECTURE.md](PS5_ARCHITECTURE.md) | How the port is built, the decisions (D1-D11), the milestones |
| [PS5_DEPENDENCY_AUDIT.md](PS5_DEPENDENCY_AUDIT.md) | Every dependency and what it needs from the platform |
| [PS5_GRAPHICS_BACKEND.md](PS5_GRAPHICS_BACKEND.md) | parallel-rdp on RADV |
| [PS5_PERFORMANCE.md](PS5_PERFORMANCE.md) | Method, instrumentation, measurements |
| [PS5_COMPATIBILITY.md](PS5_COMPATIBILITY.md) | Games tested (by ID) |
| [PS5_BUILD_GUIDE.md](PS5_BUILD_GUIDE.md) | Building and running |
| [PS5_CHANGELOG.md](PS5_CHANGELOG.md) | Changes |

## Use

Put ROMs you dumped from your own cartridges in `/data/psgopher/roms/` and start the title; the
first ROM by name boots, or the one named in `/data/psgopher/autostart.txt`. Saves and states are
in `/data/psgopher/data/`. Controls are Gopher64's default gamepad layout on the DualSense: Cross
A, Square B, L2 Z, Options Start, the right stick the C buttons; the touch pad click is Gopher64's
hotkey (with Options: quit; with L1/R1: save/load state; with L2: fast forward).

No games, BIOS or other copyrighted data are included.

## Licence and credits

GPL-3.0 (see `LICENSE`), as Gopher64. Gopher64 is by Logan McNaughton and its contributors;
the gopher logo is Gopher64's (`data/icon` in its repository, GPL-3.0), used for PSGopher's icon;
parallel-rdp by Hans-Kristian Arntzen (MIT); rcheevos by the RetroAchievements team (MIT);
stb_truetype by Sean Barrett (public domain/MIT). The PS5 layer in `ps5/host/platform*` is from
Mihawk's PS5 Vulkan Template (MIT); RADV is Mesa's (MIT) through Mihawk's PS5_Mesa and
PS5_Vulkan; the payload SDK and platform layer are Mihawk's PS5_PayloadSDK.
