# PSGopher: porting analysis

The engineering assessment of a native PS5 port of [Gopher64](https://github.com/gopher64/gopher64)
(GPL-3.0), written before any console run. Date: 2026-11-24.

Every statement carries one of these labels:

| Label | Meaning |
| --- | --- |
| **[source]** | Read in the code at the pinned revision (Gopher64 `439e6c11f69d`, master of 2026-11-24, v1.2.4) |
| **[console: X]** | Measured on a PS5 by project X, with its evidence named; not yet re-measured by PSGopher |
| **[here]** | Measured or built in this repository, on the build PC (not on a console) |
| **[hypothesis]** | Reasoned, not measured; to be tested |
| **[unknown]** | Not known; says what would settle it |

Nothing in this document has run on a console yet.

## 1. What Gopher64 is

**[source]** About 15,000 lines of Rust for the machine (`src/device/`), 9,000 for the frontend
(`src/ui/`, Slint GUI, netplay, RetroAchievements), plus C/C++ through `build.rs`:

| Part | Where | Shape |
| --- | --- | --- |
| VR4300 (R4300i) | `device/cpu.rs`, `cpu_instructions.rs`, `cop0/1/2.rs`, `tlb.rs`, `cache.rs` | Interpreter. Function-pointer dispatch; decoded handlers are kept per instruction-cache line (`ICache.instruction[8]`), so a hit skips the decode. Instruction and data caches are emulated; one instruction is one cycle plus memory costs (`cop0::add_cycles`) |
| Events/timing | `device/events.rs`, `vi.rs` | Cycle-stamped event queue on COUNT; VI fields scheduled from line timing; a speed limiter paces each field with `spin_sleep` and `Instant` |
| RSP | `rsp_cpu.rs`, `rsp_su_instructions.rs`, `rsp_vu_instructions.rs` | Interpreter on the CPU's thread, run to a sync point when the CPU starts it; vector unit in SSE (`__m128i`, AArch64 through sse2neon); pipeline pairing (SU/VU) counted |
| RDP | `parallel-rdp/` (C++) | Themaister's parallel-rdp (Gopher64's own branch), Vulkan compute; RDRAM shared with the GPU, a per-range tracker (`rdram_tracker.hpp`) decides when the CPU must wait for the GPU |
| VI | parallel-rdp's `video_interface.cpp` | VI filtering, scaling and deinterlacing on the GPU |
| AI/PI/SI/MI/RI, PIF, carts, paks | `device/*.rs` | Plain Rust |
| Audio out | `ui/audio.rs` | SDL3 `SDL_AudioStream`: resampling from the AI rate, a frequency ratio for drift, gain |
| Input | `ui/input.rs`, `input_profile.rs` | SDL3 gamepads/joysticks/keyboard; profiles in `config.json`; controllers matched by joystick path |
| Saves, states | `ui/storage.rs`, `savestates.rs` | Saves written by `tokio::fs::write`; states serialised (postcard) and zipped |
| Frontend | `ui/gui.rs` (Slint over winit/wgpu), `netplay.rs` (ggrs, matchbox WebRTC), `retroachievements.rs` (rcheevos, reqwest), Discord presence | Desktop/Android only |

**Threads [source]:** the emulated machine (CPU, RSP, RDP command parsing, VI, audio queueing) runs
on **one thread**. Others: a Tokio runtime (file writes, the FPS counter, RetroAchievements),
parallel-rdp's own worker threads, and SDL's audio thread. Gopher64 sets
`PARALLEL_RDP_SINGLE_THREADED_COMMAND=1`: RDP command recording happens on the emulation thread.

## 2. The PS5 environment (what exists, from the stack's own records)

