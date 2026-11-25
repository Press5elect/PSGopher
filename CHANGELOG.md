# PSGopher changelog

Every change, the newest version first. Nothing is released yet: Unreleased is the state of the
branch. Games are named by cartridge ID only.

## Unreleased

Status: the title, the sandbox exit, an SMB share, USB drives, covers, descriptions, the sound buffer and three games
have run on a console (2026-11-25): one NTSC game at full speed, two below it. Not yet run on a
console: the cheats, variable refresh, HDR10.

### Front end

- Added: the front end is the Live Board (`ps5/art/live-board.html`, the design chosen): HOME, the
  live mainboard with the chosen cartridge sliding into its slot and its signals running, the panel
  (Gopher64's facts of the cartridge, play time, probes drawn from the save states' own bytes), the
  cartridge strip; RACK, every cartridge on shelves with letters, sorts and filters (Internal, USB,
  Network among them); BENCH, the board's parts as the settings, each drawn to its part. The board
  is the design's canvas rendered at 4000x2500 (`ps5/tools/make-board-art.mjs`) and drawn through
  the design's own 3D transform; the faces are the design's (Bricolage Grotesque, JetBrains Mono).
  A game can be powered on from a probe. It replaced the first game list and its settings page.
- Added: the in-game quick menu, the Live Board's LIVE panel: a touch pad tap pauses the game and
  shows Resume, Take a memory probe and Load a probe (save states in slots 1-5, with their times),
  Press reset and Eject cartridge (back to the board), with the live frame rate, the emulation
  thread's CPU time a frame, the present's wait and the audio queue. Drawn with Dear ImGui over the
  game's frame by parallel-rdp's own device (`ps5/host/quickmenu.cpp`; the overlay in
  `ps5.patch`); touch pad + a button stays Gopher64's hotkey.
