# Performance implementation roadmap (phases A-E)

> **Rule.** PSGopher keeps Gopher64's VR4300 and RSP interpreters. No item here, or added later, may
> be a JIT, dynamic recompilation, block recompiler, cached native blocks, runtime code generation
> or a hybrid. Behaviour changes no console has checked go behind default-off settings.

Sources: [performance-audit.md](performance-audit.md), [benchmark-analysis.md](benchmark-analysis.md),
[gpu-optimisation-plan.md](gpu-optimisation-plan.md), [interpreter-optimisation-plan.md](interpreter-optimisation-plan.md),
[ps5-platform-optimisation.md](ps5-platform-optimisation.md); also [../performance.md](../performance.md),
[../open-work.md](../open-work.md), [../mupen64-ps5-comparison.md](../mupen64-ps5-comparison.md).

Order follows the owner's priorities: trustworthy baseline → parallel-rdp GPU (depth blending) →
CPU/GPU synchronisation → RSP interpreter → VR4300 interpreter → PS5 platform → PGO benchmarked;
plus the 4x default evaluation and the runtime upscale change.

| Phase | Content | State |
| --- | --- | --- |
| A | Static audit (these documents) | Done 2026-11-26; nothing built or run |
| B | Instrumentation, then the trustworthy baseline on the console | **Not run** |
| C | GPU: depth blending, Pre/Post, CPU/GPU synchronisation; runtime upscale change; 4x evaluation | After B |
| D | Interpreters: RSP, then VR4300, with the lockstep harness | After B (can overlap C) |
| E | Platform (threads, waits, compiler) and PGO, each benchmarked | After B; PGO last, on the final code |

Every change: one at a time, against the same baseline build in the same session, three interleaved
runs each, kept only with a gain larger than the run-to-run spread and no accuracy change.

## Top five

1. **B1 — Benchmark windows in emulated VIs, one clock, ratios of sums** (M1, M2, M4).
2. **B2 — GPU tags separated (1x vs upscaled, Pre/Post, render-pass-fence, GPU busy per VI)** (GPU-1, GPU-4, M5).
3. **B4 — Run the Phase B baseline** on three probes, 1x/2x/4x/8x, three interleaved repeats.
4. **C1 — Decide the depth-blend mechanism, then the pixel-major layout if it pays** (GPU-2).
5. **C3 — Reconcile the 8x gap and decide on CPU/GPU synchronisation work** (GPU-3, P2, P3).

## Phase B — trustworthy baseline

### B1. Benchmark harness fixes
- **Problem**: the summary averages boot zeros and stale teardown seconds; runs are wall-clock from
  the event watch, so factors cover different scenes; clocks are mixed; VR4300 not measured.
- **Evidence**: M1, M2, M4 (confirmed). STATIC `bench.c:311-355, :157-169`; `main.cpp:181-195,
  :326-330`; `interface.cpp:629`. HOST-MEASURED: exact 100% run reads 72% with 8 s boot + 3 s teardown.
- **Change**: `warmup_vis`/`measure_vis` from the first present after the probe; totals on the
  emulation thread; TSC for every part (fence waits, RDRAM waits flagged in RSP, limiter sleep, wall
  between presents); optional VR4300 span; no logging on the emulation thread inside the window
  (queue to the bench thread); per-batch instead of per-command RDP hook (M8).
- **Benefit**: removes a bias of up to tens of points; makes factors comparable.
- **Risks**: none to emulation (host counters); the VR4300 span must only read the TSC.
- **Benchmark**: sweep with old and new summary side by side; wall ≈ sum of parts within ~2%.

### B2. GPU instrumentation
- **Problem**: 1x and upscaled dispatches share tags; Pre/Post untimed; GPU windows unaligned; tags
  overlap.
- **Evidence**: GPU-1 (confirmed), GPU-4 (confirmed), M5 (confirmed); `rdp_renderer.cpp:2340-2343,
  :2153-2159, :3085-3091`; `interface.cpp:1298-1316`.
- **Change**: `-upscaled` tags; `upscale-pre`/`upscale-post`; print render-pass, render-pass-upscaled,
  render-pass-fence; reset and print the log at window edges; GPU busy per VI as first-to-last
  timestamp; reset the static counter at `rdp_init`; cache `getenv`; log calibrated-timestamp support.
- **Benefit**: interpretable sweep. **Risks**: none (tags under PARALLEL_RDP_BENCH).
- **Benchmark**: 8x with gpu_timing on vs off (overhead within spread).

### B3. Limiter, repeats, settings, RSP counters
- **Problem**: "unlimited" can be silently undone and is capped by FIFO; one run per factor; settings
  unrecorded; RSP time includes waits.
- **Evidence**: M3, M6, M7 (confirmed), RSP-2 (confirmed); `interface.cpp:475-476, :697`;
  `wsi_common_videoout.c:34-35`; `rsp_interface.rs:161, :186`; `bench.c:166, :269-279`.
