# PSGopher
<img width="3840" height="2160" alt="PSGopher_20261126210319" src="https://github.com/user-attachments/assets/a6c94ad9-86f1-48cd-9c6f-d216ef9df94c" />
<img width="3840" height="2160" alt="PSGopher_20261126210333" src="https://github.com/user-attachments/assets/4979a8d6-bf02-49ba-96ce-3ea0a358bbc2" />
<img width="3840" height="2160" alt="PSGopher_20261126210326" src="https://github.com/user-attachments/assets/54be3ca8-aa8c-44e5-a35d-876c9b27ee2d" />


A native PlayStation 5 port of [Gopher64](https://github.com/gopher64/gopher64), the Nintendo 64
emulator, for jailbroken consoles running homebrew. Gopher64's emulation is used unchanged:
its VR4300 and RSP interpreters and parallel-rdp, the Vulkan compute RDP, running on RADV
linked into the title.

**Status: v0.0.1, a very early build.** The title runs on the console (2026-11-25): the board,
USB drives, network shares and games, at full speed at 8x in the games played so far. What has and has not run
on a console is in [`CHANGELOG.md`](CHANGELOG.md).

This page is the user's guide. Developers: the documentation index is [`docs/README.md`](docs/README.md).

## Use

Start the title: its home screen is a live N64 mainboard, the cartridge you choose seated in its
slot, its facts beside it as Gopher64 reads them (game ID, lockout chip, save memory, pak), your
play time and your probes (save states). It finds every N64 ROM (`.z64`, `.n64`, `.v64`, and a
`.zip` or `.7z` of one) you dumped from your own cartridges, on the console's storage (anywhere in
`/data`, a few folders deep; `/data/psgopher/roms` is a good place), on USB drives and extended
storage, and on network shares. To search only some folders (a large USB drive is slow to search
whole), choose them in BENCH > Console > Game Folders: then only they, `/data/psgopher/roms` and the
network shares are searched.

**Romhacks**: put the patch (`.bps`, `.ips` or `.ups`) beside the ROM it was made for, with the
ROM's name (`game.z64` and `game.bps`), or anywhere for a BPS or UPS patch: PSGopher finds its ROM
by size and checksum. The patch is listed as its own cartridge and applied when it starts; your
ROM is not changed.

**64DD disks** (`.ndd`, `.d64`) are listed with the cartridges. They need the 64DD's IPL, which is
not included: put your 4 MB dump of it in `/data/psgopher/data/64dd` (BENCH > Emulation > 64DD IPL
shows the one found). Disk writes are kept beside your saves; the disk image is never changed.

- **HOME**: the board and the cartridge strip (recently played first). ◄ ► cartridge, ✕ power on,
  □ power on from a probe, △ the game's page (its cheats and its own settings, below). ○ asks to close PSGopher (✕ closes, ○ stays). Each cartridge shows its box art (libretro's thumbnails) and
  its region's flag, the panel three lines of its Wikipedia summary, fetched once over the network
  and kept in `/data/psgopher/cache/covers`.
- **RACK**: every cartridge on shelves, A to Z. D-pad, L2/R2 by letter (hold to keep going; the letter is shown large), ○ back to HOME, □ sort (A-Z, recently
  played, most played, size), OPTIONS show (all, played, never played, has probes, internal, USB,
  network), ✕ power on, △ seat it on the home strip.
