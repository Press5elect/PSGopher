# PSGopher: architecture

How the port is put together, and why. Decisions are numbered (D1...) and referenced from code
comments and the changelog. Labels as in ../history/porting-analysis.md.

## The layers

```text
  eboot.bin (PPSA99280, provisional)
  ├── ps5/host/            the title: main(), folders, the sandbox exit, the shell exit       C/C++
  │   ├── main.cpp         start, the boot log, the menu <-> game loop                         C++
  │   ├── platform.c       PS5 Vulkan Template's PS5 layer (MIT): klog, splash, pads, audio    C
  │   ├── crash.c          the crash report in the boot log (Crux2's)                          C
  │   ├── rust_abi.c       FreeBSD 12 -> 11 converters for Rust's stat/fstat/readdir/kevent     C
  │   ├── menu.cpp         the front end's display, textures, pad and loop; share screens (D17) C++
  │   ├── frontend.cpp     the Live Board: HOME, RACK, BENCH, panel, pickers (D17)             C++
  │   ├── board.cpp        the mainboard: projection, labels, moving parts (D17)               C++
  │   ├── quickmenu.cpp    the in-game quick menu and stats overlay (D17, D11)                 C++
  │   ├── web.cpp          covers and descriptions: fetch thread and cache (D18)               C++
  │   ├── image.cpp        PNG decoding (stb_image)                                             C++
  │   ├── library.cpp      the games found on the console, USB drives and shares (D16)        C++
  │   ├── title_settings.cpp  psgopher.ini: display, HDR10, stats, the picture's options (D14) C++
  │   ├── game_settings.cpp   a game's own settings (config/games/<key>.ini), applied around a game C++
  │   ├── bench.c          benchmark mode and the live stats (D11)                             C
  │   ├── sandbox.cpp      out of the sandbox, OGBox's (D15); ps5/elevation: its helper       C++
  │   └── shares.cpp       the network shares as the front end edits them, OGBox's (D15)      C++
  ├── ps5/vfs/             ps5vfs: SMB, NFS and FTP mounts as paths (Crux2's, by OGBox) (D15)  C++
  ├── ps5/sdl3/            libpsgopher_sdl3.a: the SDL3 + SDL3_ttf functions Gopher64 calls,  C++
  │                        on the PS5 layer, RADV's VK_KHR_display and stb_truetype
  ├── libgopher64.a        Gopher64 (vendor/gopher64 at the pin + ps5/patches/gopher64)       Rust
  │   ├── src/device/      the machine, unchanged
  │   ├── src/ui/          unchanged; calls SDL3 through the real sdl3-sys bindings (no-link)
  │   ├── src/psgopher.rs  psgopher_main(): the entry the title calls (the patch adds it)
  │   └── parallel-rdp/    compiled by Gopher64's build.rs with the SDK's clang++              C++
  ├── RADV, Crux2's       PS5 Mesa at PS5_Vulkan's pin + Crux2's patches (ps5/patches/mesa-27)  C/C++
  └── libps5platform.a     PS5_PayloadSDK's platform layer (heap, threads, libc gaps)         C
```

RADV is Crux2's archive, the same driver Crux2 ships (D13). Linking is PS5_Vulkan's `tools/radv-link.sh`, unchanged (heap and thread wraps, libc bindings,
whole-archive RADV).

## Decisions

