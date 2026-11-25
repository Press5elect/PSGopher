# PSGopher

A native PlayStation 5 port of [Gopher64](https://github.com/gopher64/gopher64), the Nintendo 64
emulator, for jailbroken consoles running homebrew. Gopher64's emulation is used unchanged:
its VR4300 and RSP interpreters and parallel-rdp, the Vulkan compute RDP, running on RADV
linked into the title.

**Status: in development, no release yet.** The title runs on the console (2026-11-25): the board,
USB drives, network shares and games, which do not reach full speed yet. What has and has not run
on a console is in [`CHANGELOG.md`](CHANGELOG.md).

This page is the user's guide. Developers: the documentation index is [`docs/README.md`](docs/README.md).

## Use

Start the title: its home screen is a live N64 mainboard, the cartridge you choose seated in its
slot, its facts beside it as Gopher64 reads them (game ID, lockout chip, save memory, pak), your
play time and your probes (save states). It finds every N64 ROM (`.z64`, `.n64`, `.v64`, and a
`.zip` or `.7z` of one) you dumped from your own cartridges, on the console's storage (anywhere in
`/data`, a few folders deep; `/data/psgopher/roms` is a good place), on USB drives and extended
storage, and on network shares.

- **HOME**: the board and the cartridge strip (recently played first). ◄ ► cartridge, ✕ power on,
  □ power on from a probe, △ cheats. Each cartridge shows its box art (libretro's thumbnails) and
  its region's flag, the panel three lines of its Wikipedia summary, fetched once over the network
  and kept in `/data/psgopher/cache/covers`.
- **RACK**: every cartridge on shelves, A to Z. D-pad, L2/R2 by letter, □ sort (A-Z, recently
  played, most played, size), OPTIONS show (all, played, never played, has probes, internal, USB,
  network), ✕ power on, △ seat it on the home strip.
- **BENCH**: the board's parts are the settings, Gopher64's own (the crystal: overclock; the video
  stage: resolution, supersampling; AV out: CRT look, widescreen, integer scaling; the expansion
  slot; the ports; rewind and the USB link) and PSGopher's (refresh: 59.94 Hz, 119.88 Hz or
  variable refresh; HDR10 and its brightness; in AV out, the sound buffer: 33 to 133 ms, 67 by default), and "This console": network shares, the sandbox,
  searching again, closing. ▲ ▼ part, ◄ ► or ✕ change.

L1/R1 change the view. **In a game**, tap the touch pad for the quick menu: resume, take a probe
(save state), load one, turn the stats overlay on or off (speed, frame times, audio), reset, or
eject the cartridge (back to the board). The controls are
Gopher64's default gamepad layout on the DualSense: Cross A, Square B, L2 Z, Options Start, the
right stick the C buttons; the touch pad held down is Gopher64's hotkey (with Options: back to the
board; with L1/R1: save/load state; with L2: fast forward). Saves and states are in
`/data/psgopher/data/`.

**Network shares**: BENCH, This console, Network shares adds and edits SMB and NFS shares (an
on-screen keyboard; "Find SMB servers" looks for them on your network). They are mounted at each start and
searched for games; a ROM on a share is copied to the console before it plays.

**USB drives** are outside a title's sandbox: as OGBox and Crux2 do, PSGopher leaves it at the start
through the payload loader on port 9021 (elfldr, which kstuff setups run). BENCH, This console
turns that off.

**Settings** are kept as you change them: Gopher64's own in its `config.json`, PSGopher's in
`config/psgopher.ini`. Variable refresh and HDR10 are off by default and untested on a console yet.

`/data/psgopher/autostart.txt` (a ROM's path or a name in `roms/`) still boots that ROM first.

**The log**: `psgopher-boot.log` in the title folder (`/data/homebrew/PPSA99280/` when installed
there; `/data/psgopher/` when the title folder takes no file), with the run before kept as
`psgopher-boot.previous.log`, as OGBox and Crux2 keep theirs. Gopher64's own messages are in it too.
When a game does not start, the board says why at the top.

No games, BIOS or other copyrighted data are included.

## Licence and credits

GPL-3.0 (see `LICENSE`), as Gopher64. Gopher64 is by Logan McNaughton and its contributors;
the gopher logo is Gopher64's (`data/icon` in its repository, GPL-3.0), used for PSGopher's icon;
parallel-rdp by Hans-Kristian Arntzen (MIT); rcheevos by the RetroAchievements team (MIT);
stb_truetype and stb_image by Sean Barrett (public domain/MIT); rustls and webpki-roots
(Apache-2.0/MIT/ISC, webpki-roots' certificates from Mozilla's, MPL-2.0); covers from the
libretro-thumbnails project and descriptions from Wikipedia (CC BY-SA), fetched on the console,
none shipped; the front end's faces, Bricolage
Grotesque and JetBrains Mono (SIL Open Font License). The network shares and the sandbox exit are
OGBox by Press5elect, the virtual file layer Crux2 by Press5elect (GPL-3.0 with the attribution term
of section 7(b): `NOTICE`); Dear ImGui by Omar Cornut (MIT); libsmb2 and
libnfs by Ronnie Sahlberg (LGPL-2.1); the sandbox helper from BlackBearReloaded's
ps5-native-app-boilerplate (GPL-3.0). The PS5 layer in `ps5/host/platform*` is from
Mihawk's PS5 Vulkan Template (MIT); RADV is Mesa's (MIT) through Mihawk's PS5_Mesa and
PS5_Vulkan; the payload SDK and platform layer are Mihawk's PS5_PayloadSDK.