| Fact | Label |
| --- | --- |
| Titles are native x86-64 ELFs for `x86_64-sie-ps5`, built with host clang through the payload SDK fork's wrappers, signed into a fake SELF | [console: PS5_Vulkan, every title] |
| No system Vulkan: RADV (Mesa) is linked into the title; Vulkan 1.4, `VK_KHR_display` on VideoOut, FIFO present, 3840x2160 | [console: PS5_Vulkan `docs/RADV_PHASE.md`, `HARDWARE_FINDINGS.md`] |
| Queues: graphics/compute/transfer families all served by the graphics ring; no asynchronous compute | [console: PS5_Vulkan `docs/CTS_GAPS.md`] |
| `VK_EXT_external_memory_host` works: anonymous memory takes GPU access through `sceKernelMprotect`; a compute shader wrote through the CPU's address | [console: PS5_Vulkan HARDWARE_FINDINGS 2026-09-27] |
| Host-visible memory is cached and coherent both ways (16 MiB read 0.93 ms, written 1.08 ms) | [console: PS5_Vulkan HARDWARE_FINDINGS 2026-09-27] |
| A title's threads run on 13 CPUs (0-12, mask 0x1fff); `sysconf` says 16 | [console: PS5_PayloadSDK `platform/docs/PROBE.md`] |
| Page size 16 KiB (`sysconf(_SC_PAGESIZE)`) | [console: PROBE.md] |
| MXCSR starts at 0x9fe0 (FTZ+DAZ) | [console: PROBE.md] |
| Default pthread stack 64 KiB; the link recipe's wrap gives 2 MiB | [console: PROBE.md] |
| Emulated TLS: 4.7 ns per `thread_local` read vs 1.3 ns global | [console: PROBE.md] |
| Writes: ~1.3 GiB at ~242 MiB/s, then ~2 MiB/s; `write()` costs ~3.3 ms each; stdio is slow | [console: PROBE.md "Files", "Sustained writes"] |
| ~249 files open by path at once | [console: PROBE.md "Open files"] |
| 12 GiB direct memory, 448 MiB flexible (up to 1 GiB requested in `param.json`) | [console: PROBE.md] |
| JIT code: direct memory mapped RW then `sceKernelMprotect` to RWX; protection change ~26 µs | [console: PROBE.md] |
| Rust `std` runs in a title: a custom FreeBSD-derived target with emulated TLS, `-Zbuild-std` | [console: OGBox, its DSP56300 JIT (`ps5/tools/build-deps.sh`)] |
| Zen 2 code (`-march=znver2`, AVX2) runs | [console: Crux2's paraLLEl-GS (`ps5/pgs`), PS5_RetroArch's FFmpeg `--cpu=znver2`] |
| parallel-rdp itself runs on RADV on the console (Mupen64Plus-Next, PS5_RetroArch); at 8x the GPU stayed busy ~10x as long as at 4x (the submit thread waited 2.4 s of every 10 vs 0.2 s); 4x is that title's default | [console: PS5_Mupen64Plus `7cd4414`] |
| OGBox's timing measurements (TSC ~733.3 MHz in its guest, `Sleep(1)` ~1.07 ms) are OGBox's environment's, not constants | [console: OGBox; not re-used here] |

## 3. Built here so far