- Added: covers and descriptions. Each cartridge's label is its box art from libretro's thumbnails
  (the libretro-thumbnails N64 set, read from its GitHub repository over HTTPS: thumbnails.libretro.com
  answered 404 for every N64 name when this was written), on the strip, the rack's panel and the
  cartridge in the slot; the panel shows three lines of the game's Wikipedia summary (found as
  OGBox finds one). Both are fetched as they are shown, on a thread of their own, and kept in
  `/data/psgopher/cache/covers` (none found: asked again after 30 days). The fetches are rustls over
  a blocking socket in the core (Tokio's sockets are refused on the console), with webpki-roots'
  certificates. Untested on a console.
- Added: the region's flag: on each cartridge of the strip and in the panel's REGION fact (from the
  header's country code).
- Added: cheats: HOME's △ opens Gopher64's cheats of the chosen game (its own cheat database, by
  its CRC key), each on or off with its option; they apply from the next start of the game.
- Added: the stats overlay (as Crux2's): the quick menu's "Stats overlay" shows the VIs a second,
  speed, the game's own frame rate, CPU and present time a frame, the RDRAM syncs, the audio queue
  and a frame-time graph over the game (kept in `psgopher.ini`, `stats = on`). The `video:` line in
  the log every 10 s says the same, and the log says which RDRAM path parallel-rdp took.
- Fixed: the quick menu moved two places for one press (the pad's state was made again each frame,
  so a press was seen twice).
- Added: the library: every N64 ROM on the console's `/data`, USB drives, extended storage and
  network shares, kept between starts and searched again in the background; a game ends back on the board.
- Added: settings kept as they change: Gopher64's own in its `config.json` (through
  Gopher64's `Config`), PSGopher's refresh and HDR10 in `psgopher.ini`. No text file to edit.
- Added: network shares (SMB and NFS) through Crux2's ps5vfs and OGBox's share list, added and
  edited on BENCH with an on-screen keyboard and a search for SMB servers.
- Added: USB drives and the whole of `/data`: the title leaves its sandbox at the start, as OGBox
  and Crux2 (the bundled `sandbox-elevator.elf` through the payload loader, port 9021);
  BENCH, This console turns it off.

### Console fixes (from the first console logs, 2026-11-25)

- Fixed (Gopher64, in `ps5.patch`): no game started: Tokio's whole runtime is refused by the console
  (`Operation not supported`, errno 45) and Gopher64 panicked. PSGopher builds it with the scheduler
  and timers only when that happens (all Gopher64 uses on the console), logs which of kqueue and an
  AF_UNIX socket pair was refused, and keeps the USB cartridge's TCP link off without it.
- Fixed: Rust read the console's `stat`, `dirent` and `kevent` with FreeBSD 12's layouts (its std's
  libc is 12's; the console's is 11's), so every folder looked like no folder ("File exists" at
  Gopher64's start) and Tokio's poller got the wrong event size (likely its "Operation not
  supported"). Rust's calls of `stat`, `fstat`, `readdir` and `kevent` now go through converters
  (`ps5/host/rust_abi.c`), checked against the SDK's structures at compile time and against Rust's
  libc's own offsets by a host test.
- Changed: a panic is written to the boot log with its message, place and thread.
- Added: the crash report (Crux2's): a fault writes its signal, registers and stack into the boot
  log before the console closes the title. A `video:` line says when a game's first frame is shown,
  then its frames every 10 s.
- Added: the boot log, `psgopher-boot.log` in the title folder (the run before kept), as OGBox and
  Crux2; it has Gopher64's own messages and panics, which went nowhere before (Rust writes to
  descriptor 2, which klog's capture cannot move). A game that does not start says why on the board.

### Console fixes (from the second console logs, 2026-11-25)

- Fixed: the title stopped (SIGABRT in ImGui's `GetTexID`) after several hundred covers had been
  loaded: ImGui's descriptor pool held 16 pictures against a cover cache of 48, and a failed
  upload handed ImGui a null texture (audit FE-001). The pool now holds every cover the cache keeps
  plus the board, the logo and the font atlas's pages; every step of an upload is checked and a
  failure frees what it made and is logged (`menu: a picture ... could not be uploaded`, FE-031).
- Fixed: RACK asked for and uploaded a cover for every spine on its shelves, which never draw one
  (FE-003); the cover cache no longer grows past its size.
- Fixed: covers did not come back after a game (FE-002): they are asked for again from the cache
  folder in the next menu.
- Fixed: with two controllers, holding the touch pad opened the quick menu (FE-004): each
  controller's tap is its own. A host test covers one and two controllers.
- Fixed: a 2 KiB `[BIOS]` PIF file on a share was listed as a game and failed to start: raw files
  smaller than a ROM's header and boot code (4 KiB) are not listed.
- Fixed: descriptions taken from the wrong article ("2048" found another game's, the PIF file a
  general article): an article is used only when its title is the cartridge's name (No-Intro's
  "Name, The" turned round first). The first descriptions are deleted once and asked for again.
- Changed: the `video:` line gives parallel-rdp's share of the CPU time (`CPU x ms (RDP y)`), so a
  log says whether the emulated CPU and RSP or the RDP's command processing takes a slow game's time.
  Underruns are counted from the game's first audio on (before it the queue is empty by design).

### From the seventh console log (2026-11-25)

- Measured: at 8x the GPU spends 15-16 ms a frame in parallel-rdp's depth-blending pass (of ~18 ms),
  the shading 1-1.5 ms: that pass is what slows a demanding game at 8x.
- Added: two switches for that pass (BENCH's video stage, off by default, measured with GPU timing):
  compute waves 32 wide (`wave32`, RADV_PERFTEST=cswave32; RADV's default is 64) and parallel-rdp's
  8/16-bit arithmetic (`small_types`; it turns it off on RADV from PC measurements).
- Fixed: `psgopher.ini` keys with digits were not read (the parser took letters and _ only).

### From the sixth console log (2026-11-25)

- Added: GPU timing (BENCH's video stage, `gpu_timing`, off by default): parallel-rdp's own GPU
  timestamps, each stage's time a frame in the boot log every 10 s (`gpu:` lines). The log showed
  the emulation thread waiting 8-13 ms a VI for the GPU at 8x in one game; this says which stage.

### Settings, hotkeys, BENCH and the cartridges (the owner's requests, 2026-11-25)

- Added: every Gopher64 setting is in BENCH: vsync; each port's controller (which DualSense), input
  profile and Transfer Pak with its Game Boy ROM and save (a file picker over the console and USB
  drives); the input profiles themselves (each N64 button's DualSense control, bound by pressing
  it, the Hotkey Activator, the deadzone, DirectInput; new and deleted profiles). What has no place
  on the console (fullscreen, keyboard bindings, Gopher64's own window's theme and folders,
  netplay, RetroAchievements) is listed under PC only with the reason.
- Added: the hotkeys can be configured: all on or off, each action's N64 button or none, the
  activator rebound; Gopher64's keyboard-only functions become hotkey actions (reset, pause, frame
  advance, volume, next and previous save slot, its frame rate, crop the letterbox). The quick
  menu's touch pad tap can be off (kept on while nothing else leaves a game). In `psgopher.ini`
  (`hotkeys`, `hotkey_<action>`, `quickmenu_tap`); the core reads them before each game.
- Changed: BENCH is sections (L2/R2) of labelled rows with a line saying what the chosen one does,
  in place of cards around the board that ▲▼ crossed in no clear order.
- Changed: the home strip's cartridges are drawn as N64 cartridges (the owner's reference photo):
  the grey shell with its arched top, raised centre panel, grooves and steps, the label in its
  recess.

### From the fifth console log (2026-11-25)

- Measured: shading with the general pipeline is 0% at 8x, and at 2x parallel-rdp takes 1.7 ms of
  20 ms a VI: the emulation itself (Gopher64's interpreters of the N64's CPU and RSP) takes the
  time in that game. The `video:` line now splits the CPU time into the RSP's and parallel-rdp's,
  and each game's start logs the settings that cost speed (upscale, overclock, rewind, expansion pak).
- Changed: the core is built whole-program optimised (the `psgopher` profile: release with fat LTO
  and one codegen unit; the title is 45 MB, was 53 MB).
- Changed: Gopher64's pin moved to `4b824cb8f629` (upstream's touch overlay fix).

### From the fourth console log (2026-11-25)

- Fixed: a cartridge got the description of a later game of the same name: an article whose title
  or description gives a year outside the N64's (1995-2004) is not taken; the earlier ones are
  asked again.
- Added: how parallel-rdp makes its shading pipelines (BENCH's video stage, `shader_compile`): until
  a state's specialised pipeline is compiled, its pixels are shaded by the slower general one, and
  Gopher64 compiles them on one background thread; four threads, or each compiled when first
  needed, are the choices (one thread stays the default). The `video:` line gives the share of
  shading done with the general pipeline and the compiles (done, time each, waiting). One game ran
  at 64% at 8x and 93-100% at 1x; this says whether the general pipeline is why.
- Added: the driver's own recording thread (BENCH, `driver_thread`; Crux2's `RADV_THREADED_RECORDING`),
  off by default.
- Changed: `fetch-gopher64.sh` exports the parallel-rdp submodule's changes as
  `ps5/patches/gopher64/parallel-rdp.patch` (`ps5.patch` leaves the submodule out).

### Console fixes (from the third console log, 2026-11-25)

- Fixed: a share whose listing failed (the NAS answered `SMB2_STATUS_IO_TIMEOUT` after the USB
  search) emptied the library and lost every cartridge's header facts, which were then read again
  over the network. A share's listing is asked twice, and a place that cannot be listed keeps the
  last list's games. A host test covers one and two failures.
- Added (Crux2's): network shares are woken at the title's start: a little of a cartridge on each
  is read on a thread of its own, so a NAS's disks are turning before the library lists it
  (`vfs: share ... woken` in the boot log).
- Added: the sound buffer, BENCH's AV out (`audio_buffer` in `psgopher.ini`): how much sound
  Gopher64 keeps queued, 33 ms (2 VIs, Gopher64's own) to 133 ms; 67 ms by default, about Crux2's
  (PCSX2's 50 ms buffer and 20 ms output latency). More room means fewer gaps near full speed and
  more delay; below full speed no buffer is enough (Crux2 also stretches the sound in time).
- Fixed: a description was missed when the article's title had the cartridge's words in another
  order; descriptions are asked of Wikipedia every 1.5 s at most (it answered 429 at one lookup
  every half second), and the earlier ones are asked again.
- Changed: the `video:` line splits parallel-rdp's time into drawing commands, SyncFull's GPU
  submissions and the frame's render (`RDP y: draw a, sync b, frame c`).

### Platform and build

- Changed: Gopher64's pin moved to `5cf32936c8b4` (master of 2026-11-25: a Transfer Pak clock fix).
- Added: Gopher64 (`439e6c11f69d`, master of 2026-11-24) builds for the PS5 as a static library,
  with Rust's standard library rebuilt for the console's target (`ps5/rust/x86_64-sie-ps5.json`)
  and parallel-rdp, volk and rcheevos through the payload SDK's clang (fork `88d6e41`).
- Added: `ps5/sdl3`, the 88 SDL3/SDL3_ttf functions Gopher64 calls, on the PS5: the display through
  RADV's `VK_KHR_display`, up to four DualSense controllers, a 48 kHz audio stream with a
  windowed-sinc resampler, timers and event watches, text through stb_truetype. Host tests.
- Added: the title `PPSA99280` (provisional ID): klog, the splash, folders under `/data/psgopher`,
  the ROM from `autostart.txt` or `roms/`, the shell exit. Links through PS5_Vulkan's RADV recipe
  (RADV at PS5 Mesa `1ba9874`); every import resolves to a module a title loads.
- Added: 119.88 Hz, variable refresh and HDR10 output, through Crux2's RADV, chosen in
  `/data/psgopher/config/psgopher.ini` (`display`, `hdr`); off by default until tested on a console.
- Changed: RADV is Crux2's (PS5 Mesa `1ba9874` with Crux2's 17 driver patches, Mesa main 27.0-devel
  among them), built by Crux2's script; the build refuses PS5_Vulkan's plain archive.
- Added: the console probe (`probe.txt`) and the benchmark mode (`benchmark.txt`).
- Changed: the console icon is Gopher64's gopher logo on PSGopher's dark ground (`ps5/sce_sys/icon0.png`,
  made from Gopher64's `data/icon/gopher64.png`).
- Changed (Gopher64, in `ps5.patch`): `config.json` and the cheats file are written through a
  buffer: a `write()` costs about 3.3 ms on the console (PS5_PayloadSDK's PROBE.md).
- Changed (Gopher64, in `ps5.patch`): `psgopher_setting_get/set`, Gopher64's settings by name for
  the front end; HDR10 goes off as a game ends.
- Added: `NOTICE` (the OGBox and Crux2 attributions kept, every other author), shipped as
  `NOTICE.txt`; BENCH, This console, About.