**D1. Upstream stays upstream.** Gopher64 is fetched at a pinned revision into `vendor/gopher64`
(gitignored) and changed by one patch, `ps5/patches/gopher64/ps5.patch`, exported with
`ps5/tools/fetch-gopher64.sh --export` (Crux2's model for PCSX2). The patch only gates: target
tables in `Cargo.toml`, where `build.rs` finds SDL's headers, the folders, the entry point. No
emulation code is touched. Moving the pin is rebasing one small file.

**D2. Implement SDL3, don't remove it.** Gopher64 calls about 90 SDL3/SDL3_ttf functions
[here: `nm -u` of the core]. Replacing those call sites would scatter PS5 code through
`ui/*.rs` and `parallel-rdp/interface.cpp` and conflict with every upstream change. Instead
`ps5/sdl3` implements exactly that subset with SDL's own documented semantics, against SDL's
headers at the release the bindings were generated for (3.4.20). Functions that only the
desktop configuration paths reach (keyboard profile editor, SDL renderer) exist and fail
politely (`SDL_SetError`, NULL/false), so nothing calls through NULL. A full SDL3 PS5 backend
(video/audio/joystick drivers in SDL itself) was considered: more code to verify, the whole SDL
build through cmake for the target, and no feature Gopher64 uses that the subset lacks.

**D3. The PS5 is a target, not a feature.** A custom target `x86_64-sie-ps5`
(`ps5/rust/x86_64-sie-ps5.json`: OGBox's proven spec, vendor `sie`, `cpu znver2`, frame pointers
always) selects the PS5 in Cargo (`cfg(target_vendor = "sie")`) and, through `build.rs`, a
`psgopher` cfg for the few source lines that need one (Gopher64 denies warnings, and a custom
vendor value would trip `unexpected_cfgs`).

**D4. One emulation thread, as upstream.** The title's main thread runs Gopher64's `run()` and so
the machine, as on the desktop. The pad is read on that thread (`SDL_PumpEvents`, called by
Gopher64 once a frame). Audio plays from the PS5 layer's own thread (256-frame grains, 48 kHz),
pulling from the `SDL_AudioStream` ring. Tokio's whole runtime is refused by the console
(EOPNOTSUPP, 2026-11-25): `psgopher::build_runtime` builds its scheduler and timers only (no I/O
driver), which is all Gopher64 needs here (file writes, timers); the USB cartridge's TCP link is off. Thread placement is a measured optimisation (performance.md), not
a default yet.

**D5. Display.** `SDL_Vulkan_CreateSurface` makes a `VK_KHR_display` plane surface on the
display's mode; the window size Gopher64 sees is the mode's extent (3840x2160). parallel-rdp's WSI
asks for MAILBOX and falls back to FIFO, the console's only mode [source + console: PS5_Vulkan].
The title asks for 59.94 Hz (no high-frame-rate bits): NTSC N64 fields are 59.94 Hz, so FIFO
paces 1:1; PAL's 50 Hz alternates 1 and 2 refreshes (the console has no 50 Hz mode).

**D6. Audio.** `SDL_AudioStream` is implemented with SDL's semantics: `Queued` counts unconsumed
input bytes, `Available` playable output, the frequency ratio speeds consumption. The resampler is
a windowed-sinc polyphase filter from the stream's rate (the AI's, ~32-48 kHz) times the ratio to
48 kHz. Gopher64's own drift controller (`ui/audio.rs`) then works as on the desktop.

**D7. Input.** Each signed-in user's DualSense is an SDL gamepad (players 1-4 from the PS5 layer,
paths `ps5:pad<n>`), with SDL's standard layout: Cross = South, Circle = East, Square = West,
Triangle = North, L1/R1 shoulders, L2/R2 trigger axes, Options = Start, the touch pad click = Back
(Gopher64's hotkey; the Create button is the shell's). First boot assigns pad n to port n with the
default profile, through Gopher64's own `assign_controller`. Hot-plug: a user signing in or out
becomes `SDL_EVENT_JOYSTICK_ADDED/REMOVED`, which Gopher64 already handles.