- **Change**: absolute limiter-off request after `rdp_init`, logged; `repeat K` interleaved;
  `cache cold|warm`; settings in summary and CSV; pad-event counter; frame-interval percentiles;
  RSP cycles/bursts/`rsp_rdram_ms` per VI; guest instructions and idle-skipped cycles per VI.
- **Benefit**: spread to judge gains; 1x headroom. **Risks**: restore user settings after the run.
- **Benchmark**: a 119.88 Hz unlimited 1x run above 60 VI/s confirms the limiter is off.

### B1-B3 status (implemented 2026-11-26, not yet run on a console)

Done: windows in VIs from the game's first present (`warmup_vis`, `measure_vis`), totals on the
emulation thread in TSC ticks with one conversion (wall = work + present wait + limiter; work split
into RSP less its RDP and RDRAM waits, parallel-rdp commands/fence waits/syncs/frame, other RDRAM
waits, the rest), no logging inside the window, the RDP hook once a batch (M8), RSP bursts and
cycles, frame-interval percentiles, audio underruns; GPU tags native/upscaled apart, Pre and Post
timed, the GPU log reset and read at the window's edges, the frame counter reset each game and the
switch read once; `unlimited` absolute from `rdp_init`; `repeat`, `cache cold|warm`. Host-tested
(`ps5/tests/test_bench.c`: each part within 0.15 ms of known work).

Not done: guest instructions and idle-skipped cycles a VI (a counter in the interpreter's loop: left
out so the measurement does not change the hot path), GPU busy as first-to-last timestamp, the
calibrated-timestamp support line, a pad-event counter. The VR4300's time is the remainder.

### B4. Run the baseline
- **Protocol**: [benchmark-analysis.md](benchmark-analysis.md) §4: probes A (heavy opening), B (in
  game), C (CPU framebuffer writes); 1x/2x/4x/8x; 3 interleaved repeats; overclock off (plus an 8x
  on/off pair); warm cache (plus one cold); the wait-policy, shaderstats and probe runs.
- **Acceptance**: CV < ~2% per factor; parts sum to wall; GPU busy ≤ wall. Recorded in
  [../performance.md](../performance.md) with the build ID.
- **Before B4, at no cost**: read `rdram_ms` and the `rdp:` line from existing 1x and 8x logs.

## Phase C — parallel-rdp GPU and synchronisation

### C1. Depth blending: layout decision, then pixel-major layout
- **Problem**: depth blending is 12.1-16.2 ms/frame at 8x (CONSOLE-MEASURED); the sample-major
  layout makes `init_tile`/`finish_tile` uncoalesced (192 vs 24 lines per workgroup, HOST-MEASURED).
- **Evidence**: GPU-2 (weakened): only 2 scattered accesses per pixel; the per-primitive loop is
  coalesced and scales with overdraw; the layout's share is unknown.
- **Change**: step 1 — A/B build with init/finish coalesced (timing only), or the console
  microbenchmark, or time against binned primitives per tile; step 2 — pixel-major layout across
  every reader/writer ([gpu-optimisation-plan.md](gpu-optimisation-plan.md) G4), default-off.
- **Benefit**: unknown; ceiling part of 12-16 ms at 8x, less at 2x, none at 1x.
- **Risks**: **needs visual and correctness testing**: bit-exact scanout and extracted RDRAM on RDP
  command dumps at 2x/4x/8x.
- **Benchmark**: `depth-blending-upscaled` per dispatch, GPU busy per VI, speed.

### C2. Pre/Post
- **Problem**: Pre runs every pass at S>1; Post on upscaled passes; cost unknown; S²-growing in
  CPU-drawn scenes. **Evidence**: GPU-4 (confirmed).
- **Change**: after B2 shows their cost — C1's layout makes Pre's copies contiguous; skip Post when
  the 1x pass wrote nothing (default-off). **Benefit**: unknown; likely small in 3D scenes.
- **Risks**: skipping Post needs correctness testing (stale reference). **Benchmark**: probe C at 8x.

### C3. CPU/GPU synchronisation
- **Problem**: at 8x the emulation thread waits in `flush_queues` on render-pass fences; 18 ms/VI of
  timed GPU work predicts ~93%, measured 52-70% (different builds).
- **Evidence**: GPU-3 (weakened), P2 (weakened), P3 (confirmed); `rdp_renderer.cpp:3080-3094,
  :2483-2496`; `radv_ps5_winsys.c:101-149`.
- **Change**: first reconcile with B2 data (GPU busy per VI, render-pass-fence, untimed VI scale/
  downscale/present). If serialisation shows: submit heuristics in `maintain_queues`, the sync-state
  count, or wait-policy env settings (X3) — each default-off. If purely GPU-bound: no sync work.
- **Benefit**: unknown; at most the 93%-vs-measured gap. **Risks**: none to the image (host timing).
- **Benchmark**: 8x sweep with SPIN 1500/200/0 and SLEEP 1000/250/100; render-pass-fence ms vs GPU
  total − 16.7 ms.

