# PSGopher and the PS5 port of Mupen64Plus-Next: what each does, and what the console showed

Written 2026-11-25. A study of the PS5 port of Mupen64Plus-Next (a local checkout of the PS5 fork,
its PS5 commits and build options) set beside PSGopher's console logs. Nothing was taken from its
code; this report records what it does and what was learned from it. Games are named by what the
logs say of them, not by title.

## In short

- **The CPU side is where the two differ most.** Mupen64Plus-Next on the PS5 *recompiles* both of the
  N64's processors to x86-64 (its `new_dynarec` for the VR4300, ParaLLEl-RSP's JIT for the RSP).
  Gopher64, PSGopher's core, *interprets* both, on every platform: upstream has no recompiler.
  PSGopher runs Gopher64's x86-64 path (the RSP's vector unit is native SSE/AVX in the title's
  code); there is no faster x86 path left out.
- **At 8x both reach the same limit: the GPU.** Mupen's PS5 port measured 8x keeping the GPU about
  ten times as busy as 4x, saw frames dropped with crackling sound, and made 4x its PS5 default.
  PSGopher's GPU timing found parallel-rdp's depth-blending pass taking 12-16 ms of each frame at 8x
  in a demanding game, the whole of a 16.7 ms frame.
- **What follows for PSGopher:** 4x as the default with 8x kept as a choice (Mupen's measured
  answer); the resolution changed while a game runs (Mupen does it); and, for the CPU side, an RSP
  recompiler is the change of real size. Profile-guided optimisation would only trim the
  interpreters.

## What Mupen64Plus-Next's PS5 port does

From its PS5 commits (2026-09-28 to 2026-09-30) and its `platform=ps5` build:

| | Mupen64Plus-Next (PS5) | PSGopher (Gopher64) |
| --- | --- | --- |
| N64 CPU (VR4300) | recompiled: `WITH_DYNAREC = x86_64` (`new_dynarec`) | interpreted |
| RSP | recompiled: ParaLLEl-RSP (a JIT, `HAVE_PARALLEL_RSP = 1`) | interpreted, its vector unit in SSE/AVX intrinsics |
| JIT code memory | direct memory the payload SDK fork's platform layer maps read-write-execute (`ps5platform/exec.h`); anonymous memory made executable did not work on the console | none needed |
| RDP | parallel-rdp | parallel-rdp (Gopher64's fork) |
| parallel-rdp's command processing | on parallel-rdp's own worker thread | on the emulation thread ("single-threaded"): Gopher64's RDRAM and texture tracking needs it |
| Waiting for the GPU | "synchronous": at each full sync | at RDRAM syncs and when 32 render passes are in flight |
| Supersampled read-back | off | off |
| Default upscale on the PS5 | **4x** (was 8x; changed after measuring) | the owner's choice (8x in the logs) |
| Upscale changed in a game | applies at the next frame (the command processor is made again once the GPU is done with the old one) | applies at the next game |

Its commit making 4x the default, in substance: users saw games drop frames at 8x with the sound
crackling; on the author's console 8x kept the GPU about ten times as busy as 4x (the submitting
thread waited 2.4 s of every 10 at 8x against 0.2 s at 4x), leaving no margin.

## What PSGopher's console logs showed

One demanding NTSC game, measured with the boot log's `video:` and `gpu:` lines (builds `2ab7c1d`
to `03758be`, 2026-11-25). A VI is 16.7 ms at full speed.

| Setting | Speed | Emulation thread a VI | Of which |
| --- | --- | --- | --- |
| 1x | 93-100% (77% in heavy opening scenes) | 11-16 ms | the interpreters |
| 2x | 81-84% | 18.6-20.8 ms (the game's opening) | parallel-rdp 1.6-1.7 ms |
| 8x supersampled, overclock on | 52-70% | 17-21.5 ms | RSP 6-9 ms; parallel-rdp 5-13 ms, mostly waiting for the GPU; CPU interpreter ~2-5 ms |

The GPU at 8x, a frame (`gpu:` lines, parallel-rdp's own timestamps):

| Stage | GPU time a frame |
| --- | --- |
| depth-blending | 12.1-16.2 ms (8-12 render passes, **1.21 ms a pass**) |
| shading | 0.3-1.5 ms |
| binning, span setup, VI scale and fetch, VRAM extract | ~1.5 ms together |

Ruled out on the console, each by measurement:

- the general (unspecialised) shading pipeline: 0% of dispatches used it;
- compiling shaders on more threads or at once: no change;
- 32-wide compute waves and parallel-rdp's 8/16-bit arithmetic: 1.21 ms a depth-blend pass either way;
- RDRAM's mapping: memory granted to the GPU stays cached for the CPU (PS5_Vulkan's hardware findings);
- the driver's GPU wait: it spins at most 1.5 ms before sleeping.

## What it means

1. **8x is the GPU's limit in heavy games, for both emulators.** parallel-rdp's depth-blending at 8x
   (2560x1920 for a 320x240 game, 64 times the pixels) costs about as much as a whole frame on the
   PS5's GPU when a game draws ten or more passes a frame. Mupen's port reached the same conclusion
   by measurement and defaults to 4x (a quarter of the pixels).
2. **The CPU side is the interpreters.** Mupen gets its speed from recompilers; Gopher64 has none.
   At 1x the interpreters fit a VI, barely, in the heaviest game measured. Profile-guided
   optimisation (built locally, `PSGOPHER_PGO`, off by default) would give a modest gain; a
   recompiler, of the RSP first (the larger share), is the change that would give margin.
3. **parallel-rdp's threading** differs for a reason: Gopher64 processes RDP commands on the
   emulation thread for its RDRAM tracking, so in PSGopher that work and the GPU waits count against
   the emulation thread. Moving it to a worker as Mupen does would change Gopher64's accuracy design.

## Recommendations

| | What | Why | Size |
| --- | --- | --- | --- |
| 1 | 4x the default upscale; 8x kept as a choice | measured in both emulators: 8x overloads the GPU in heavy games | small |
| 2 | The upscale changed while a game runs (quick menu), as Mupen does | try 8x and 4x per game without restarting it | medium |
| 3 | An RSP recompiler for Gopher64, then possibly the CPU | the interpreters are the CPU side's cost; Mupen's speed comes from recompiling | large: a plan first |
| 4 | Profile-guided build of the core | trims the interpreters | built; needs one profiling session on the console |

The details of each measurement are in [`performance.md`](performance.md#first-console-observations);
the open items in [`open-work.md`](open-work.md).
