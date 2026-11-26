# GPU optimisation plan (parallel-rdp)

> **Rule.** The VR4300 and RSP interpreters stay; nothing here involves a JIT, recompiler, cached
> native blocks or runtime code generation. GPU changes keep the rendered image bit-identical unless
> marked as needing visual and correctness testing, and go behind a default-off setting until tested.

Context: [performance-audit.md](performance-audit.md) §2.1, [benchmark-analysis.md](benchmark-analysis.md),
[../performance.md](../performance.md), [../graphics.md](../graphics.md). Code paths below are in
`vendor/gopher64/parallel-rdp/` (Gopher64's glue `interface.cpp`) and its
`parallel-rdp-standalone/parallel-rdp/` (renderer and shaders). Patches go to `ps5.patch` through
`ps5/tools/fetch-gopher64.sh --export`.

## 1. Measure first (no image change)

### G1. Separate 1x and upscaled timings (GPU-1, confirmed)

- **Evidence (STATIC)**: no `SUPER_SAMPLED_READ_BACK` flag (`interface.cpp:540-556`) →
  `caps.super_sample_readback = false` (`rdp_device.cpp:202`) → `flush_queues` runs the 1x pass and,
  when `render_pass_is_upscaled()` (`rdp_renderer.cpp:3137-3157`), the upscaled pass. Both use the tag
  `"depth-blending"` (`:2340-2343`). [../performance.md](../performance.md) divides 12.14 ms by 10.0
  to get 1.21 ms "per pass": that is per dispatch.
- **Change**: tag `"depth-blending-upscaled"` when upscaled; same for rasterization/shading and
  binning (`submit_tile_binning_combined`, `:1979`). Print `render-pass` and `render-pass-upscaled`
  (`:2406-2409`, `:2462-2465`) and the CPU tag `render-pass-fence` (`:3086-3092`) on the `gpu:` line.
- **Caution**: `render-pass*` intervals *contain* the stage intervals; do not add every tag into a
  total. Report GPU busy per VI as first-to-last timestamp per frame context instead.
- **Expectation to check**: render-pass count per frame *roughly* constant across factors (flush
  limits scale with S², `rdp_renderer.cpp:74-75, :1095-1102, :1666-1690`; tile rounding makes it
  approximate).

### G2. Time Pre and Post (GPU-4, confirmed with correction)

- **Evidence (STATIC)**: at S>1, Pre (`submit_update_upscaled_domain`) runs for **every** render
  pass, including non-upscaled off-screen ones; Post only for upscaled ones
  (`rdp_renderer.cpp:2365-2402`). 1-D dispatch over native pixels, `NUM_SAMPLES = S²`
  (`:2092-2140`). Pre writes S² copies at the 8 MiB stride when a word differs from its reference
  (`update_upscaled_domain_pre.comp:103-155`). Only SSAAResolve is timed (`:2153-2159`).
- **Change**: `upscale-pre` and `upscale-post` timestamps under `caps.timestamp >= 2`.
- **Benefit**: none directly; sizes a cost that is linear in native pixels normally and grows with S²
  in CPU-drawn scenes.

### G3. Read the sweep against a scaling model (GPU-5, weakened)

| Stage | Expected scaling 1x→8x (STATIC) |
| --- | --- |
| Span setup | ~S (upscaled setup runs S× the scanlines; not traced in full) |
| Binning, shading, early-out workgroups | ~S² |
| Depth blend, per-primitive loop | S² × overdraw per tile |
| Depth blend, `init_tile`/`finish_tile` requests | ~S³ (2 accesses per pixel, uncoalesced) |
| Pre/Post | native pixels (constant) unless the CPU wrote RDRAM |
| VI/extract | output-sized |

Use it as a **rough indicator only**: an excess over S² can also come from overdraw changes or L2
thrashing in the 2 GiB `tile_work_list` and 512 MiB per-tile buffers. To separate mechanisms, also
log binned primitives per tile, or run the A/B of G4 step 1.

### G4-diag. Shader statistics (GPU-7, weakened)

One run with `RADV_DEBUG=shaderstats` at 8x: VGPRs, waves per SIMD and LDS of `depth_blend.comp`
and the rasterizer specialisations. Occupancy is currently assumed, not known.

## 2. Depth blending first

### G4. Upscaled RDRAM layout (GPU-2, weakened)

- **Problem (STATIC)**: upscaled RDRAM is sample-major: slice = `(y & (S-1))*S + (x & (S-1))`,
  address = `index + slice*(RDRAM_SIZE>>1)` (`memory_interfacing.h:286-302, :339-358`; loads/stores
  `:56-97, :153-160, :185-284`). With 8x8 workgroups (`rdp_renderer.cpp:2220-2221`) and dispatch
  `(W*S/8)×(H*S/8)` (`:2338`), one workgroup at 8x is the 64 samples of one native pixel, in 64
  planes 8 MiB apart. HOST-MEASURED line count (re-checked): 24 / 48 / 96 / 192 distinct 128-byte
  lines per workgroup at 1x / 2x / 4x / 8x.
- **Correction**: these scattered accesses are **2 per pixel** (`init_tile` and `finish_tile`,
  `depth_blend.comp:86-150`). The per-primitive loop (`:115-138`) reads coverage, colour, depth and
  shade alpha (~10 B per pixel per primitive) at `tile_instance*64 + gl_LocalInvocationIndex`,
  coalesced. A row of workgroups at 8x touches ~64 planes × 640 B per buffer, small enough that L2
  may hold it. The wave32/small-types result fits a memory limit, but fits per-tile buffer bandwidth
  equally. **The layout's share of the 12-16 ms is unknown.**
- **Step 1 (decide)**, any of:
  1. An A/B test build with `init_tile`/`finish_tile` replaced by a coalesced access (image wrong,
     timing only).
  2. A standalone compute microbenchmark on the console: u16 load/store, 8x8 workgroups, 2560×1920,
     sample-major (8 MiB per lane) vs pixel-major (contiguous).
  3. Time per upscaled dispatch against average binned primitives per tile.
- **Step 2 (only if step 1 shows a large gap)**: pixel-major layout, element
  `native_index*S*S + slice`, changing every reader and writer together: `memory_interfacing.h`
  (load/store, write-mask offset `:328/:335`), `update_upscaled_domain_pre.comp`, `_post.comp`,
  `_resolve.comp`, `extract_vram.comp`, `vi_scale.frag` (bindings at `video_interface.cpp:454-455`),
  and `clear_super_sampled_write_mask`'s offset (`rdp_renderer.cpp:2179`). Internal buffer only:
  the CPU never sees it, 1x RDRAM unchanged. Pre's S² copies become contiguous as a side effect.
- **Benefit**: unknown; ceiling is part of 12-16 ms/frame at 8x (CONSOLE-MEASURED); smaller at 2x;
  none at 1x.
- **Risks**: **needs visual and correctness testing.** A missed access site gives garbage or blocky
  upscaled pixels. Compare scanout and extracted RDRAM bit for bit, old against new, on recorded RDP
  command dumps (`rdp_dump_write.cpp`) at 2x/4x/8x. Behind a default-off setting.
- **Benchmark**: `depth-blending-upscaled` ms per dispatch, GPU busy per VI and speed, same probe,
  2x/4x/8x, before and after.

### G5. Skip Post when the 1x pass wrote nothing (GPU-4)

- **Reasoning (STATIC, reviewer)**: Pre already makes the reference equal to the real word
  (`update_upscaled_domain_pre.comp:113, :133, :155`), so Post is a no-op when the 1x pass wrote
  nothing (`fb.color_write_pending` / `fb.depth_write_pending`).
- **Risks**: a stale reference would give wrong upscaled pixels after CPU writes. **Needs
  correctness testing**, default-off.
- **Benefit**: unknown; likely small in steady 3D scenes, larger in CPU-drawn scenes at 8x.

## 3. CPU/GPU synchronisation at 8x (GPU-3, weakened)

- **Evidence**: `flush_queues` waits on `internal_sync[buffer_instance].fence` when the 32-instance
  ring wraps (`rdp_renderer.cpp:3080-3094`, `rdp_data_structures.hpp:372`); `maintain_queues` submits
  after 8 passes, 256 primitives, GPU idle or 1 ms (`:2483-2496`); the winsys spins then sleeps
  (`radv_ps5_winsys.c:120-149`). gpu: lines are per frame context = per VI (`vi.rs:180-206`).
- **Correction**: the GPU's timed work (~18 ms/VI) exceeds the budget, so the GPU is *one* limit. But
  with ideal overlap 18 ms predicts ~93%, and the console measured 52-70% (different builds). Untimed
  GPU work (VI scale and 4K downscale, present, gaps between submissions) or CPU/GPU serialisation
  may account for the rest. **Synchronisation work is not ruled out** until per-VI GPU busy time and
  `render-pass-fence` are logged in the same run.
- **Order**: G1/G2 instrumentation → read the gap → then decide between submit heuristics, sync-state
  count or wait policy ([ps5-platform-optimisation.md](ps5-platform-optimisation.md) X3) and G4.

## 4. Runtime upscale change (GPU-6, weakened)

Today a factor change needs a game restart. It is a feature (it makes the sweep possible in one
session on one scene), not a speed gain.

Sequence, on the emulation thread between VIs, in a new `rdp_set_upscale(factor, ssaa)` under
`PSGOPHER` in `interface.cpp`:

1. `processor->idle()`.
2. `rdp_save_state` into a scratch buffer (`interface.cpp:1360-1408`).
3. Delete the processor **first** (`rdp_close`, `:715-741`): at 8x it holds ~4.5 GiB
   (`tile_work_list` 2 GiB; per-tile colour/depth 512 MiB each; coverage and shade alpha 128 MiB
   each; multisampled RDRAM 512 MiB; hidden 256 MiB; binning and offsets 32 MiB each;
   `rdp_renderer.cpp:298-342, :586-605`), all zero-initialised, hidden filled with 0x03.
4. Call `rdp_new_processor` with the new factor (`:540-597`: re-registers the snapshot filter,
   synchronous uploads, tracker reset, write-mask callback).
5. `rdp_load_state` — **it overwrites `gfx_info` with its argument (`:1386`), so the new factor must
   travel in that struct** — then replay `device.vi.regs` as `video.rs:201-208` does.
6. Re-run `psgopher_hdr_init` (called right after `rdp_new_processor`, `:670-672`).

Behaviour: the upscaled domain restarts from native data, so framebuffers that are never redrawn stay
at native resolution (same as cold boot). Skip when the factor is unchanged. Behind a default-off
setting, from the quick menu.

Risks: a missed piece of state (VI registers, tracker, snapshot filter, HDR) shows a black frame,
stale textures or a crash; out of memory at 8x if the old processor is not freed first.
Console test: switch 1x→8x→1x repeatedly on one scene; check the `rdp: ... upscale` line, frame
continuity, switch time, GPU memory, and a save/load right after a switch.

## 5. Evaluating 4x as the default

No 4x figure exists yet. The decision needs Phase B data, not this audit. Criteria proposed for the
owner:

| Criterion | Threshold (proposal) | Source |
| --- | --- | --- |
| Speed on probes A, B, C at 4x | ≥ 99% (full speed) in steady scenes; heavy opening no worse than 1x's | Phase B sweep, 3 interleaved repeats |
| Frame-interval p99 / count over 1.5× VI | No worse than 2x by more than the run-to-run spread | Phase B summary (M6) |
| Audio underruns | 0 in limited runs | Phase B summary |
| GPU busy per VI at 4x | Below ~14 ms (headroom for heavier scenes) | G1 tags |
| Memory | 4x allocations fit with margin (scale the 8x sizes by 16/64) | GPU-6 sizes, boot log |
| Thermal/long run | 30 min at 4x without speed drift | One long run |
| Image | No visual regression against 2x on the compatibility list | [../compatibility.md](../compatibility.md) |

If G4 lands and helps, re-run the 4x evaluation: the layout cost falls hardest at high factors.

## 6. Refuted claims

| Claim | Why it was refuted |
| --- | --- |
| "The winsys reports `wave_front_size 32`, so depth_blend runs in waves of 32" (GPU-7) | `radv_ps5_winsys.c:161, :228` describe device info and a probe result; RADV's default compute wave size on GFX10 is 64 unless cswave32 is set. Wave size is ruled out by the console A/B only. |
| "The shader is limited by memory requests from the layout" stated as the cause (GPU-2) | Leaves out the coalesced per-primitive loop; the layout's share is unknown. |
| "8x is GPU-bound; sync work will not help" at high confidence (GPU-3) | The two console figures come from different builds and do not reconcile (93% predicted vs 52-70% measured). |
| "Render-pass count is the same at every factor" as a fact (GPU-1) | Only roughly: tile rounding at each scale. |
| "Pre/Post run only for qualifying passes" (GPU-4) | Pre runs for every render pass at S>1. |

Also not verified: that Crux2's patch 0014 does not limit compute occupancy (GPU-7).