**D8. Files.** `/data/psgopher/` (reachable over FTP; folders 0777, files 0666):
`config/`, `data/saves`, `data/states`, `cache/`, `roms/`. The shader caches (RADV's Mesa cache,
parallel-rdp's) go to `/data/psgopher/cache`. Writes are few and small (saves, states) and
already go through `write()` in whole files; nothing further until profiling says so (OGBox's
throttling lesson applies to bulk writes, which Gopher64 does not make).

**D9. Floating point.** Gopher64 sets FTZ/DAZ itself from FCR31 on the emulation thread
[source: `cop1::set_host_flush_mode`, at init, on FCR31 writes and state loads], so the console's
FTZ start state is corrected exactly where it matters. Every other thread keeps the console's
state; none computes guest floating point.

**D10. Gopher64's online features off.** Netplay, RetroAchievements and Discord compile (pure
Rust/C) but are never started: they need Tokio's I/O driver, which the console refuses (D4), and
they are off unless configured or asked for on the command line. The title's own network use is
the shares (D15) and the covers and descriptions (D18), both on blocking sockets.

**D11. Instrumentation from the start.** `ps5/host/bench.c`: presents and their spread, time
blocked in present, the emulation thread's CPU time, `rdp_check_rdram` waits, audio underruns,
one klog line a second and a CSV written once at the end, enabled by
`/data/psgopher/benchmark.txt`; a probe of std, Tokio and the clocks by `probe.txt`
([`performance.md`](performance.md)). Both are test files the launch reads, never defaults. Always on
and cheap: the live stats (`psgopher_live_read`, fed by the same hooks), the `video:` line in the boot
log every 10 s (VIs, speed against the target, the game's own frame rate, CPU and present time a VI,
RDRAM syncs, audio queue and underruns), the RDRAM path parallel-rdp took, and the quick menu's stats
overlay (`stats = on` in `psgopher.ini`).

**D12. Link.** PS5_Vulkan's recipe unchanged, with Crux2's `--no-dynamic-linker -z
nodynamic-undefined-weak` (Mesa's weak entry points must not become imports), and PSGopher's own
`--defsym` bindings kept local (`accept4`, `gai_strerror`, `realpath` to the platform layer;
`getrandom`, `dl_iterate_phdr`, `pthread_setname_np`, `recvmmsg` to `ps5/host/rust_stubs.c`).
The four in `rust_stubs.c` belong in the platform layer with host tests; they stay here until
PS5_PayloadSDK takes them. zstd is built with `ZSTD_TRACE=0` (its weak hooks would be imports).

**D13. Crux2's RADV.** PSGopher links the RADV Crux2 builds and plays on: PS5 Mesa at PS5_Vulkan's
pin with Crux2's driver series (`ps5/patches/mesa-27` in Crux2: Mesa main 27.0-devel merged in,
the GFX10.1 hardware workarounds, the shader-cache and timed-wait fixes, the VideoOut changes),
built by Crux2's own `ps5/tools/build-radv-crux2.sh` into PS5_Vulkan's `.deps/native/radv-crux2`.
The patches live in Crux2 only. `link-title.sh` refuses to link without that archive, and
`check-sources.sh` stops the build when the archive's patch list differs from Crux2's current one.
The series' opt-in features (variable refresh, HDR10, streamed zeroing) stay off: PSGopher sets
none of their switches.

