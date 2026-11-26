# Handover

Where the work stands on 2026-11-26 (build `64422ca`), for whoever picks it up next. The rules are
in [`workflow.md`](workflow.md) (rule 9: interpreters only); the open items in
[`open-work.md`](open-work.md); the performance programme in [`perf/`](perf/implementation-roadmap.md).

## State

- Branch `claude/nice-hopper-9mlrrc`; everything is committed.
- Gopher64 is pinned at `4b824cb8f629`; its changes are `ps5/patches/gopher64/ps5.patch`, plus
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

## Next, in order

All four were offered to the owner and not yet started.

1. **4x as the default upscale.** Measured at 100% where 8x is not; the PS5 port of Mupen64Plus-Next
   made the same choice after measuring ([`mupen64-ps5-comparison.md`](mupen64-ps5-comparison.md)).
   Keep the owner's existing choice.
2. **A default-off padded stride between the sample copies.** Make the stride a specialisation
   constant, RDRAM_SIZE plus a few KiB, used in place of `slice * RDRAM_SIZE`:
   - Change it in `memory_interfacing.h` and `update_upscaled_domain_{pre,post,resolve}.comp`, and
     in the allocation sizes in `rdp_renderer.cpp` (`upscaling_multisampled_rdram`, `_hidden_rdram`,
     the write mask after the copies).
   - `RDRAM_MASK_8` must stay `RDRAM_SIZE - 1`.
   - Regenerate `slangmosh.hpp` (`glslangValidator` is on the PC).
   - On the console, compare depth-blending a pass at 8x in the `gpu:` lines, with the experiment
     on and off, from the same probe.
3. **Find the 6 ms off the CPU.** Time `device->submit` and the other blocking calls in
   `rdp_renderer.cpp` and `rdp_device.cpp`, inside the benchmark only.
4. **Two small log fixes:**
   - Do not count audio underruns while the quick menu pauses the game.
   - Report `bursts` per VI, not as the window's total.

After those, follow [`perf/implementation-roadmap.md`](perf/implementation-roadmap.md):

- **Before any interpreter change, build the correctness harness:** lockstep against the original
  Gopher64. The interpreters are the next limit at 1x, NTSC first.
- PGO comes last of the roadmap items, measured before and after on the same probe.

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
