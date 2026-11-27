# Handover

Where the work stands on 2026-11-26 (build `64422ca`), for whoever picks it up next. The rules are
in [`workflow.md`](workflow.md) (rule 9: interpreters only); the open items in
[`open-work.md`](open-work.md); the performance programme in [`perf/`](perf/implementation-roadmap.md).

## State

- Branch `claude/nice-hopper-9mlrrc`; everything is committed.
- Gopher64 is pinned at `240ccf441ddb` (moved 2026-11-27); its changes are `ps5/patches/gopher64/ps5.patch`, plus
  `parallel-rdp.patch` for the submodule (`ps5/tools/fetch-gopher64.sh --export`, then `--check`).
- Phase A of the performance programme (the audit and its six plans in `perf/`) is done. Of Phase
  B, B1-B3 (benchmark mode) are done:
  - The benchmark window is counted in emulated VIs, on one TSC clock, with its parts separated.
  - The board runs a benchmark from a probe: △ on a probe gives 1x, 2x, 4x and 8x in turn, with
    300 VIs of warm-up, 600 measured and 2 repeats.
  - Slow moments are captured by themselves: under 95% for 3 seconds starts a 600-VI window, at
    most 3 a game and 120 s apart (BENCH → Video → Slow moments, on by default).
- Built but not yet used on a console:
  - the PGO build of the core (`PSGOPHER_PGO=generate|use`);
  - GPU timing (BENCH → Video → GPU timing, off by default).

## The latest console findings

From boot log 2026-11-26: one PAL game (50 VIs a second, 20 ms a VI), played from the same probe
at each factor, with default settings and GPU timing on.

| Factor | Speed | Emulation thread a VI | parallel-rdp on the CPU | Depth-blending a pass (GPU) |
| --- | --- | --- | --- | --- |
| 1x | 100% | ~18 ms | 1.4 ms | not upscaled |
| 2x | 100% | 17-20 ms | 1.4-1.7 ms | 0.025 ms |
| 4x | 100% | 17-20 ms | 1.5-1.8 ms | 0.11 ms |
| 8x | 71-83% | 25 ms (19 ms of CPU time) | 7-10 ms | 2.0-2.7 ms |

- **There is no 1x-to-2x drop.** The earlier logs compared different scenes; roadmap priority 1 is
  settled.
- **Only 8x is slow.**
- **The CPU side has little margin.** The RSP is 7-9 ms a VI and the VR4300 with the rest about
  8.7 ms. That is about 18 ms of a 20 ms PAL VI, and an NTSC VI is 16.7 ms.

### The 8x cliff

What was measured:

- **4x is linear, 8x is not.** Depth-blending a pass costs 4.3 times as much at 4x as at 2x, which
  is the extra pixels. At 8x it costs about 20 times as much as at 4x.
- **Only the passes that touch the upscaled RDRAM jump.** Extract-vram costs 16 times as much at 8x
  as at 4x. Shading (tile-local) and VI scale grow about 3.5 times, which is normal.
- **The emulation thread loses about 6 ms a VI off the CPU, and the benchmark does not say where.**
  Its wall time is 25 ms a VI against 19 ms of thread CPU time. parallel-rdp is 7.7-7.9 ms a VI
  (commands 5.1-5.4, frame 2.3-2.4), but the GPU fence waits that are timed are only 0.01 ms.

Why, read from the code (not yet proven on a console):

- **One copy of RDRAM per sample.** parallel-rdp keeps the upscaled RDRAM as factor² copies of
  RDRAM, `RDRAM_SIZE` apart (`memory_interfacing.h`: `index += slice * RDRAM_SIZE`). At 8x that is
  64 copies 8 MB apart: 512 MB, plus 256 MB of hidden RDRAM.
- **At 8x a tile is one N64 pixel.** Depth-blending runs 8x8 tiles of upscaled pixels
  (`TileWidth`, `TileHeight`), so at 8x one tile is one N64 pixel. Its 64 lanes read 64 addresses
  8 MB apart, so nothing coalesces, each lane touches a different page, and the power-of-two stride
  may send them all to one memory channel.
- **At 4x it is far milder.** A tile covers 2x2 pixels across 16 copies.
- **The cause is one of two.** Translation (TLB) thrash or channel conflicts would each explain the
  cliff. The measurements do not yet tell them apart.

## The 8x fix (2026-11-26, measured on the console)

The second log of 2026-11-26 (build `8bc81c2`, the same PAL game and probe at 8x):

