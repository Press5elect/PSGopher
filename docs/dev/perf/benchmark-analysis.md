# Benchmark analysis and the Phase B protocol

> **Rule.** The VR4300 and RSP interpreters stay. No JIT, recompiler, cached native blocks or
> runtime code generation is proposed in this or any of the [perf documents](performance-audit.md).

**The trustworthy baseline (Phase B) has NOT been run yet.** The figures below are first
observations from single runs. They are useful for direction, not for measuring gains. See also
[../performance.md](../performance.md) and [../mupen64-ps5-comparison.md](../mupen64-ps5-comparison.md).

## 1. The figures so far (CONSOLE-MEASURED, one demanding NTSC game)

VI budget 16.7 ms. Sources: boot-log `video:` and `gpu:` lines.

| Factor | Speed | Emulation thread ms/VI | RSP | parallel-rdp (CPU side) | GPU | Caveats |
| --- | --- | --- | --- | --- | --- | --- |
| 1x | 93-100% after the opening; 77% in the heavy opening | 11-16; 21.3 in the opening | not reported | 0.5-1.2 ms | light | 10 s windows; limiter likely on |
| 2x | 81-84% | 18.6-20.8 | not reported | 1.6-1.7 ms | — | **Opening only**: not comparable with the 1x figure after the opening |
| 8x supersampled, overclock on | 52-70% | 17-21.5 | 6-9 ms | 5-13 ms wall, mostly "draw" | depth blending 12.1-16.2 ms/frame over 8-12 dispatches (1.21 ms each); shading 0.3-1.5; binning/span/VI/extract ~1.5 | Overclock on; GPU total (~18 ms) from build 45cc28d, speed from 892ab0a |
| 8x, RADV_PERFTEST=cswave32 or PARALLEL_RDP_SMALL_TYPES=1 | unchanged | | | | 1.21 ms per dispatch either way | Only A/B so far |

What the columns mean: "CPU" is `CLOCK_THREAD_CPUTIME_ID` of the emulation thread per VI; RSP and
RDP are TSC wall time around `rsp_cpu::run` (less RDP started inside) and around parallel-rdp entry
points (`bench.c:157-169`, STATIC).

### Caveats on these figures

- **Different scenes**: the 2x figures are from the opening only; the 1x figures are mostly after it.
- **Short windows**: 10 s `video:` windows and 600-present `gpu:` windows, not aligned to each other.
- **One run each**, no spread.
- **Overclock** differs between the 8x run and the others; overclock raises VR4300 work per VI by up
  to 4/3 for code that does not idle, and can raise RSP/RDP work when it lifts a lagging game's frame
  rate (CPU-9, weakened and corrected).
- **Different builds** for the 8x GPU total and the 8x speed (GPU-3, weakened).
- **Mixed clocks**: CPU − RSP − RDP understates VR4300 + rest (P2, M4).
- **"Per pass"** is per dispatch of two kinds, 1x and upscaled (GPU-1, confirmed).

## 2. Methodology audit (all eight findings confirmed)

