# Interpreter optimisation plan (RSP, then VR4300)

> **Rule.** PSGopher keeps Gopher64's VR4300 and RSP interpreters. No JIT, dynamic recompilation,
> block recompiler, cached native blocks, runtime code generation or hybrid, ever, nor as a
> recommendation. Every item below is interpreter work: dispatch, decoding, handlers, SIMD, memory
> access, data layout, compiler options or PGO. The caches proposed here hold indices into
> Gopher64's existing pre-decoded handler data, not native code.

Context: [performance-audit.md](performance-audit.md) §1, [benchmark-analysis.md](benchmark-analysis.md),
[ps5-platform-optimisation.md](ps5-platform-optimisation.md) (compiler options shared with the
platform plan), [../open-work.md](../open-work.md) (tasks 18 PGO, 20 correctness harness).
Addresses are in `build/ps5/link/llvm-pie.elf`. All benefit figures are **unknown until measured on
the console**; the host figures are a Xeon VM, not Zen 2.

## 0. Before any change: measure the split

| Counter | Why | Finding |
| --- | --- | --- |
| RSP cycles per VI, bursts per VI | ns per RSP cycle; separates workload from speed | RSP-2 (confirmed) |
| RDRAM waits taken inside RSP bursts, subtracted from RSP | `rsp_interface.rs:161, :186` → `rdp_check_rdram` → `wait_for_timeline`; `bench.c:166` subtracts only RDP ticks, `bench.c:269-279` ignores `in_rsp` | RSP-2 |
| Read the existing `rdram_ms` first | `rsp_ms − rdram_ms` is already a lower bound on non-wait RSP time (`bench.c:159-160`); no new build needed | reviewer |
| Direct VR4300 time: TSC around `cpu::run` less nested RSP/RDP | Today only inferred | CPU, M4 |
| Guest instructions and idle-skipped cycles per VI | Separates overclock and game load (CPU-9) | CPU |
| Fill/writeback/uncached RDRAM counts; slow-path `rdp_check_rdram` count | Sizes CPU-4 | CPU |
| Opcode/path histogram (diagnostic build only) | Decides RSP-5, RSP-6, CPU-6 | RSP, CPU |

CONSOLE-MEASURED bound (RSP-2): 1.04M RSP cycles per VI (62.5 MHz / 59.94 Hz) with at most 2
instructions per cycle means 9 ms is ≥ 4.3 ns per RSP instruction, slower than the host replica's
2.6-3.4 ns, so waits or mispredictions are probably in the figure. RSP time at 1x has not been
reported, so whether RSP time scales with GPU load is not yet known.

## 1. Correctness method: lockstep against the original

Every change is kept only if it passes all of these against the unmodified pin build:

1. **VR4300 lockstep trace** (CPU-1..9): same ROM and save state, no input, N VIs. Per VI dump PC,
   GPRs, HI/LO, COP0 (Count, Compare, Status, Cause, EPC, EntryHi), COP1 FGRs and FCR31, and an
   xxhash of RDRAM; streams must be identical.
2. **RSP per-burst differential** (RSP-1, -5, -6): record IMEM, DMEM, registers and SP state at each
   `do_task`; replay through the old and new code (old kept behind a compile-time switch); compare
   gpr, vpr, accl/accm/acch, vcol/vcoh/vccl/vcch/vce, divin/divout/divdp, DMEM, SP_PC, branch_state
   and `cycle_counter` after every burst. The harness must expect the original's DMEM-end behaviour
   (RSP-8).
3. **Exhaustive unit tests** for single handlers: mtc2 all 16 elements × 64k values (RSP-5); lqv/
   sqv/ldv/sdv all 16 elements × 16 alignments × boundary addresses (RSP-6).
4. **Test ROMs**: n64-systemtest (CPU, TLB, cache, COP1, exception sections) and n64-systembench on
   both builds; Gopher64's compatibility ROMs ([../compatibility.md](../compatibility.md)).
5. **Image**: hash of extracted frames on the probes at 1x and 8x (needed for CPU-4, which changes
   when the host waits on the GPU).
6. **Savestate round-trip** old ↔ new for anything touching serialised layout (CPU-8).

This is open task 20 in [../open-work.md](../open-work.md); it should exist before the first
interpreter change lands.

## 2. RSP

