# PSGopher performance audit (Phase A)

Date: 2026-11-26. Status: static audit only. **No change described here has been built or run on a
console, and the trustworthy baseline (Phase B) has not been run yet.**

> **Rule.** PSGopher keeps Gopher64's VR4300 and RSP interpreters. Nothing in these documents
> proposes, or may be read as proposing, a JIT, dynamic recompilation, block recompilers, cached
> native blocks, runtime code generation or a hybrid. Interpreter optimisations (dispatch,
> decoding, handlers, SIMD, memory access, data layout, compiler options, PGO) are in scope.

The set:

| Document | What it holds |
| --- | --- |
| [performance-audit.md](performance-audit.md) | This file: architecture, where the time goes, measured against inferred |
| [benchmark-analysis.md](benchmark-analysis.md) | The figures so far, the methodology faults, the Phase B protocol |
| [gpu-optimisation-plan.md](gpu-optimisation-plan.md) | parallel-rdp: depth blending, Pre/Post, runtime upscale change, 4x default |
| [interpreter-optimisation-plan.md](interpreter-optimisation-plan.md) | RSP then VR4300; the lockstep correctness method |
| [ps5-platform-optimisation.md](ps5-platform-optimisation.md) | Threads, priorities, waits, memory, compiler |
| [implementation-roadmap.md](implementation-roadmap.md) | Phases A-E, every item with problem, evidence, change, benefit, risk, benchmark |

Related: [../performance.md](../performance.md) (console observations),
[../open-work.md](../open-work.md), [../mupen64-ps5-comparison.md](../mupen64-ps5-comparison.md)
(the 2026-11-25 report), [../architecture.md](../architecture.md).

Evidence labels used everywhere: **STATIC** (read from code or disassembly of
`build/ps5/link/llvm-pie.elf`), **HOST-MEASURED** (run on the PC during the audit, scratch files in
`SCRATCH/perf-audit/`), **CONSOLE-MEASURED** (from the boot-log figures listed in
[benchmark-analysis.md](benchmark-analysis.md)). Each finding was checked by a second reviewer;
the verdict (confirmed / weakened) is given, and a weakened finding is stated in its corrected form.

## 1. The work of one VI

Everything that emulates runs on **one thread**, the emulation thread (the main thread; `play()` in
`ps5/host/main.cpp:220` runs synchronously, STATIC). Per VI (16.7 ms at NTSC) it does, in order of
appearance:

| Stage | Where | How it is timed today |
| --- | --- | --- |
| VR4300 interpreter | `cpu.rs:259-323` `run`, inlined into `gopher64::device::run_game` (0x2d25f0) | Not timed; inferred as CPU − RSP − RDP |
| RSP interpreter | `rsp_cpu::run`, inlined into `rsp_interface::do_task` (0x2ff490) | TSC wall around `rsp_cpu::run` (`rsp_interface.rs:512-516`), less RDP started inside |
| RDRAM syncs | `rdp_check_rdram` (`interface.cpp:1339-1358`) from CPU memory accesses and RSP DMA | `psgopher_bench_rdram_wait` (wait only) |
| parallel-rdp command processing | `interface.cpp` (single-threaded), `rdp_renderer.cpp` | TSC wall around entry points (`interface.cpp:1274, :1528, :1588, :1693`) |
| GPU work | parallel-rdp compute shaders on RADV | parallel-rdp's own timestamps, the `gpu:` line, 600-present windows |
| Present and speed limiter | `vi.rs:292-300` spin_sleep; FIFO present | present wait in the live figures; limiter sleep not timed |

Other threads during a game (STATIC, `ps5-platform` P1): audio (`ps5/host/platform.c:496`),
parallel-rdp's FenceExecutor (`rdp_device.cpp:49`), pipeline compile workers
(`rdp_renderer.cpp:78-87`), RADV's submit-kick thread (`radv_ps5_platform.c:751`), optionally RADV's
threaded-recording worker (`title_settings.cpp:207`), the Tokio runtime (`psgopher.rs:322`), the web
job thread (`web.cpp:246`) and, in benchmark mode only, the bench thread (`bench.c:377`). **None has
a priority or affinity set** (grep, STATIC). Details in [ps5-platform-optimisation.md](ps5-platform-optimisation.md).

### The interpreters in brief (STATIC)

