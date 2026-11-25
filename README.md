# PSGopher

<img width="3840" height="2160" alt="PSGopher_20261125131517" src="https://github.com/user-attachments/assets/43609781-7e96-41b3-b453-aafb4a6957d9" />

A native PlayStation 5 port of [Gopher64](https://github.com/gopher64/gopher64), the Nintendo 64
emulator, for jailbroken consoles running homebrew. Gopher64's emulation is used unchanged:
its VR4300 and RSP interpreters and parallel-rdp, the Vulkan compute RDP, running on RADV
linked into the title.

**Status: in development. Nothing has run on a console yet.** The title builds, links and is
signed, and its game list runs on the PC (host tests); the first console runs are next. No release
exists.

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

Start the title: it shows a list of every N64 ROM it finds (`.z64`, `.n64`, `.v64`, and a `.zip` or
`.7z` of one), dumped from your own cartridges, on the console's storage (anywhere in `/data`, a few
folders deep; `/data/psgopher/roms` is a good place), on USB drives and extended storage, and on
network shares. **L1/R1** change the tab (Internal, USB, Network), **Cross** plays, **Triangle**
searches again, **OPTIONS** opens the settings.

In a game the controls are Gopher64's default gamepad layout on the DualSense: Cross A, Square B, L2
Z, Options Start, the right stick the C buttons; the touch pad click is Gopher64's hotkey (with
Options: back to the list; with L1/R1: save/load state; with L2: fast forward). Saves and states are
in `/data/psgopher/data/`.

**Network shares**: on the Network tab, **Square** adds and edits SMB and NFS shares (an on-screen
keyboard; "Find SMB servers" looks for them on your network). They are mounted at each start and
searched for games; a ROM on a share is copied to the console before it plays.

**USB drives** are outside a title's sandbox: as OGBox and Crux2 do, PSGopher leaves it at the start
through the payload loader on port 9021 (elfldr, which kstuff setups run). Settings > Storage turns
that off.

**Settings** are kept as you change them: Gopher64's own (resolution, supersampling, widescreen,
CRT, overclock, Expansion Pak, rewind, controllers...) in its `config.json`, PSGopher's (refresh: 59.94
Hz, 119.88 Hz or variable refresh; HDR10 and its brightness) in `config/psgopher.ini`. Variable
refresh and HDR10 are off by default and untested on a console yet.

`/data/psgopher/autostart.txt` (a ROM's path or a name in `roms/`) still boots that ROM first.

No games, BIOS or other copyrighted data are included.

## Licence and credits

GPL-3.0 (see `LICENSE`), as Gopher64. Gopher64 is by Logan McNaughton and its contributors;
the gopher logo is Gopher64's (`data/icon` in its repository, GPL-3.0), used for PSGopher's icon;
parallel-rdp by Hans-Kristian Arntzen (MIT); rcheevos by the RetroAchievements team (MIT);
stb_truetype by Sean Barrett (public domain/MIT). The network shares and the sandbox exit are
OGBox by Press5elect, the virtual file layer Crux2 by Press5elect (GPL-3.0 with the attribution term
of section 7(b): `NOTICE`); Dear ImGui by Omar Cornut (MIT) with Roboto (Apache-2.0); libsmb2 and
libnfs by Ronnie Sahlberg (LGPL-2.1); the sandbox helper from BlackBearReloaded's
ps5-native-app-boilerplate (GPL-3.0). The PS5 layer in `ps5/host/platform*` is from
Mihawk's PS5 Vulkan Template (MIT); RADV is Mesa's (MIT) through Mihawk's PS5_Mesa and
PS5_Vulkan; the payload SDK and platform layer are Mihawk's PS5_PayloadSDK.