- **BENCH**: every setting, in sections (L2/R2): Video (resolution, SSAA downscaling, downsampling, the
  final filter, the video interface's smoothing, anti-aliasing and filters, gamma dither, deinterlacing,
  native-resolution texture rectangles, crop overscan, VSync, shader compiles, the graphics driver's thread), Screen & Sound (CRT shader, widescreen, integer scaling,
  the audio buffer: 33 to 133 ms, 67 by default), TV Output (59.94 Hz, 119.88 Hz or variable refresh;
  HDR10 and its brightness), Emulation (overclock, Expansion Pak: Auto, On or Off, the 64DD IPL, rewind), Controllers
  (each port: enabled, which DualSense, its input profile, its Transfer Pak with the Game Boy
  ROM and save), Profiles (every N64 button's DualSense control: ✕ then press it; the deadzone),
  Hotkeys and Console (network shares, the sandbox, game folders, searching again, fast writes,
  updates, exiting). ▲ ▼ setting, ◄ ► change, ✕ choose; the line below says what
  the setting does. Gopher64's settings keep its labels. A ◆ beside a row means the chosen game has a
  setting of its own for it (below).
- **The Expansion Pak** (the N64's extra 4 MB): PSGopher knows which games need it, which use it for more
  and which misbehave with it (`gamedata/expansion-pak.tsv` in the title folder; sources in
  `docs/dev/expansion-pak.md`). **Auto** (the default) turns it on, except for a game that must run
  without it; **On** and **Off** force it. HOME's panel and a game's page say **Expansion Pak: Required,
  Enhanced, Must be off** or **Not listed** (a game the list does not know) and the memory the game gets (8 MB or 4 MB). Starting a game that needs it
  with it set to Off asks first (✕ starts anyway, ○ goes back).
- **A game's page** (△ on HOME): **Cheats**, and the Video, Screen & Sound, Emulation and Controllers
  rows that a game can have of its own (each port's Enabled and Transfer Pak, Emulate VRU; not the
  profiles or the Game Boy files). Each row starts at **Use Global Setting** (and shows what that is
  now); choosing a value sets it for this game only, kept in
  `/data/psgopher/config/games/<key>.ini`, the key being the cartridge header's game code with its
  destination and revision (`NXXE-00`; a `.zip` or `.7z` has no header and is kept by its file's name).
  When the game starts they are applied over the global settings, and after it the global settings are
  back. ◆ marks the rows the game has set, and BENCH marks them for the chosen game. The picture rows:
  - **Final Filter**: Sharp Bilinear (crisp pixels, blended over one output pixel at their edges; the
    default), Bilinear, Nearest, Bicubic (Catmull-Rom: sharper than Bilinear) or Lanczos (3 lobes, the
    sharpest, without halos at hard edges). Bicubic and Lanczos also filter a picture larger than the
    screen (8x without Downsampling) instead of skipping pixels. The CRT shader, when on, takes its place.
  - **Post Anti-Aliasing**: Off (the default) or FXAA (NVIDIA FXAA 3.11), smoothing jagged edges at the
    screen's resolution after the Final Filter.
  - **Sharpening**: Off (the default), Low, Medium or High (AMD FidelityFX Contrast Adaptive
    Sharpening), at the screen's resolution after Post Anti-Aliasing; with HDR on, before the picture
    is made into HDR10. Neither applies with the CRT shader.
  - **Downsampling**: Off, 1/2, 1/4 or 1/8: the upscaled picture scaled down by that much, never below
    the original size. SSAA downscaling, when on, always scales down all the way.
  - **VI Bilinear** (Auto: off at 4x and 8x, on at 1x and 2x; On; Off), **VI Anti-Aliasing**,
    **Dither Filter**, **Divot Filter** (on) and **Gamma Dither** (off).
  - **Deinterlacing**: Bob (each field on its own) or Blend (fields merged with the one before).
  - **Native-Resolution Texture Rectangles** (off) and **Crop Overscan** (None, 8 or 16 pixels).

L1/R1 change the view. **In a game**, tap the touch pad for the quick menu: resume, take a probe
(save state), load one, turn the stats overlay on or off (speed, frame times, audio), reset, or
eject the cartridge (back to the board). The controls are
Gopher64's default gamepad layout on the DualSense: Cross A, Square B, L2 Z, Options Start, the
right stick the C buttons; the touch pad held down is Gopher64's hotkey activator (with Options: back
to the board; with L1/R1: save/load state; with L2: fast forward; with C-left: rewind; with B: change
the pak). In BENCH, Hotkeys turns them all off, gives each action another button or none, and adds
what Gopher64 has only on a keyboard (reset, pause, frame advance, volume, the save slot, its frame
rate, cropping the letterbox); the quick menu's tap can be switched off too (it stays on while
nothing else leaves a game). **Speed**: HOME's □ Probes, △ on a probe benchmarks that moment at 1×,
2×, 4× and 8× and shows each one's speed (take the probe at 1× to play at full speed); with Slow
moments on (BENCH, Video), a slow stretch in a game is measured into the boot log by itself. Saves
and states are in
`/data/psgopher/data/`.

**Updates**: BENCH, Console, Check for Updates asks GitHub for the newest release (also once when the
title starts; Check for Updates at Start turns that off). An offer shows the release notes under the
rows: Download Update (checked against its SHA-256), Install Update (the title closes; the files it
replaces are backed up), Skip This Version, and Roll Back to the version kept from before the last
update. BENCH, Console, Fast Writes (Helper) has the write helper copy games from a share to the
console's storage, which keeps it fast after the first gigabyte.

**Network shares**: BENCH, Console, Network Shares adds and edits SMB and NFS shares (an
on-screen keyboard; "Find SMB servers" looks for them on your network). They are mounted at each start and
searched for games; a ROM on a share is copied to the console before it plays.

**USB drives** are outside a title's sandbox: as OGBox and Crux2 do, PSGopher leaves it at the start
through the payload loader on port 9021 (elfldr, which kstuff setups run). BENCH, Console
turns that off.

**Settings** are kept as you change them: Gopher64's own in its `config.json`, PSGopher's in
`config/psgopher.ini`. Variable refresh and HDR10 are off by default and untested on a console yet.

`/data/psgopher/autostart.txt` (a ROM's path or a name in `roms/`) still boots that ROM first.

**The log**: `psgopher-boot.log` in the title folder (`/data/homebrew/PPSA99280/` when installed
there; `/data/psgopher/` when the title folder takes no file), with the run before kept as
`psgopher-boot.previous.log`, as OGBox and Crux2 keep theirs. Gopher64's own messages are in it too.
When a game does not start, the board says why at the top.

No games, BIOS or other copyrighted data are included. Where each part of PSGopher came from is in [`PROVENANCE.md`](PROVENANCE.md).

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