| ID | Change | Evidence | Verdict | Benefit | Risk |
| --- | --- | --- | --- | --- | --- |
| R1 (RSP-1) | Streamline the run loop: fetch with `instructions[((pc & 0xFFC) >> 2)]` (drops the panic path, 0x2ff57e → 0x2ff698); test `State::Step` first, other states out of line; drop the second PC store (0x2ff5b7 / 0x2ff520); keep `instruction_type`, `last_instruction_type`, `pipeline_full` in locals, written back on exit; keep `cycle_counter` in memory (read by mfc0 `rsp_su_instructions.rs:527`, `do_dma` `rsp_interface.rs:224`, `run_rdp` `rdp.rs:120`); reload PC after the call (mfc0 rewinds SP_PC, `:551`) | STATIC; HOST-MEASURED llvm-mca znver2 18.1 → 14.1 cycles/iter (branches removed, no store-forwarding model); replica 3.37-3.44 → 3.16-3.19 ns vector mix | Weakened | Unknown; probably at the low end: the removed bounds check is a fused never-taken cmp/ja and the state jump is well predicted. The "5-20%" is an unvalidated estimate. | Ordering of break, sync_point and cycle accounting (the replica caught an extra cycle per task in a first version). Masking is equivalent because every SP_PC writer masks (`rsp_interface.rs:379-386`, `rsp_su_instructions.rs:552`), except a crafted savestate. |
| R2 (RSP-3) | PGO over the probes; check that `callq *%rax` at 0x2ff5a5 is promoted or laid out better | STATIC: `ps5/pgo/` empty → `pgo=off` (`build-core.sh:51`) | Confirmed | Unknown; the site is megamorphic (≤3 promoted targets), so mostly layout | A profile from one game may slow others |
| R3 (reviewer) | Non-leaf frame pointers for handlers (see [ps5-platform-optimisation.md](ps5-platform-optimisation.md) X6) | STATIC: vmudh@0x309a00 has push/mov/pop rbp, 3 of 31 instructions; `"frame-pointer": "always"` | — (reviewer's addition) | Small; more than the bounds check | Crash backtraces lose the leaf's immediate caller |
| R4 (RSP-5) | mtc2 with even element: `modify_vpr16(&mut vpr[rd], velement/2, gpr[rt] as u16)` instead of two byte stores | STATIC mtc2@0x308320 byte stores 0x308351/0x30836e; vmudh loads with vmovdqa (0x309a0e). Equivalence shown (byte 15-e and 14-e = lane 7-e/2). HOST microtest inconclusive. | Weakened | Unknown; `modify_vpr16` is itself a match over immediates; stall only if a vector op reads the register soon after | Very low; exhaustive test |
| R5 (RSP-6) | SIMD forms of the unaligned lqv/sqv/ldv/sdv paths, byte loop kept for wrap cases | STATIC lqv@0x305c90, ldv@0x3054e0, sqv@0x306f70, sdv@0x30641e; fast paths `rsp_su_instructions.rs:805, :1052, :1072-1074` | Weakened | Unknown; only after a histogram shows these paths are hot. "About 6 instructions" is an estimate | Medium: element/address edge cases, DMEM wrap |
| R6 (RSP-7) | `-Ztune-cpu=znver2` with the x86-64-v3 feature set kept (or the A/B in X5) | STATIC `build-core.sh:53` overrides the spec's znver2 | Weakened | Unknown, probably small | None to semantics |
| — (RSP-4) | **Not now**: match on a pre-decoded op number with inlined handlers | HOST-MEASURED replica: no consistent gain over R1 | Confirmed (as a recommendation against) | None measured | Large mechanical change |
| — (RSP-8) | Correctness note, not performance: aligned ssv/slv/sdv do not wrap at the DMEM end and write IMEM without re-decoding (ssv at 0xFFF, slv 0xFFD-0xFFF, sdv 0xFF9-0xFFF; sqv is safe). Upstream behaviour, not touched by ps5.patch | STATIC `rsp_su_instructions.rs:1001-1065`; sdv@0x30655f | Confirmed | None | Fixing changes behaviour; separate, tested change |

Floor on loop cost (reviewer, STATIC): each instruction also stores gpr[0]=0 (0x2ff56f) and reloads
`sync_point` (0x2ff538); both are needed because handlers may write them.

## 3. VR4300

| ID | Change | Evidence | Verdict | Benefit | Risk |
| --- | --- | --- | --- | --- | --- |
| V1 (CPU-1) | Same-icache-line fetch fast path, **kseg0 PCs only**: keep the line's vaddr, phys base and index; on a sequential fetch in the same line skip translation and the tag check. `pc_phys` must still be stored every instruction (read by `cpu_instructions.rs:51-73` idle checks). Invalidate by setting the cached vaddr to an impossible value in `icache_fill`, the CACHE ops (`cpu_instructions.rs:642-675`) and savestate load. TLB-mapped PCs keep the slow path (translation also depends on Status ERL/EXL/KSU and ASID, and can raise exceptions). Kseg1 fetches stay uncached as today (`cpu.rs:277-284`). | STATIC `cpu.rs:259-276`, `cache.rs:72-82`; run_game 0x2da37d-0x2da566 | Weakened | ~15-20 of ~52 loop instructions per sequential instruction (STATIC); time unknown; two indirect branches per instruction remain and their misprediction rate is unmeasurable (no PMU) | A missed invalidation executes stale pre-decoded handlers |
| V2 (CPU-2, P5) | PGO, gopher64 crate | STATIC `build-core.sh:45-67`; cold paths inline in the fetch path (0x2da3a1-0x2da3be, 0x2da4c0) | Confirmed | Unknown | Stale profile after a pin move degrades silently: `-pgo-warn-mismatch=false` (`build-core.sh:60`); remove it for a diagnostic build. C++ (`rdp_check_rdram`) gets no profile. |
| V3 (CPU-5, P6) | `#[cold] #[inline(never)]` on `dcache_fill`/`dcache_writeback` and the miss branch; `#[inline]` on the hit path in `memory.rs:91-125` | STATIC data_read@0x319ad0 pushes six registers before the hit test (0x319ae4-0x319b2e); the uncached path (0x319b33-0x319b58) pays them too | Confirmed | ~10-15 instructions per cached load/store (STATIC); time unknown | None to semantics; check that `lw` does not still push for its own slow-path call |
| V4 (CPU-3) | `#[inline(never)]` on `cpu::run` so the loop keeps the device pointer, icache base and constants in registers | STATIC stack reloads 0xc0(%rsp)@0x2da397, 0xa8(%rsp)@0x2da3d6/0x2da409, 0xb0(%rsp)@0x2da3f8 | Weakened | A few L1-hit loads per instruction; **only a rebuild and disassembly can show the reloads go** (medium confidence) | None |
| V5 (CPU-4) | One `rdp_check_rdram` per cache line on fills/writebacks/uncached RDRAM; copy words directly; sum the per-word cycles (exact: `rdram_cycles_per_word` is const per size). Batched path only when the installed handler **is** `rdram::write_mem`/`read_mem` (MI init mode swaps in `write_mem_repeat`, `rdram.rs:108-121`) | STATIC `cache.rs:27-60, :89-125`; `rdram.rs:56-107`; icache_fill@0x3185b0 (8 indirect calls) | Weakened | Saves 3-7 indirect and cross-language calls per line; each check is tens of instructions, **not** a scan; depends on miss rates (unmeasured) | Moves the GPU wait from per word to per line: same data under the tracker's assumption, but **needs an image and correctness check at 8x** |
| V6 (CPU-6) | Pre-decode COP1 fmt S/D/W/L straight to the final handler, CU1 check folded into each | STATIC `cpu.rs:52-61`; `cop1.rs:207-236`; execute_cop1_s@0x316bb0 `jmpq *0x48fc8(...)`; second-level tables static (`cop1.rs:375ff`, `savestates.rs:333-338`) | Confirmed | One indirect branch and prologue per FPU arithmetic instruction; FPU mix unmeasured | Every final handler must do the CU1 check first |
| V7 (CPU-7) | Non-leaf frame pointers (see X6) | STATIC lui@0x300de0 (9 instructions, 3 frame) | Weakened | ~3 of ~62 instructions per simple handler | `crash.c:127-130` would lose the faulting leaf's immediate caller unless it also reads the return address at `mc_rsp` |
| V8 (CPU-8) | Structure-of-arrays icache: packed tags+valid, 64-byte-aligned pointer lines, words separate; savestate format kept | STATIC 0x68-byte stride (`imulq $0x68` @0x2da3f4), 53,248 bytes | Weakened | Unknown; each hit touches two host lines, not three; only the guest's working set is hot | Savestate compatibility; every accessor updated |
| — (CPU-9) | No code change: fix the overclock setting in every comparison | STATIC `cpu.rs:229-238`, `vi.rs:66-75` | Weakened | Hygiene | — |

Order: V2 and V3 (cheap, semantics-free) → V4 (check the disassembly) → V1 → V6 → V5 (after the
counters) → V7/V8. Each one at a time, against the same baseline build, three runs each
([../performance.md](../performance.md) method).

## 4. Refuted claims

| Claim | Why it was refuted |
| --- | --- |
| `conflict()` "scans the stamp arrays" on every word when the RDP is busy (CPU-4) | For 4-byte accesses `first == last`, so it reduces over one block (`rdram_tracker.hpp:81-100`); `get_completed_timeline()` is one atomic acquire load (`rdp_device.hpp:132-135`). |
| Overclock leaves RSP work unchanged (CPU-9) | Only the cost per task is fixed (`rsp_cpu.rs:199-201`); if the overclock lifts a lagging game's frame rate, RSP tasks and RDP lists per VI rise. |
| 55-63 loop instructions; 20-25 saved by V1 (CPU-1) | Reviewer counts ~52 on the kseg0 hit path and ~15-20 saved, because `pc_phys` must still be stored. |
| "The PS5's CPU may not support all Zen 2 features" (RSP-7) | Unsupported; the project's own target spec names znver2. |
| RSP-1's "5-20%, 0.3-1.5 ms" as figures | From branch-free llvm-mca and a VM replica; estimates only. |
| `#[inline(never)]` "will" remove the reloads, high confidence (CPU-3) | A prediction; LLVM may still spill around the indirect call and jump table. |