| What | Result | Label |
| --- | --- | --- |
| RADV release archive, PS5 Mesa `1ba9874` (PS5_Vulkan's pin) | built, 260 MiB archive | [here] |
| Payload SDK fork `88d6e41` (newest, over v0.45) | installed | [here] |
| Gopher64's whole library for `x86_64-sie-ps5` (Rust nightly 2026-11-23, `-Zbuild-std`), parallel-rdp/volk/rcheevos through the SDK's clang | **compiles**, `libgopher64.a` 24 MiB, 41 emulated-TLS variables | [here] |
| The archive's imports vs the SDK's console link stubs | only 7 names have no stub; 5 are bound to the platform layer by PS5_Vulkan's link recipe; `getrandom` and `dl_iterate_phdr` need stand-ins | [here] |

## 4. The twelve questions

### 1. Can Gopher64 realistically be ported natively to the PS5?

**Yes.** Every hard dependency has a proven route on this console: Rust std (OGBox), parallel-rdp on
RADV (PS5_Mupen64Plus), host-memory import (PS5_Vulkan), AVX2 (Crux2). The core already compiles
for the target [here]. What remains is the platform layer (display, pad, audio, files, exit) and
the link, both of which have worked examples. The open risk is performance of the single emulation
thread on Zen 2 (question 6), which no one has measured for Gopher64.

### 2. Reusable without modification

**[source]+[here]** The whole machine (`src/device/`), save/state code, cheats, config, input
profiles, the audio drift controller, the VI pacing logic, parallel-rdp and its RDRAM tracker,
rcheevos, every pure-Rust crate (serde, postcard, zip, sevenz, sha2, rand, regex, rustc-hash,
fatfs, clap). The SDL-facing Rust (`ui/video.rs`, `audio.rs`, `input.rs`) also stays unchanged:
PSGopher implements the SDL3 calls it makes instead of rewriting it (PS5_ARCHITECTURE.md).

### 3. Needing platform-specific replacements

| Component | Replacement |
| --- | --- |
| SDL3 (window, Vulkan surface, events, gamepads, audio stream, timers) and SDL3_ttf | `ps5/sdl3`: the ~90 SDL3/SDL3_ttf functions Gopher64 calls, implemented over the PS5 layer (`VK_KHR_display`, scePad, sceAudioOut, stb_truetype); the real `sdl3-sys` bindings are kept with `no-link` |
| Vulkan loader (volk `dlopen`) | `SDL_Vulkan_GetVkGetInstanceProcAddr` returns `vk_icdGetInstanceProcAddr` (RADV linked in) |
| Slint GUI | not built (`--no-default-features`); a native ROM browser comes later; first milestone boots a ROM named in a file |
| `dirs`, `current_exe` | fixed folders under `/data/psgopher/` (patch, one `cfg`) |
| mimalloc | not used: Rust's System allocator goes to `malloc`, which the link recipe wraps to the platform heap in direct memory |
| Process entry/exit | the title's `main` calls `psgopher_main`; the shell exit, never `exit()` |
| Netplay (WebRTC), RetroAchievements/Discord (network) | compiled, never started: networking is unproven on the console (platform contract). Off by default upstream too |

### 4. Is parallel-rdp viable on the PS5?

**Yes, with evidence.** It already runs on this RADV in PS5_RetroArch [console: PS5_Mupen64Plus].
Its fastest RDRAM path needs `VK_EXT_external_memory_host` with `minImportedHostPointerAlignment`
alignment, which the console supports [console: PS5_Vulkan]. Gopher64 allocates RDRAM 64 KiB
aligned [source]; the console's page is 16 KiB, so the alignment holds [hypothesis: the driver
reports ≤64 KiB; read `minImportedHostPointerAlignment` at boot]. Remaining questions are
performance ones: no asynchronous compute (all on the graphics ring), and upscaling cost
(4x vs 8x measured by PS5_Mupen64Plus).

### 5. Best graphics backend strategy

Keep parallel-rdp unchanged on RADV through `VK_KHR_display`. No high-level renderer, no rewrite:
it is the accuracy reference, it runs, and the driver is a full Vulkan 1.4. Changes are confined
to the surface (KHR_display plane), the loader entry point, and present mode (FIFO only;
parallel-rdp asks MAILBOX and falls back to FIFO [source: `interface.cpp`]). Anything beyond that
(e.g. scanout straight into the swapchain image, upscale defaults) must be measured first
(PS5_GRAPHICS_BACKEND.md).

### 6. Is the existing interpreter adequate?

**[unknown]**, the project's biggest risk. The VR4300 interpreter, the RSP interpreter and RDP
command recording share one thread [source], with cache emulation on every fetch. Upstream ships
x86-64 and Android/AArch64 builds, which suggests it is fast enough on desktop and recent phone
cores, but that is not a measurement. The PS5's Zen 2 runs at up to 3.5 GHz [hypothesis: as Sony
states; to confirm with a cycle-count probe], below current desktop clocks. Settled by:
the benchmark mode (PS5_PERFORMANCE.md) on the console, emulated-cycles per host-second per game.

### 7. Would an x86-64 dynarec help?