### C4. Runtime upscale change
- **Problem**: a factor change needs a restart; the sweep needs it in one session on one scene.
- **Evidence**: GPU-6 (weakened); `interface.cpp:540-597, :715-741, :1360-1408, :1386, :670-672`;
  `video.rs:201-208`; ~4.5 GiB at 8x.
- **Change**: `rdp_set_upscale` (idle, save, delete first, new processor, load with the new factor in
  `gfx_info`, replay VI registers, HDR init), from the quick menu, default-off.
- **Benefit**: a feature, not speed; a visible pause per switch (unmeasured).
- **Risks**: missed state → black frame, stale textures, crash; OOM if the old processor is not freed.
- **Benchmark**: repeated 1x↔8x switches; time per switch; GPU memory; save/load right after.

### C5. Evaluate 4x as the default
- **Problem**: no 4x figure exists. **Evidence**: none yet (Phase B).
- **Change**: decide on the criteria in [gpu-optimisation-plan.md](gpu-optimisation-plan.md) §5
  (full speed on the probes, frame-interval p99, no underruns, GPU headroom, memory, a 30 min run,
  no visual regression); re-evaluate after C1.
- **Risks**: a default that drops below full speed in heavy scenes. **Benchmark**: Phase B data at 4x.

## Phase D — interpreters (lockstep harness first)

### D0. Correctness harness
VR4300 per-VI lockstep trace, RSP per-burst differential, exhaustive handler tests, test ROMs, image
hashes ([interpreter-optimisation-plan.md](interpreter-optimisation-plan.md) §1; open task 20).

### D1. RSP run loop (RSP-1, weakened)
- **Problem**: two indirect branches, a bounds check, a double PC store and pairing-field memory
  traffic per RSP instruction. **Evidence**: do_task 0x2ff57e, 0x2ff5a5, 0x2ff5a7-0x2ff5b5,
  0x2ff5b7/0x2ff520; llvm-mca 18.1 → 14.1 (no branches modelled).
- **Change**: R1 in the interpreter plan. **Benefit**: unknown, probably low (branches well
  predicted). **Risks**: break/sync_point/cycle ordering; mfc0 PC rewind.
- **Benchmark**: RSP net ms/VI and ns per RSP cycle (B3), 1x, old vs new.

### D2. RSP small items
mtc2 even elements (RSP-5), unaligned vector load/store SIMD (RSP-6, only after a histogram),
`-Ztune-cpu=znver2` (RSP-7). All weakened: benefit unknown. Not doing: match dispatch (RSP-4).
Separate correctness item: DMEM-end wrap in ssv/slv/sdv (RSP-8).

### D3. VR4300
In order: cold dcache miss paths (V3, CPU-5 confirmed) → `#[inline(never)]` run with disassembly
check (V4, CPU-3 weakened) → kseg0 same-line fetch fast path (V1, CPU-1 weakened, ~15-20 of ~52
instructions) → COP1 direct pre-decode (V6, CPU-6 confirmed) → per-line RDRAM check (V5, CPU-4
weakened; needs counters and an 8x image check) → icache layout (V8). Each: lockstep, then VR4300
ms/VI at 1x on probe A, three runs. Benefit unknown for all; matters most at 1x.

## Phase E — platform and PGO

### E1. Thread placement and priorities (P1, weakened)
Log placement first; port Crux2's priorities/layout only if sharing is seen; default-off; check save
writes under 730-priority Tokio. Benchmark: 1x and 2x, CPU ms/VI, speed, underruns.

### E2. Wait policy (P3, confirmed)
Covered by C3's env sweep; set from `title_settings.cpp` only if it helps.

### E3. Compiler target and frame pointers (P4 confirmed, X6)
znver2 tuning A/B; non-leaf frame pointers only after `crash.c` reads the return address at `mc_rsp`.
Benchmark: 1x CPU and RSP ms/VI, three runs.

### E4. PGO, benchmarked (CPU-2, RSP-3, P5 — confirmed)
- **Problem**: built, never used (`ps5/pgo/` empty). **Evidence**: `build-core.sh:45-67`.
- **Change**: generate on the console over several games and probes (1x, 8x, opening, in game,
  RSP-heavy); merge; build `use`; check mismatches with warnings on.
- **Benefit**: unknown. **Risks**: none to semantics; unrepresentative or stale profile.
- **Benchmark**: off vs use, three runs each, including a probe not in the training set. Done last
  so the profile matches the final code, and re-collected when the pin moves.

## Not on the roadmap

- Anything recompiler-like (rule above).
- RSP match dispatch (RSP-4): no gain on the host.
- Wave size or 8/16-bit arithmetic for depth blending: ruled out by the console A/B (GPU-7).
- Emulated TLS and per-thread-index costs: verified non-issues (P7).