| ID | Defect | Evidence |
| --- | --- | --- |
| M1 | The summary averages per-second snapshots from a clock that starts before boot. Boot seconds after the 5 s mark add zeros; teardown seconds add copies of the last window. An unsynchronised struct copy can mix two windows. | STATIC `bench.c:311, :336, :347-355, :204-219`; `main.cpp:326-330`. HOST-MEASURED (unmodified `bench.c` with stubs, `SCRATCH/perf-audit/bench-sim/sim.c`): an exact 100% run with 8 s boot and 3 s teardown reads "72% speed over 18 s"; with 0.5 s boot, 100%. The console's size of the bias depends on boot/teardown times, not yet measured. |
| M2 | Runs last a fixed number of wall seconds from parallel-rdp's event-watch registration (before Vulkan init and the probe load), so slower factors emulate less of the scene. | STATIC `main.cpp:181-195`; `psgopher_sdl3.cpp:308-312`; `interface.cpp:629, :664, :670` |
| M3 | "unlimited" is a toggle (event 4) that can land before `interface.cpp:697` forces the limiter back on, with no log; it is also a cross-thread write. FIFO is the only present mode, so throughput is capped at the display rate anyway. | STATIC `main.cpp:183-187`; `interface.cpp:475-476, :625, :697, :649-655`; `wsi_common_videoout.c:34-35`; `video.rs:250-252`. Whether the race happens is unmeasured. |
| M4 | Thread-CPU and TSC-wall figures are mixed; VR4300 time is not measured; RDRAM waits from RSP DMA are inside RSP time. | STATIC `bench.c:133, :157-169`; `radv_ps5_winsys.c:103-104, :145-148`; `rsp_interface.rs:162, :187` inside `:513-516` |
| M5 | GPU timestamp windows are 600 presents on a static counter never reset; not in the summary; `getenv` on every present; GPU busy per VI not measured. | STATIC `interface.cpp:1298-1316`; `query_pool.cpp:454-487`; `rdp_renderer.cpp:3085-3091` |
| M6 | The summary lacks the frame-time distribution, underruns, present/RDRAM/fence waits, compiles and spread; the CSV lacks the RSP/RDP split; speed target fixed at 60/50 rather than Gopher64's frame time. | STATIC `bench.c:188, :315-316, :338-342, :395-398`; `vi.rs:53-55` |
| M7 | No repeats, no interleaving, shader-cache warmth and speed-relevant settings (rewind, overlay, display mode, gpu_timing, shader_compile, driver_thread) not controlled or recorded; pad input not checked. | STATIC `main.cpp:131-170, :233-236, :318-331`; `title_settings.cpp:179-211`; `savestates.rs:125-137` |
| M8 | The per-command RDP hook (2 rdtsc + atomic add per command ≥ 8) is always on, not "two clock reads a frame". | STATIC `interface.cpp:1586-1591`; `bench.c:8, :107-111`. HOST-MEASURED ~40-45 ns per hook on a 2.1 GHz VM; console cost and commands/VI unknown. |

Also found by the reviewer (STATIC):

- **Synchronous log writes on the emulation thread** inside the window: the 10 s heartbeat `say()`
  from `psgopher_bench_present` and, with gpu_timing, the `gpu:` line every 600 presents
  (`platform.c:128-144`). `bench.c:7` itself says a `write()` costs ~3.3 ms on the console (a code
  comment, not measured here). These are periodic hitches caused by the instrumentation.
- The bench thread's once-a-second `say()` shares the stderr lock with the emulation thread;
  contention unknown.
- The limiter runs every `limit_freq` VIs (`vi.rs` ~319-325), so its sleeps come in bursts; log
  `limit_freq` when reading limited runs.
- The exit event (3) is also an unsynchronised cross-thread write (`callback.emu_running`).

## 3. Instrumentation needed before Phase B

Grouped; each is host-side counting or timing only, no change to emulation.

1. **Windows in emulated VIs** (M1, M2): `warmup_vis N`, `measure_vis M`, counted from the first
   present after the probe loads; totals kept on the emulation thread; ratios of sums at the end.
2. **One clock** (M4, P2): TSC around fence waits (`rdp_renderer.cpp:3088`, `rdp_device.cpp:253,
   1203, 1259`), RDRAM waits flagged when inside an RSP task and subtracted from RSP (RSP-2),
   limiter sleep, and per-VI wall between presents. Optional: a TSC span around the VR4300 loop
   less nested RSP/RDP.
3. **RSP counters** (RSP-2): RSP cycles per VI (`cycle_counter`, already computed), bursts per VI,
   `rsp_rdram_ms`.
4. **GPU tags** (GPU-1, GPU-4): `depth-blending-upscaled`, upscaled shading/binning, `upscale-pre`,
   `upscale-post`; print `render-pass`, `render-pass-upscaled`, `render-pass-fence`; per-VI GPU busy
   (first to last timestamp in a frame context); reset the log at window edges (M5); cache the
   `getenv`; log calibrated-timestamp support.