- **VR4300**: pre-decoded. Each emulated icache line holds its 8 words and 8 handler pointers
  (`cache.rs` ICache, 0x68 bytes, filled by `icache_fill` through `decode_opcode`). Per guest
  instruction the loop stores gpr[0]=0, translates the PC, checks the icache tag, makes one indirect
  handler call, takes a jump table on `branch_state`, increments COP0 Count in memory and compares
  with `next_event_count`. About **52 host instructions** outside the handler on the kseg0 hit path
  (reviewer's count; the specialist's 55-63 was corrected). Simple handlers are ~9 instructions, 3 of
  them the frame-pointer prologue/epilogue. Loads and stores call `memory::data_read`/`data_write`
  out of line; `data_read` (0x319ad0) pushes six callee-saved registers even on a hit.
- **RSP**: pre-decoded at IMEM write and DMA time (`rsp_interface.rs:141, 204`). About 35 x86
  instructions of loop per RSP instruction: an indirect handler call (0x2ff5a5), a jump table on
  `branch_state.state` (0x2ff5a7-0x2ff5b5), a bounds check (0x2ff57e) and memory traffic for PC and
  the pairing fields. Vector handlers are already tight VEX/AVX code (vmudh 31-33 instructions); no
  pdep/pext/gather in 0x2f0000-0x320000.
- **parallel-rdp**: runs on the emulation thread; command processing is single-threaded
  (`interface.cpp`). GPU work is submitted in batches (`maintain_queues`,
  `rdp_renderer.cpp:2483-2496`), with a ring of 32 sync states (`rdp_data_structures.hpp:372`).

## 2. Where the time goes: measured against inferred

| Claim | Type | Status |
| --- | --- | --- |
| 1x: 93-100% after the opening, emulation thread 11-16 ms/VI, parallel-rdp 0.5-1.2 ms; 77% (21.3 ms/VI) in the heavy opening | CONSOLE-MEASURED | Single runs, 10 s windows; see [benchmark-analysis.md](benchmark-analysis.md) |
| At 1x the interpreters dominate the emulation thread | Inferred | The VR4300/RSP split at 1x has not been reported |
| 8x: 52-70%, 17-21.5 ms/VI, RSP 6-9 ms, parallel-rdp 5-13 ms wall, mostly "draw" waiting on render-pass fences | CONSOLE-MEASURED | Overclock on; mixed clocks (below) |
| 8x GPU: depth blending 12.1-16.2 ms per frame over 8-12 "passes" | CONSOLE-MEASURED | The "passes" are dispatches of two kinds (GPU-1) |
| The GPU's timed work at 8x (~18 ms per VI) exceeds the budget, so the GPU is at least one limit | CONSOLE-MEASURED + STATIC | **Weakened (GPU-3)**: 18 ms with ideal overlap predicts ~93%, not 52-70%; the 18 ms (build 45cc28d) and the 52-70% (build 892ab0a) are from different runs |
| 8x is purely GPU-bound; synchronisation work cannot help | — | **Not established.** Untimed GPU work or CPU/GPU serialisation may explain the gap |
| The RSP figure (6-9 ms at 8x) is interpreter time | — | **No**: it includes RDRAM waits from RSP DMA (RSP-2, confirmed); `rsp_ms − rdram_ms` is a lower bound available today |
| "VR4300 + rest ~2-5 ms" at 8x | Derived | Biased low: CPU is thread-CPU time, RSP/RDP are TSC wall time including sleeps (P2, M4); the 8x CPU figure also contains the emulation thread's own winsys spin |
| wave32 and 8/16-bit arithmetic do not change depth blending | CONSOLE-MEASURED | Confirmed by the console A/B only (GPU-7 corrected) |

### 2.1 GPU at 8x (STATIC unless marked)

1. **Two dispatches per qualifying pass (GPU-1, confirmed).** PSGopher never sets
   `COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_READ_BACK_BIT` (`interface.cpp:540-556`), so
   `caps.super_sample_readback` is false (`rdp_device.cpp:202`) and every render pass runs the 1x
   pass; passes on RGBA5551/8888 framebuffers at least 256 wide also run the upscaled pass
   (`rdp_renderer.cpp:3104-3157`). Both register the tag `"depth-blending"` (`:2340-2343`). The
   "1.21 ms per pass" in [../performance.md](../performance.md) is per dispatch, averaging cheap 1x
   and expensive 8x dispatches. The render-pass count should be *roughly* independent of the factor
   (flush limits scale with S², but tile rounding makes it approximate).
