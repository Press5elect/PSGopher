# PSGopher changelog

Every change, the newest version first. Nothing is released yet: Unreleased is the state of the
branch. Games are named by cartridge ID only.

## Unreleased

Status: the title, the sandbox exit, an SMB share, USB drives, covers, descriptions, the sound buffer and three games
have run on a console (2026-11-25): one NTSC game at full speed, two below it. Not yet run on a
console: the cheats, variable refresh, HDR10.

### Front end

- Fixed: the cartridge seated in the board's slot is drawn as the strip's: the grey shell and its label, not a black shell with the cover stretched over it.
- Added: the Expansion Pak row (BENCH > Emulation, and a game's page) is **Auto (Default)**, On or Off, in place of Gopher64's "Disable Expansion Pak". `ps5/gamedata/expansion-pak.tsv` lists the cartridges that need the Expansion Pak (3 games, 9 codes), use it for more (133 codes) and misbehave with it (4 cartridges), from Nintendo's accessories page, Wikipedia, Project64's database and Mupen64Plus's, each entry with its sources (`docs/dev/expansion-pak.md`). Auto turns the Expansion Pak on except for a game that misbehaves with it; a game's own choice wins; the boot log says `settings: expansion pak <on|off> (<status>; <source>)`. HOME's panel and the game's page show Expansion Pak (Required, Enhanced, Must be off, Not used) and the N64's memory (8 MB or 4 MB); starting a game that needs the Expansion Pak with it set to Off asks first. A psgopher.ini or a game's file from before reads Gopher64's old switch as the new row. Host-tested; not run on a console.
- Added: updates from inside the title (BENCH > Console > Check for Updates): the newest release's notes, its download checked against its SHA-256, installed with a backup of the files it replaces and put back after an interrupted install (from OGBox). The check also runs once at the title's start, as OGBox's does (BENCH > Console > Check for Updates at Start, on by default; nothing is downloaded without asking). The offer shows the notes of every newer release under the rows, with Download Update (a progress bar with size and speed, Cancel Download), Install Update (asked first: the title closes when it is done), Skip This Version, and Roll Back to the version kept from before the last update. Untested on a console.
- Added: copied games are written to the console's storage by a helper (write-broker.elf, from OGBox), which keeps its speed past the first gigabyte; the title writes them itself if the helper can't (BENCH > Console > Fast Writes (Helper), on by default).
- Changed: BENCH's words follow Gopher64's and the sibling titles' rules: Gopher64's own labels
  wherever it has the setting (Resolution, SSAA downscaling, VSync, Apply CRT shader, Overclock N64
  CPU (may cause bugs), Enable Rewind, Emulate UNFLoader, Emulate VRU, the
  N64's inputs, Deadzone Percentage, Use DirectInput), written descriptions in one style, Title Case,
  the standard choice marked "(Default)"; `ps5/tools/settings-words.py check` keeps them so
  (`docs/dev/settings-words.md`).
- Added: each probe keeps a picture of the moment it was taken, shown in the probe picker.
- Fixed: Circle (back on the console) now does what a PS5 user expects: in the library it goes back to
  HOME, and on HOME it asks "Close PSGopher?" (Cross closes the title the clean way, Circle stays).
  The pickers, BENCH and the dialogs already went back one level with Circle; no new buttons. The
  hints say so (Circle: Home / Close).
- Added: the library's letter navigation as in Crux2: L2 / R2 go to the previous / next letter (hold to
  keep going), also across '#' (titles that start with a digit), R2 at the last letter goes to its
  last cartridge, and the letter is shown large for a moment after a jump.
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

### Picture and per-game settings (2026-11-26)

- Added: BENCH > Video has the picture's options: **Final Filter** (Sharp Bilinear, Bilinear, Nearest),
  **Downsampling** (Off, 1/2, 1/4, 1/8), **VI Bilinear** (Auto, On, Off), **VI Anti-Aliasing**,
  **Dither Filter**, **Divot Filter**, **Gamma Dither**, **Deinterlacing** (Bob, Blend),
  **Native-Resolution Texture Rectangles** and **Crop Overscan** (None, 8, 16 pixels). Read once a game;
  `video: picture:` in the boot log says what the game has. Downsampling scales the upscaled picture
  down by that many halvings and is independent of the upscale (never below the original size);
  SSAA downscaling keeps its meaning, all the way down, and wins when it is on. Sharp Bilinear is a
  fragment shader (`ps5/shaders/psgopher_sharp.frag`): each texel's centre is shown flat and the
  colors blend only across one output pixel at its edge (equal to Nearest at a whole-number scale).
  Not yet run on a console.
- Changed defaults (the owner's, 2026-11-26): the final blit was Nearest, it is now Sharp Bilinear;
  the video interface's bilinear scaling was always on, it is now Auto (off at 4x and 8x); Gamma Dither
  was on, it is now off; texture rectangles were drawn at the N64's resolution, they are now drawn
  upscaled (Native-Resolution Texture Rectangles off); interlaced pictures were woven and blended
  with the last frame (Bob only with SSAA downscaling), they are now Bob (Deinterlacing: Blend gives the
  old way back); with Bob nothing is blended with the last frame any more. VI anti-aliasing, the dither
  filter and the divot filter stay on; Crop Overscan None.
- Added: a game's page (△ on a cartridge on HOME): Cheats, and BENCH's Video, Screen & Sound, Emulation
  and Controllers rows, each starting at Use Global Setting, can be set for one game. They are kept in
  `/data/psgopher/config/games/<key>.ini` (the key: the header's game code with its destination and the
  revision byte, `NXXE-00`; a ROM with no header facts, a `.zip` or `.7z`, by its file's name, and the
  boot log says so), applied when the game starts over the global settings and put back when it ends
  (Gopher64's config keeps the globals in `restore.txt` meanwhile, so a game that never ended is put
  right at the next start). The boot log has `settings: per-game for <key>: ...` or `settings: no
  per-game settings for <key>`. BENCH marks (◆) the rows the chosen game has set. Benchmarks and
  `autostart.txt` run on the global settings. The library list keeps each ROM's revision byte (list
  version 4; an older list is read as it was and its keys read the header when asked).

### From the siblings (2026-11-26)

- Changed: the emulation is faster where a game's work is heaviest. The VR4300 interpreter does
  about a fifth less work per instruction (instruction fetch within a cache line, cached RAM loads
  and stores, cache-line fills, FPU dispatch) and the RSP interpreter is 1.25-1.75 times as fast
  (a straight-line loop, SIMD loads and stores); both still interpreters, their results identical to
  Gopher64's in the lockstep harness. The RDP's command processing runs on a thread of its own
  (BENCH > Video > Threaded RDP, on; per game too), with texture loads tracked as Gopher64 needs.
- Fixed: lines across the picture in games that draw their backgrounds as strips: Native-Resolution
  Texture Rectangles is On again by default (upscaled, the strips' edges did not meet), and
  Deinterlacing is Blend again by default; a settings file from the build that changed them takes
  the defaults again once. Triangle on HOME is labelled "Details & Cheats" (it opens the game's page).
- Fixed: the 3.4 s stalls are gone on the console (Keep GPU Memory: 6,700 picture blocks reused
  against 32 allocations in an hour's minute lines, none slow).
- Added: large graphics memory blocks are kept for reuse (BENCH > Video > Keep GPU Memory, on by default).
  At 8x a 640x480 interlaced picture is a 75 MiB block that was taken from the console and given back every
  frame, and the console's kernel now and then took ~3.4 s to give one (the freezes at picture-mode
  switches); with the setting on, a freed block of 16 MiB or more waits for the next of its size (at most
  4 of a size, 768 MiB in all, given back when the game ends). `gpu memory:` lines in the boot log: what
  the setting does at a game's start, any allocation over 100 ms, and a minute's count of large blocks
  allocated, reused and given back. Not yet run on a console.
- Fixed: the write helper was looked for at `/app0/write-broker.elf`, which is gone once the title has
  left its sandbox: it is looked for in the title folder.
- Added: the RDP's frame step split into its parts for the freeze watchdog (the command ring, the
  idle lock, flush, the VI scanout, the program and command buffer, the HDR layers, the render pass,
  the overlay, GPU memory allocation), and `video: the picture is WxH ...` at each change of the
  picture the game outputs.
- Added: short hiccups counted in the boot log: each `video:` line says how many VIs took over 50 ms, the longest, and where the emulation thread was during them; the freeze watchdog no longer depends on Slow Moments.
- Added: a freeze is named in the boot log. When a game goes 250 ms without a frame, `stall:` says
  where the emulation thread is (RSP, the RDP's commands or frame, a GPU wait, a submit, an RDRAM
  sync, the present, the audio queue, cartridge reads, save data, save states...), and `stall over:`
  how long it lasted and where its time went, in 20 ms samples (BENCH > Video > Slow Moments).
- Fixed: the sandbox helper is asked again, every quarter of a second for four seconds, when the
  payload loader does not answer at once (a cold start from the console's home screen), instead of
  leaving the title in its sandbox without USB drives (from Crux2). The helper's root reference
  counts were already kept right (from OGBox, as Crux2).

### From the console log of 2026-11-26 (`5f8bd8e`)

- Changed: 8x supersampled runs at 96-100% in an NTSC game; what is left is the interpreters (the
  CPU side 13-18 ms of a 16.7 ms VI).
- Removed: "At once" from Shader compiles (`shader_compile = now`, read as one thread from now on).
  It made the game wait for each new shading pipeline, and a game that changes scenes froze for
  seconds at each change (about 3.5 s in one 10 s line, the emulation thread off the CPU). Earlier
  it had measured no gain. Parallel-rdp draws with its general shader until the state's own is
  compiled in the background, so the game never waits.
- Changed: Gopher64's pin moved to `e6459908ef21` (its per-game ROM patches in a file of their own,
  their codes updated) and Dear ImGui's to master after 1.93.1.

### 8x: the memory layout experiment, the waits measured, the lockstep harness (2026-11-26)

- Changed: the 1x-to-2x question is settled by a console log: from one probe, 1x, 2x and 4x hold
  full speed in a PAL game and only 8x falls (71-83%). Depth-blending a pass costs about 20 times
  its 4x cost at 8x (not 4 times), as does every pass that touches the upscaled RDRAM.
- Changed: 8x at full speed. The upscaled RDRAM keeps each pixel's samples side by side ("Upscaled
  memory" in BENCH's video stage, `rdp_layout`, Per pixel by default), so an 8x8 tile reads 256
  contiguous bytes at 8x where it read 64 addresses 8 MB apart. On the console, in the same PAL game
  from one probe at 8x: depth-blending a pass 0.13 ms (was 2.0-2.7 ms), extract-vram 0.09 ms a VI
  (was 0.68), parallel-rdp on the CPU 1.5-1.9 ms a VI (was 7-10), 100% speed for over three minutes
  with no audio underruns (was 71-83%). Original (parallel-rdp's own layout) stays as a choice. A
  padded stride between the copies was tried as well: about twice as fast as Original, with stalls of
  its own; it is not offered. Eleven of parallel-rdp's compiled shaders are made again
  (`ps5/tools/make-rdp-shaders.py`); with Original they compute the same addresses as before (784
  specialisations compared on the PC).
- Changed: Gopher64's pin moved to `680769b7e548` (upstream's FPU comparison fix: subnormals and
  signed zeros compared as the VR4300 does, and per-game ROM fixes).
- Added: the benchmark window reports the time in the driver's submits and parallel-rdp's time off
  the CPU; RSP bursts are a VI's, not the window's total.
- Fixed: audio underruns are not counted while the quick menu holds the game.
- Added: the lockstep harness (`ps5/tests/run-lockstep.sh`): the original Gopher64 at the pin and
  the working tree run side by side on the PC (VR4300, RSP, RDRAM compared every VI and RSP burst;
  1.49 million single RSP instructions compared), the first difference named. Required before any
  interpreter change.

### The performance programme: a trustworthy benchmark (2026-11-26)

- Added: the benchmark from the board: HOME, □ Probes, △ on a probe runs it at 1x, 2x, 4x and 8x
  (twice, 10 s of the game each) and shows each factor's speed; no file or restart.
- Added: slow moments (BENCH, Video, on by default): a game below 95% for 3 s has its next 600 VIs
  measured into the boot log (`benchmark window slow moment:`), without stopping it.
- Fixed: a benchmark run in seconds could be ended early by an earlier run's timer.

- Added: the audit's Phase A documents (`docs/dev/perf/`).
- Changed: benchmark mode measures a window counted in VIs (`warmup_vis`, `measure_vis`) after which
  the game ends itself, so every upscale factor covers the same emulated VIs; `repeat` interleaves
  the sweep; `cache cold|warm`. Each run's summary splits a VI on one clock into work, present wait
  and the limiter, and the work into the RSP (less what it waited for), parallel-rdp (commands, GPU
  fence waits, syncs, the frame), RDRAM waits and the VR4300 with the rest, with frame-time
  percentiles and underruns. GPU timing tags native and upscaled work apart and times the upscale's
  Pre and Post steps. Nothing in the emulation itself changed.

### Interpreters only; the same scene at each upscale (2026-11-25)

- Decided (the owner's): Gopher64's VR4300 and RSP interpreters are a fixed requirement; no
  recompiler or JIT in any form. CPU-side work optimises the interpreters, measured on the console
  and tested against the original.
- Added: benchmark mode runs one probe at several upscale factors in turn (`state <slot>`,
  `upscale 1 2 4 8` in `benchmark.txt`) and ends each run with a `benchmark summary:` line. The
  logs' 1x-to-2x drop compared different scenes (2x only in a game's heavy opening, where 1x was 77%
  too); this measures it on one.

### From the eighth console log (2026-11-25)

- Measured: neither 32-wide waves nor parallel-rdp's 8/16-bit arithmetic changes the depth-blend
  pass (1.21 ms a pass at 8x either way); they stay off. The emulation thread alone takes ~21.5 ms
  a VI in that game at these settings (the RSP's interpreter 9 ms).
- Added: profile-guided builds of the core (Crux2's method): `PSGOPHER_PGO=generate` makes a
  profiling build whose counts the title writes to `/data/psgopher/pgo` after each game;
  `ps5/tools/merge-profiles.sh` makes `ps5/pgo/core.profdata`, which builds then use.

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