**D14. Variable refresh and HDR10, behind settings.** Both come from Crux2's RADV (patches 0015-0017
and 0020) and need the title's side, done as Crux2 does it. `param.json` declares the 119.88 Hz mode
(`attribute3` 0x80040) and HDR10 (`attribute` 0x20000000); `ps5/host/videoout_hooks.cpp` answers the
driver's three hooks by finding `sceVideoOutVrrUnpegFromFixedRate`, `sceVideoOutSubmitChangeBufferAttribute2`
and `sceVideoOutGetOutputStatus` by name or NID while the title runs (Crux2's lookup; a title that
imported them would not start on a firmware without them). `/data/psgopher/config/psgopher.ini`
chooses: `display = 60` (default; `PS5_VIDEOOUT_59HZ`), `120`, or `vrr` (`PS5_VIDEOOUT_VRR`: each frame
shown when ready, which also ends PAL's 50 Hz judder); `hdr = off` (default) or `on`, with
`hdr_white` (203) and `hdr_peak` (1000). HDR10 is on only while asked for, the console's output is
HDR and VideoOut took the framebuffers (Crux2's rules, in `interface.cpp` under `PSGOPHER`): the
frame's picture and its messages are drawn into two layers, and `ps5/shaders/psgopher_hdr.frag`
(Crux2's light: gamma 2.2, BT.709 to BT.2020, white and highlights, PQ) writes each 10:10:10:2 word's
bytes into the B8G8R8A8 swapchain image. Both are off by default until a console run shows them
working (the owner's rule for what no console has tested).

The picture's options (BENCH > Video, `psgopher.ini`, a game's page) reach `interface.cpp` as
`PSGOPHER_*` variables read once a game by `psgopher_picture_init`: the final blit's filter (Sharp
Bilinear is `ps5/shaders/psgopher_sharp.frag`, drawn with a linear sampler; Bilinear and Nearest use the
plain blit with the matching sampler; the CRT shader keeps Nearest), the video interface's `vi.*`
options in `ScanoutOptions`, `RDP::Quirks` for the texture rectangles, `crop_overscan_pixels` (added to
the widescreen letterbox's `crop_rect` when that is on), `downscale_steps` and Bob or Blend
deinterlacing. Bicubic and Lanczos are `ps5/shaders/psgopher_resample.frag` (compiled twice, the second
with `LANCZOS`), drawn in the blit's place. Post Anti-Aliasing (`psgopher_fxaa.frag`) and Sharpening
(`psgopher_cas.frag`), when either is on (`PSGOPHER_POST_AA`, `PSGOPHER_SHARPENING`), draw the picture
with its filter into a layer of the screen's size first (`psg_post.a`), FXAA into a second (`psg_post.b`)
when CAS follows, and the last pass into the frame's target (the swapchain, or HDR10's SDR picture layer
before the PQ pass), before the messages; with both off the frame is drawn as before. Downsampling's steps are independent of the upscale, and SSAA downscaling still means
all of them (`psgopher_downscale_steps`: SSAA wins, else the row's steps, never past log2 of the upscale).

A game's own settings (`game_settings.cpp`) are BENCH's rows, one number each, in
`config/games/<key>.ini` (`gameKey`: the header's game code with destination and revision, or the file's
name with no header facts). `GameScope` applies them around `play()` in `main.cpp`: the title's rows
go into a copy of `TitleSettings` that `applyHdr` is given (the global ones are never changed), and
Gopher64's rows through `psgopher_setting_set`, with the globals journalled in `config/games/restore.txt`
until the game ends (`recoverGameSettings` at the title's start puts a game that never ended right).

**D15. Storage everywhere, as OGBox and Crux2.** A title's sandbox shows only part of `/data` and no
USB drive. Before anything starts a thread (klog's capture is the first), `main` asks the bundled
helper (`sandbox-elevator.elf`, OGBox's `ps5/elevation` with PSGopher's title ID; sent to the
payload loader on port 9021) for the console's whole file system (`ps5/host/sandbox.cpp`, OGBox's
unchanged), finds the title folder again (it is no longer `/app0`), and from then on gives the
driver the folder's `param.json` (`PS5_VIDEOOUT_PARAM_JSON`; `/dev/null` at 60 Hz, as OGBox) and
ps5vfs its stand-in file. `elevation-off.txt` in the title folder keeps it in the sandbox (BENCH,
This console writes it). Network shares are Crux2's ps5vfs as OGBox has it (`ps5/vfs`: SMB through
libsmb2, NFS through libnfs, mounted at `/smb/<name>`) with OGBox's share list
(`ps5/host/shares.cpp`): `/data/psgopher/config/network.cfg`, written by the front end as shares are
added, changed or removed, mounted at the start. Gopher64 reads its ROM with `std::fs`, which knows
nothing of ps5vfs: a share's ROM is copied to `/data/psgopher/cache/play` (one at a time; a ROM is
at most 64 MiB) and that path is played. The console holds a title's own writes back after about
1.3 GiB per process (OGBox measured 192 MiB/s, then 12-32 MiB/s; a payload of its own kept 200 MiB/s
past 4 GiB), so with `write_helper` on (default) and a destination under `/data/` the copy is written
by OGBox's write broker (`ps5/broker`, `write-broker.elf`, sent to the payload loader on port 9021
like the elevation helper, launched once a run at the first copy; `ps5/host/write_broker.cpp`): the
bytes go to it in 16 MiB messages, then SYNC, and the file's size is read back. When it is off, not
there or fails at any step, the title writes the copy itself, as before. OGBox's fast writes ran on
the owner's console in OGBox; PSGopher's use of them has not yet run on one. The broker's COPY
(it reads a source it can open itself) is in the payload but unused: a share's bytes only the title's
ps5vfs can read. Saves are named by the ROM's hash, so they are the same
wherever the ROM is. On the console: the helper and the file layer run in OGBox and Crux2 [console];
with PSGopher's title ID [unknown] until the first run.

**D16. The game library.** Every ROM found (`.z64 .n64 .v64`, a `.zip` whose first file is one, a
`.7z` in a folder named for the N64 or in `roms/`): the console's `/data` (`/data/psgopher/roms`
first), `/mnt/usb0-7`, `/mnt/ext0-1` and every share, each searched five folders deep on a thread,
with a budget per place (`ps5/host/library.cpp`). The list is kept in `cache/library.tsv` (with each
ROM's header facts: game ID, lockout chip, save type, pak, CRC key) so a start shows it at once, and
searched again in the background; play time is `cache/played.tsv`. The front end (D17) draws it on a
Vulkan device of its own (volk, RADV, the display's plane surface through `ps5/sdl3`), made before
the menu and ended before the game, so Gopher64's parallel-rdp makes its own as on the desktop; the
display (VideoOut) stays open across both. Settings are each one's own file, so they persist and
agree: Gopher64's are its `config.json`, read and written through Gopher64's own `Config`
(`psgopher_setting_get/set` in `psgopher.rs`), PSGopher's are `psgopher.ini`
(`ps5/host/title_settings.cpp`). Refresh takes effect at the next start (the display opens once);
HDR10 at the next game, and a game turns HDR10 off as it ends, so the menu is SDR. A game ends with
the quick menu's Eject or Gopher64's quit (OPTIONS with the touch pad) and the menu comes back;
Gopher64 runs again in the same process, as its Android build does. The host tests draw the menu
offscreen through the PC's Vulkan with a scripted pad (`ps5/tests/test_menu.cpp`, PSGOPHER_MENU_HOST).

**D17. The front end is the Live Board.** The design the owner chose (`ps5/art/live-board.html`)
is the specification: its stage (1920x1080, doubled at 4K), its board canvas (2000x1250), its CSS 3D
pose per view, its colours, faces and timings. The board's still layer is rendered from the design
itself (`make-board-art.mjs`, 4000x2500, mipmapped on upload) and drawn as a 41x26 grid through the
same transform (translate, perspective 2600 px, rotateX, rotateZ, scale about the centre), eased
between poses with the design's cubic-bezier(.2,.8,.2,1); the moving parts (`board.cpp`, the
design's drawDynamic) are drawn in board pixels and projected with it. Labels are the design's
generator, clipped as polygons so one label can be flat or seated in the slot. Everything else is
Dear ImGui draw lists (`frontend.cpp`), the pad read directly. The panel shows what Gopher64 makes
of the cartridge (D16's facts), not guesses. In a game the quick menu (`quickmenu.cpp`) is the
design's LIVE panel, drawn over the frame through parallel-rdp's device. Cheats are Gopher64's own database
(`psgopher_cheats_of`/`psgopher_cheat_set` in `psgopher.rs`, keyed by the header's CRCs); the region's
flag is drawn from the header's country code; covers and descriptions are D18.

**D19. Probes and their pictures.** A probe is a save state, one per slot 1-9:
`data/states/<name>-<hash>.state<slot>` (`ui/storage.rs` makes the path; `psgopher::set_game` tells
the quick menu its stem; `probesOf` in `library.cpp` and the quick menu list them by it).
`savestates::create_savestate` (the emulation thread, at a frame boundary, after `rdp_idle`) takes
the state; the file is written by a Tokio task. When a probe is taken the core also writes **its
picture**, one file beside it, read once at that moment (the game is waiting anyway):

- Path: the probe's path plus `.png` (`psgopher::probePicturePath` in `ps5/host/probe_picture.h`),
  e.g. `data/states/<name>-<hash>.state3.png`. Nothing else in `data/states` ends in `.png`; the
  probe's own name still ends in its slot's digit.
- Content: a PNG, 8-bit RGB, no alpha. The pixels are what the VI outputs at the N64's own
  resolution: parallel-rdp's scanout with the upscaling averaged away again (`downscale_steps` = the
  upscale's steps), no letterbox crop, no overlay, no board. Typically 640x480 (640x240 for a
  non-interlaced 320x240 game): the pixels are not square, show it at 4:3. Not scaled to anything.
- Written atomically: `<png>.tmp` then `rename`, so a reader sees the old picture or the new one.
  Overwriting a probe replaces its picture; if the picture cannot be read (blank screen, no video
  yet) the probe is saved all the same and the old picture is removed (it would show another
  moment). Nothing in the title deletes probes; a tool that does removes
  `removeProbePicture(state)` with it.
- A probe may have no picture (taken before this existed, or a blank screen): the reader checks the
  file and draws its fallback. The PNG uses stored deflate blocks (no library; 0.9 MB at 640x480).
- Cost: one scanout and readback of the native image (about 0.3 MB, at the scale's downscale) plus the
  PNG's encoding (about 5 ms on a PC) and write, once per save; the core logs
  `probes: picture <w>x<h> written in <ms> ms`, or `probes: picture not taken: <why>`.
- The seam: `rdp_probe_picture` (`parallel-rdp/interface.cpp`) reads with
  `ScanoutOptions::snapshot`, which leaves the VI's history (previous frame, layout, counters) as it
  was, then calls the host's `psgopher_probe_picture_write` (`quickmenu.cpp`); both in
  `parallel-rdp.patch` and `ps5.patch`.

**D18. Covers and descriptions from the network.** A cartridge's label is its box art from the
libretro-thumbnails N64 set (No-Intro names, from GitHub: thumbnails.libretro.com answered 404 for
every N64 name, 2026-11-25), its panel three lines of its Wikipedia summary (search, then the first
result described as a video game, as OGBox does). `ps5/host/web.cpp` asks for what the front end
shows, newest first, on one thread, and keeps everything in `/data/psgopher/cache/covers` (a `.none`
file when there is none, asked again after 30 days; a minute's rest after a network failure). The
fetches are the core's (`psgopher_cover`, `psgopher_about` in `psgopher.rs`): rustls over a blocking
`std::net::TcpStream` with webpki-roots' certificates, because Tokio's sockets are refused (D4) and the
console has no certificate store. Covers become textures one per frame, at most 48 kept. Not yet run
on a console.

## Milestones

| # | Deliverable | Proof |
| --- | --- | --- |
| M1 | Gopher64's library cross-compiles for the target | [here] done 2026-11-24 |
| M2 | The title links through `radv-link.sh` and packages to `eboot.bin` with every import resolved | [here] done 2026-11-24: 351 imports, all from modules a title loads |
| M3 | On the console: launch, klog, splash, display, pad detected, a tone through audio, shell exit | done 2026-11-25 (the owner's boot logs): also the sandbox exit, an SMB share, USB drives |
| M4 | A ROM boots and plays: video, audio, pad, saves | in progress: a PAL game plays, at 51-85% of full speed (mostly ~56%) with choppy audio (2026-11-25; [`performance.md`](performance.md#first-console-observations)) |
| M5 | Benchmark mode numbers per game, baseline recorded | console runs |
| M6+ | One optimisation at a time, kept only with a measured gain | performance.md entries |