2. **Sample-major upscaled RDRAM (GPU-2, weakened).** Sample s of a native pixel lives in plane s,
   8 MiB apart (`memory_interfacing.h:286-358`). In `depth_blend.comp`'s `init_tile`/`finish_tile`,
   an 8x8 workgroup at 8x touches 192 distinct 128-byte lines against 24 at 1x (HOST-MEASURED with
   `SCRATCH/perf-audit/dblines.py`, re-checked). **But** those are 2 accesses per pixel; the
   per-primitive loop (`depth_blend.comp:115-138`) reads ~10 bytes per pixel per binned primitive,
   coalesced, from buffers that scale with S² × overdraw. Which part dominates the 12-16 ms is
   **unknown**.
3. **Pre/Post passes (GPU-4, confirmed).** At S>1, `update_upscaled_domain` Pre runs for **every**
   render pass (including non-upscaled off-screen ones), Post only for upscaled ones. Neither is timed
   separately (only SSAAResolve is, `rdp_renderer.cpp:2153-2159`).
4. **Tags overlap.** "render-pass"/"render-pass-upscaled" contain the depth-blending and shading
   intervals; adding every tag double-counts (reviewer, STATIC). How the ~18 ms total was obtained
   should be checked.

### 2.2 CPU side at 8x

- The emulation thread blocks in `flush_queues` on `internal_sync[...].fence` once 32 instances are
  in flight (`rdp_renderer.cpp:3080-3094`), timed as `"render-pass-fence"`. That wait reaches
  `radv_ps5_queue_wait_seq`, which pause-spins up to 1.5 ms and then sleeps 1 ms at a time
  (`radv_ps5_winsys.c:101-149`). The spin counts in the thread-CPU figure; the sleep does not
  (reviewer's addition to P2, STATIC).
- RDRAM syncs from CPU and RSP accesses wait on a condvar fed by the FenceExecutor
  (`rdp_device.cpp:1092-1097, :1259`): GPU → FenceExecutor poll → condvar → emulation thread (P3,
  confirmed).

### 2.3 CPU side at 1x

At 1x the GPU is light (parallel-rdp 0.5-1.2 ms CPU, CONSOLE-MEASURED) and the interpreters take
most of 11-21 ms/VI. How that splits between VR4300 and RSP is **not measured**. This is where the
interpreter work of [interpreter-optimisation-plan.md](interpreter-optimisation-plan.md) and the
platform work of [ps5-platform-optimisation.md](ps5-platform-optimisation.md) would show.

## 3. What is unknown, and what settles it

| Unknown | Console measurement that settles it |
| --- | --- |
| Per-VI GPU busy time (first to last timestamp in a frame context) at 8x | Phase B with the GPU tags of GPU-1/GPU-4 and `render-pass-fence` in the same run |
| Share of depth-blend time from the sample-major layout | Microbenchmark, or an A/B build with `init_tile`/`finish_tile` stubbed or coalesced (GPU-2) |
| VR4300 time | A TSC span around the VR4300 loop less nested RSP/RDP (M4) |
| RSP interpreter time and ns per RSP cycle | RSP cycles, bursts and RDRAM waits inside RSP per VI (RSP-2) |
| Whether helper threads share the emulation thread's core | Log each thread's placement and priority (P1) |
| nanosleep granularity on the console | `psgopher_probe` sleep histogram (P3) |
| Whether "unlimited" runs were really unlimited | Log the limiter state; a 119.88 Hz run above 60 VI/s (M3) |

## 4. Verdict counts

| Specialist | Confirmed | Weakened | Refuted |
| --- | --- | --- | --- |
| GPU (GPU-1..7) | 2 | 5 | 0 |
| RSP (RSP-1..8) | 4 | 4 | 0 |
| VR4300 (CPU-1..9) | 3 | 6 | 0 |
| Platform (P1..7) | 5 | 2 | 0 |
| Methodology (M1..8) | 8 | 0 | 0 |

No whole finding was refuted; several sub-claims were, and are listed in the "Refuted" section of
each plan.