5. **Limiter** (M3): an absolute "limiter off" request applied after `rdp_init`; log the state used.
6. **Summary** (M6): frame-interval p50/p95/p99/p99.9/max over the whole window, counts over 1.5×
   and 2× the VI period, underruns (limited runs only), present/RDRAM/fence wait ms/VI, compiles in
   the window, general-pipeline share; new CSV columns appended at the end.
7. **Settings and repeats** (M7): `repeat K` interleaved, `cache cold|warm`, all speed-relevant
   settings in the summary and CSV header, pad-input count per window.
8. **No logging on the emulation thread** inside the window: queue lines to the bench thread.
9. **Per-command hook** (M8): per-batch timing, or only in benchmark mode; commands per VI counted.

## 4. The Phase B protocol (to run on the console)

### 4.1 Probes

- Probe A: a save state in the demanding NTSC game's **heavy opening**.
- Probe B: a save state **in game** (steady 3D).
- Probe C: a scene with **CPU framebuffer writes** (full-screen video or a transition), for Pre/Post.
- No pad input during a run (checked by the counter).

### 4.2 Fixed settings

Overclock **off** for the main sweep (one extra 8x pair with overclock on and off); rewind off;
overlay off; display mode 119.88 Hz; gpu_timing on (`PARALLEL_RDP_BENCH=2`) for the GPU runs, plus
one 8x pair on/off to measure its own overhead; shader cache warm (one unrecorded pass per factor
first) except for the explicit cold-cache run; default `RADV_PS5_WAIT_*`.

### 4.3 benchmark.txt (once the instrumentation above exists)

The keys below other than the existing ones are proposed in this audit and do not exist yet.

```
# Phase B: one probe per file
probe A
upscale 1 2 4 8
warmup_vis 600
measure_vis 3600
seconds 180            # safety timeout only
repeat 3               # interleaved: 1 2 4 8 1 2 4 8 1 2 4 8
cache warm
limited
```

Then the same with `unlimited` (headroom at 1x and 2x), and for probes B and C.

### 4.4 Extra runs

| Run | Purpose |
| --- | --- |
| 8x with `RADV_PS5_WAIT_SPIN_US=0` and `=100000` | How much of CPU and "draw" is spin or sleep rounding (M4, P3) |
| `RADV_PS5_WAIT_SLEEP_US=1000/250/100` at 1x and 8x | Wake latency on RDRAM waits (P3) |
| 8x overclock on and off | Overclock's host cost (CPU-9) |
| Cleared `/data/psgopher/cache/radv` vs warm | Cache-warmth effect on the first factor (M7) |
| `psgopher_probe` | nanosleep and `std::thread::sleep` granularity, `available_parallelism` (P3) |
| Boot-log timestamps "benchmark: run", "video: the first frame is shown", "N s are up", "Gopher64 returned" | Size of M1's bias in the current builds |
| `RADV_DEBUG=shaderstats` once at 8x | depth_blend and rasterizer VGPRs, waves per SIMD (GPU-7) |
| The boot log's `rdp:` line | Whether RDRAM is host-imported or copied (changes every conclusion about waits) |

### 4.5 What to record per run

Build ID, settings line, probe, factor, repeat index; VIs measured; speed (and true VI rate);
emulation-thread wall, thread-CPU, RSP net, RSP cycles and bursts, RDP net, VR4300 + rest (all one
clock), present wait, limiter sleep, RDRAM wait (CPU and RSP separately), fence wait; per-tag GPU
ms/frame and count (render-pass, render-pass-upscaled, depth-blending, depth-blending-upscaled,
shading, binning, upscale-pre, upscale-post, render-pass-fence) and GPU busy per VI; frame-interval
percentiles; game fps; underruns; compiles in the window; pad events.

### 4.6 Acceptance of the baseline

- Coefficient of variation per factor below about 2% over the three repeats, so changes of 3% or
  more can be claimed (M7).
- Per VI window, wall ≈ sum of the parts within about 2% (M4).
- Render-pass count per frame roughly the same at every factor (GPU-1).
- GPU busy per VI ≤ wall per VI (M5).
- The baseline is recorded in [../performance.md](../performance.md) with the build ID; every later
  change is measured against it in the same session ([implementation-roadmap.md](implementation-roadmap.md)).
