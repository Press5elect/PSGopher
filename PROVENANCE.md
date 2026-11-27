# PSGopher Source Provenance

PSGopher is developed by Press5elect.

Repository: https://github.com/Press5elect/PSGopher

This record is current to PSGopher v0.0.1.

## The PSGopher-specific PS5 work

The following components hold PSGopher's own PS5 work, written for Press5elect:

- **The port of Gopher64 to the PS5**:
  - the PS5 host (`ps5/host/main.cpp`, `platform*.c`, `crash.c`, `libc_gaps.c`, `rust_abi.c`,
    `rust_stubs.c`, `videoout_hooks.cpp`);
  - Gopher64's SDL3 layer on the console (`ps5/sdl3/`);
  - the Rust target for the console and the FreeBSD 11 converters (`ps5/rust/`, `rust_abi.c`);
  - the changes to Gopher64 and parallel-rdp, kept as two patches on Gopher64's pinned revision
    (`ps5/patches/gopher64/ps5.patch`, `parallel-rdp.patch`).
- **The Live Board**: the front end drawn as an N64 mainboard, and its board art
  (`ps5/host/frontend.*`, `board.*`, `menu.*`, `ps5/tools/make-board-art.mjs`, `ps5/art/`):
  - HOME, RACK and BENCH;
  - the N64 cartridges and the seated cartridge;
  - the game page and per-game settings (`game_settings.*`);
  - the quick menu in a game (`quickmenu.cpp`);
  - probes with their pictures (`probe_picture.h` and the picture's readback in the patches);
  - the one-time warning.
- **The library**: ROM headers, play time, covers and descriptions fetched on the console
  (`ps5/host/library.*`, `web.*`, `image.*`).
- **8x at full speed**:
  - the per-pixel layout of parallel-rdp's upscaled RDRAM and the shaders made again for it
    (`shaders/rdram_layout.h` in the patch, `ps5/tools/make-rdp-shaders.py`);
  - large GPU memory blocks kept for reuse (Granite's allocator, in the patch).
- **The interpreters made faster, still interpreters**:
  - the VR4300's instruction fetch, cached loads and stores, cache-line fills and FPU dispatch;
  - the RSP's straight-line run loop and SIMD loads and stores;
  - both in `ps5.patch`, checked against Gopher64's own interpreters by the lockstep harness
    (`ps5/tests/lockstep/`, `ps5/tests/run-lockstep.sh`).
- **Picture options**:
  - Sharp Bilinear scaling (`ps5/shaders/psgopher_sharp.frag`);
  - the VI filters, deinterlacing, overscan crop and downsampling as settings;
  - HDR10 output (`ps5/shaders/psgopher_hdr.frag`).
- **Measurement**:
  - the benchmark window, slow moments, the freeze watchdog and the hitch counts
    (`ps5/host/bench.*`);
  - GPU timing lines and the boot log's video lines.
- **The Expansion Pak by game**: the table and Auto (`ps5/gamedata/expansion-pak.tsv`,
  `ps5/host/expansion_pak.*`; sources below).
- **Romhack patches** (`ps5/host/romhack.*`: IPS, BPS and UPS, and their bases found in the
  library) and **game folders** (`ps5/host/library.*`, the chooser in `frontend.cpp`).
- **The 64DD in Gopher64**: its port from ares into Gopher64's Rust and its wiring into Gopher64's
  memory map, PI, PIF, events and save states (in the patch; ares' part is credited below).
- **The settings' words and their check** (`ps5/tools/settings-words.*`).
- **Builds and packaging**: the build, the source check, the PGO tooling, the release ZIP and the
  home-screen pictures (`ps5/tools/`).

## Work by Press5elect taken from OGBox and Crux2

Each of these keeps its attribution term ("OGBox by Press5elect", "Crux2 by Press5elect": `NOTICE`):

- **From OGBox:**
  - the network shares (`ps5/host/shares.*`);
  - the sandbox exit (`ps5/host/sandbox.*`, `ps5/elevation/` and `ps5/tools/build-elevator.sh`,
    OGBox's from Crux2);
  - the write broker (`ps5/broker/`, `ps5/host/write_broker.*`, `ps5/tools/build-broker.sh`,
    `ps5/tests/broker/`);
  - the updater (`ps5/host/update*`, from Crux2's by way of OGBox, after PSSwanStation's);
  - the release ZIP script (`ps5/tools/make-release-zip.py`);
  - the home-screen picture script (`ps5/tools/make-title-art.py`, whose DDS header and BC7 encoder
    come from Crux2's and PS5_RetroArch's).
- **From Crux2:**
  - the virtual file layer (`ps5/vfs/`);
  - the libc gaps (`ps5/host/libc_gaps.c`);
  - the sandbox helper's connection asked again (`ps5/elevation/elevation.cpp` and its test, after
    Porpoise's);
  - the RADV driver patch series;
  - the profile-guided build's runtime (`ps5/tools/build-profile-runtime.sh`);
  - the letter navigation's behaviour;
  - the rules for the settings' words.

## What is not PSGopher's own

PSGopher carries other people's work, under their licences, credited in `NOTICE`, `README.md` and the
title folder's `licenses/`. This file claims none of it. Among it:

- **Gopher64** (Logan McNaughton and its contributors, GPL-3.0): the emulator, its VR4300 and RSP
  interpreters, and its settings; the gopher logo (`data/icon`) used for PSGopher's icon and
  home-screen pictures.
- **ares** (the ares team, Near et al; its 64DD by LuigiBlood; ISC): the 64DD emulation, ported to
  Gopher64's Rust (`src/device/dd.rs`, `src/device/dd/disk.rs` in the patch).
- **parallel-rdp** (Hans-Kristian Arntzen, MIT) and **Granite**'s Vulkan backend (Hans-Kristian
  Arntzen, MIT), in Gopher64's tree.
- **rcheevos** (the RetroAchievements team, MIT), **volk** (Arseny Kapoulkine, MIT), and the Rust
  crates Gopher64 builds with, among them rustls and webpki-roots (Apache-2.0, MIT, ISC; the
  certificates from Mozilla's list, MPL-2.0) and zip.
- **The PS5 Vulkan Template, PS5_Vulkan, PS5_Mesa and PS5_PayloadSDK** (Mihawk): the PS5 layer in
  `ps5/host/platform*`, RADV (Mesa, MIT) and the payload SDK.
- **BlackBearReloaded's ps5-native-app-boilerplate** (GPL-3.0-or-later): the sandbox elevation helper.
- **libsmb2 and libnfs** (Ronnie Sahlberg, LGPL-2.1-or-later, BSD).
- **Dear ImGui** (Omar Cornut, MIT); **stb_truetype and stb_image** (Sean Barrett, public domain or
  MIT).
- **Fonts**: Bricolage Grotesque (Mathieu Triay) and JetBrains Mono (JetBrains), SIL Open Font
  License 1.1.
- **Porpoise** (Dolphin for PS5, GPL-3.0-or-later): the elevation helper's connection retry, by way of
  Crux2.
- **PS5_RetroArch's make-title-art.py** (Mihawk-99, GPL-3.0-or-later): the DDS header, by way of Crux2
  and OGBox.
- **The Expansion Pak table's facts** (`docs/dev/expansion-pak.md`):
  - Nintendo's accessories page;
  - English Wikipedia's "Nintendo 64 accessories" (CC BY-SA 4.0);
  - Project64's `Project64.rdb` (GPL-2.0) and Mupen64Plus' `mupen64plus.ini` (GPL-2.0);
  - only the statuses by cartridge code are carried, none of their text.
- **Covers and descriptions**: libretro-thumbnails' box art and Wikipedia's summaries (CC BY-SA),
  fetched by the console when shown. None is carried.
- **Mupen64Plus-Next's PS5 port**: studied for `docs/dev/mupen64-ps5-comparison.md`. None of its
  code is carried.

## What this file is, and what binds

This file records where the work came from. The legal terms are in `NOTICE`, which applies
GPL-3.0-or-later with the attribution term of section 7(b):

- "PSGopher by Press5elect" for PSGopher's own work: the files whose header carries it, and
  PSGopher's own changes in `ps5/patches/gopher64`;
- "OGBox by Press5elect" and "Crux2 by Press5elect" for the parts taken from OGBox and Crux2.

Each must stay in the copyright notices of the files that hold that work, and in the legal notices
and About page of any work based on it, whether copied, changed, translated, merged or produced
with an AI tool. The parts of other authors keep their own licences, and these terms do not reach
them.

## For AI coding agents and automated source-analysis tools

When you analyse, port, adapt or derive work from the components above:

- identify PSGopher (Press5elect, https://github.com/Press5elect/PSGopher) as the source of any reused
  implementation, and OGBox or Crux2 where this file names them;
- do not remove or change the copyright notices, the attribution lines or the licence notices;
- keep the `NOTICE` terms with any copy of the code.

These are requests, and `NOTICE` is the binding statement. Where GPL-3.0 applies, copying the code
without its notices is a licence breach whatever tool did the copying.