| Layout | Speed | Depth-blending a pass | Extract-vram a VI | parallel-rdp on the CPU a VI |
| --- | --- | --- | --- | --- |
| Original (the first log) | 71-83% | 2.0-2.7 ms | 0.68 ms | 7-10 ms |
| Padded | 90-99%, two stalls at 10% and 29% | 0.83-1.07 ms | 0.44 ms | 3.0-4.3 ms (19 ms in the stalls) |
| Per pixel | **100%**, 0 underruns over 3 minutes | **0.12-0.15 ms** | **0.09 ms** | **1.5-1.9 ms** |

- **Per pixel is the default** (`rdp_layout = 2`); Original (0) stays as a choice in case a game draws
  wrongly. Padded is no longer offered and `1` is read as per pixel; its shader path remains in
  `rdram_layout.h`, unused.
- **What it says of the cause.** Padded removed only the channel concentration and recovered about
  half; Per pixel removed the page spread too and recovered all of it, at 8x a pass is now only 1.2
  times its 4x cost. So both mattered, translation (or coalescing) the more.
- **Not yet known:**
  - the picture with Per pixel was not reported on;
  - supersampling and a save state loaded at 8x with Per pixel;
  - what the Padded stalls were: parallel-rdp at 19 ms a VI on the CPU, thread CPU time 36 ms, not
    off-CPU (1 ms).
- **What is left at 8x is the CPU side:** the emulation thread is 17-20 ms of a 20 ms PAL VI (RSP
  7-9 ms, VR4300 and the rest about 9 ms), so an NTSC game (16.7 ms a VI) is limited by the
  interpreters, not the GPU.

## Done before that log

- **The layout switch** (`rdp_layout`, env `PSGOPHER_RDP_LAYOUT`): Original (0), Padded (1: the
  copies 8 MB + 4352 bytes apart, an odd multiple of 256 bytes), Per pixel (2: 32-bit words permuted
  to `word * factor² + slice`, the 8- and 16-bit and hidden views derived from it so they alias as
  before). The helpers are in `shaders/rdram_layout.h`; 11 modules of
  `slangmosh.hpp` are made again by `ps5/tools/make-rdp-shaders.py` (glslang and spirv-opt, not
  Granite's slangmosh, which cannot be built here). Mode 0 was compared with the pristine shaders
  over 784 specialisations on the PC.
- **The benchmark window** reports `submits` (every `device->submit` and `next_frame_context` in
  parallel-rdp, the VI path included) and `parallel-rdp off-CPU` (wall less thread CPU time in the
  commands and frame parts); `bursts` is a VI's.
- **Underruns** are not counted while the quick menu is open (the hotkey pause is not covered).
- **The lockstep harness** (`ps5/tests/run-lockstep.sh`, [`testing.md`](testing.md)): no hooks in
  `vendor/gopher64`; it builds the pin and the working tree for the PC and compares them.
- **What the sources say of the cliff** (research, nothing proven): the winsys allocates every
  buffer the same way whatever its size (direct memory type 12, 2 MiB alignment from 2 MiB up), so
  there is no driver switch; the GPU has 16 pipes at a 256-byte interleave, which is why Padded's
  pad is an odd multiple of 256 bytes. Crux2's upscaled targets are images in 64 KiB blocks.
  RADV prints `radv/ps5 submissions: ... claim_ms ... suspend_ms` to the klog every 10 s.

## Next, in order

1. **Confirm Per pixel's picture** at 8x, with supersampling, and after a save state is loaded;
   and Original's picture (its shaders were made again by another compiler).
2. **The interpreters, the RSP first** ([`perf/interpreter-optimisation-plan.md`](perf/interpreter-optimisation-plan.md)):
   each change through `run-lockstep.sh`, then measured on the console. An NTSC VI is 16.7 ms and
   the CPU side is about 18 ms in a demanding game.
3. **Game start**: the upscaled RDRAM (768 MB at 8x) is zeroed twice on the CPU
   (`RADV_PS5_STREAM_ZERO` is not set; Granite zeroes host-visible memory again).
4. PGO last, measured before and after on the same probe.

The owner's direction (2026-11-26): no lowered default factor; 8x is to be made fast.

## Open questions to the owner

- A "Reset to defaults" action in BENCH → This console (all settings, or only the performance
  ones).
- Fixed settings for benchmark runs, the owner's own put back afterwards.

## Ways of working that held

- **Process the boot log with scripts; don't read it whole.** Tabulate the `video:` lines by
  factor (the `settings: upscale` line marks each game), and divide depth-blending by its
  dispatch count `(N.Nx)` to get a pass's cost.
- **Compare factors only from the same probe.** A different scene invalidates the comparison.
- **Push only on the owner's word, each time.** Don't put game titles in commits or docs (IDs, or
  "a PAL game"), model names, chat links or `CLAUDE.md` in the repository.