**[hypothesis]** Probably for the VR4300, but not first. Gopher64's accuracy comes from its
cycle and cache model; a recompiler must reproduce COUNT, cache, TLB and exception timing per
instruction, which removes much of the usual gain. The RSP is the better JIT candidate (fixed
4 KiB IMEM, microcode reused frame after frame; ParaLLEl-RSP's JIT runs on this console in
PS5_RetroArch). Decision rule: only after the console benchmark shows the emulation thread is the
limit, and only for the unit that dominates its time.

### 8. Biggest expected CPU bottlenecks [hypothesis]

1. The serial emulation thread: VR4300 + RSP interpreters back to back. 2. RSP vector ops in heavy
microcode (audio and graphics tasks). 3. `rdp_check_rdram` waits on the GPU timeline when the CPU
touches RDRAM the RDP has pending (CPU/GPU round trips). 4. Dispatch through function pointers
and the per-fetch TLB/cache checks. 5. Emulated-TLS reads, if any hot path uses `thread_local`
(4.7 ns each [console: PROBE.md]). To be measured, not assumed.

### 9. Biggest expected GPU bottlenecks [hypothesis]

1. Upscaling: 8x cost ~10x the GPU time of 4x on this console [console: PS5_Mupen64Plus].
2. Everything on one ring: RDP work, VI scanout and present serialise. 3. Readbacks when the CPU
reads framebuffers (the tracker's waits). 4. SSAA downscale and the CRT shader, if enabled.

### 10. PS5-specific memory and scheduling work worth investigating

Ranked in PS5_PERFORMANCE.md. In short: pin the emulation thread to its own core and keep
parallel-rdp's workers and audio off it; check that RDRAM is imported, not copied (host coherent);
keep RDRAM and hot state in direct memory pages that are never re-protected; no dirty-page tracking
by protection faults (Gopher64 uses an explicit range tracker, so the 16 KiB-granularity problem
OGBox met does not arise [source]); batch save writes through `write()` in large chunks.

### 11. Minimum viable native build

One title (`PPSA99280`, provisional) that: logs to klog, hides the splash, opens the display
through `VK_KHR_display`, initialises parallel-rdp, reads a ROM path from
`/data/psgopher/autostart.txt`, runs it with the pad on player 1 and audio through sceAudioOut,
saves to `/data/psgopher/data/saves`, and exits through the shell. That is Milestone 2/3 in
PS5_ARCHITECTURE.md.

### 12. Most realistic path to full compatibility and 60 FPS

1. Boot unchanged Gopher64 (no accuracy changes), 2. measure per game on the console with the
benchmark mode, 3. fix the platform's costs first (thread placement, GPU waits, upscale default
from measurement), 4. only then work inside the emulator's hot loops, one change at a time,
5. a recompiler (RSP first) only if 2-4 leave games below full speed. Compatibility is upstream's
from the start, as long as nothing in the machine is changed; tracking upstream keeps it so.

## 5. Optimisations, ranked

Impact and complexity are estimates; none is measured. "Confidence" is that it will help at all.

| # | Optimisation | Impact | Complexity | Confidence | Gate |
| --- | --- | --- | --- | --- | --- |
| 1 | Emulation thread pinned alone on a core; parallel-rdp workers, audio, Tokio elsewhere | medium | low | medium | frame-time variance before/after |
| 2 | Upscale default chosen from measurement (start at 4x, as PS5_Mupen64Plus measured) | high (GPU) | low | high | GPU busy time per frame |
| 3 | Confirm host-coherent RDRAM import at boot; log if parallel-rdp fell back | high if fallen back | low | high | boot log line |
| 4 | Build with `target-cpu=znver2` instead of upstream's `x86-64-v3`, and PGO as Crux2 does | low-medium | low | medium | benchmark mode |
| 5 | RSP vector unit: profile the hottest ops; AVX2 where SSE needs several ops | medium | medium | medium | per-op profile |
| 6 | Fewer `rdp_check_rdram` waits (find which ranges block) | medium | medium | low-medium | wait counter |
| 7 | RSP JIT (ParaLLEl-RSP precedent) | high if RSP-bound | high | medium | only if RSP dominates |
| 8 | VR4300 block recompiler | high if CPU-bound | very high | low | only if CPU dominates and accuracy holds |
