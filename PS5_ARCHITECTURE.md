# PSGopher: architecture

How the port is put together, and why. Decisions are numbered (D1...) and referenced from code
comments and the changelog. Labels as in PS5_PORTING_ANALYSIS.md.

## The layers

```text
  eboot.bin (PPSA99280, provisional)
  ├── ps5/host/            the title: main(), folders, the ROM to boot, the shell exit        C/C++
  │   └── platform.c       PS5 Vulkan Template's PS5 layer (MIT): klog, splash, pads, audio    C
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
pulling from the `SDL_AudioStream` ring. Tokio's workers are limited (`TOKIO_WORKER_THREADS`) since
they only write files here. Thread placement is a measured optimisation (PS5_PERFORMANCE.md), not
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

**D10. Networking off.** Netplay, RetroAchievements and Discord compile (pure Rust/C) but are
never started: the console's networking is unproven for titles (platform contract), and they are
off unless configured or asked for on the command line.

**D11. Instrumentation from the start.** `ps5/host/bench.c`: presents and their spread, time
blocked in present, the emulation thread's CPU time, `rdp_check_rdram` waits, audio underruns,
one klog line a second and a CSV written once at the end, enabled by
`/data/psgopher/benchmark.txt`; a probe of std, Tokio and the clocks by `probe.txt`
(PS5_PERFORMANCE.md). Both are test files the launch reads, never defaults.

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

## Milestones

| # | Deliverable | Proof |
| --- | --- | --- |
| M1 | Gopher64's library cross-compiles for the target | [here] done 2026-11-24 |
| M2 | The title links through `radv-link.sh` and packages to `eboot.bin` with every import resolved | [here] done 2026-11-24: 351 imports, all from modules a title loads |
| M3 | On the console: launch, klog, splash, display, pad detected, a tone through audio, shell exit | a console run (owner) |
| M4 | A ROM boots and plays: video, audio, pad, saves | a console run per game (owner) |
| M5 | Benchmark mode numbers per game, baseline recorded | console runs |
| M6+ | One optimisation at a time, kept only with a measured gain | PS5_PERFORMANCE.md entries |
