# PS5 platform optimisation: threads, priorities, waits, memory, compiler

> **Rule.** The VR4300 and RSP interpreters stay; nothing here involves a JIT, recompiler, cached
> native blocks or runtime code generation. Platform changes that no console has checked go behind
> default-off settings (PSGopher rule 3).

Context: [performance-audit.md](performance-audit.md), [benchmark-analysis.md](benchmark-analysis.md),
[interpreter-optimisation-plan.md](interpreter-optimisation-plan.md), [../performance.md](../performance.md),
[../open-work.md](../open-work.md). Driver paths are in `PS5_Mesa/src/amd/vulkan/winsys/ps5/`;
Crux2's thread work in `Crux2/ps5/frontend/ps5_priority.h` and `ps5_thread_layout.*`.

## X1. Thread priorities and placement (P1, weakened)

- **Fact (STATIC)**: grep finds no `scePthreadSetprio`, `pthread_setschedparam`,
  `cpuset_setaffinity` or `scePthreadSetaffinity` in `ps5/` or `vendor/gopher64/src`. Threads in a
  game: emulation (main), audio (`platform.c:496`, default attributes), FenceExecutor
  (`rdp_device.cpp:49`), compile workers (`rdp_renderer.cpp:78-87`), RADV kick
  (`radv_ps5_platform.c:751`), optional RADV recorder (`title_settings.cpp:207`; Crux2 patch 0002
  spins 50 µs), Tokio multi-thread (`psgopher.rs:322`), web jobs (`web.cpp:246`), bench
  (`bench.c:377`, benchmark mode). Crux2 records strict-priority scheduling with every thread at
  700 (`ps5_priority.h:6-12`) and logical CPUs 0-12, 2n/2n+1 one core; it makes "Dedicated Cores"
  the default (Crux2 `docs/releases/v1.0.22.md:15`) with no recorded gain figure.
- **Correction**: whether any spinning helper actually shares the emulation thread's core is
  **unknown**; Tokio workers park when idle and 13 logical CPUs exceed the busy threads.
- **Step 1**: a test build that logs each thread's processor and priority
  (`ps5_pthread_getaffinity_np`; [../performance.md](../performance.md) lists placement as unknown).
- **Step 2 (if placement shows sharing)**, behind default-off settings: Crux2's priorities (audio
  690, emulation 700 set explicitly, web/library/Tokio 730, via `scePthreadSetprio` resolved at run
  time, `ps5_priority.cpp:34`) and a layout (emulation thread alone on one core, FenceExecutor/kick/
  recorder on a second, the rest on the remaining processors; process mask set before parallel-rdp
  and RADV create threads, since threads inherit it). One "threads:" log line.
- **Risks**: none to emulation. A 730 thread can starve while 700 threads spin — check save writes
  (`write_save` runs as a Tokio task). Keep audio unpinned or higher priority.
- **Benchmark**: 60 s at 1x and 2x, same state: off / priorities only / layout; CPU ms/VI, speed,
  `audio_underrun_grains`.

## X2. One clock for the figures (P2, weakened; M4)

- **Fact (STATIC)**: CPU = `CLOCK_THREAD_CPUTIME_ID` (`bench.c:133, :157-158, :259`); RSP and RDP
  = TSC (`bench.c:162-167`). The winsys wait spins up to 1.5 ms with `pause`, then
  `radv_ps5_sleep_us(1000)` = nanosleep (`radv_ps5_winsys.c:123-149`, `radv_ps5_platform.c:174-181`).
- **Correction**: the double count applies to waits **on the emulation thread** — the render-pass
  fence in `enqueue_command` (`rdp_renderer.cpp:3083-3088`). `rdp_check_rdram` waits on a condvar
  (`rdp_device.cpp:1092-1097`) and does not spin there. At 8x the emulation thread's own spin is in
  the 17-21.5 ms CPU figure, so that figure overstates emulation work (reviewer). "8x GPU-limited
  whatever the CPU does" is **not** established: per-VI GPU time needs game fps or the frame-context
  timestamps.
- **Change (measurement only)**: CLOCK_THREAD_CPUTIME_ID as well as TSC around the RDP entry points;
  spin ns, sleep ns and wait count in `radv_ps5_queue_wait_seq`, printed on the `gpu:` line; game fps
  next to GPU ms. parallel-rdp's `render-pass-fence` already gives the fence stall time.

## X3. Fence waits: spin then sleep (P3, confirmed)

- **Fact (STATIC)**: defaults 1500 µs spin, 1000 µs sleep; `RADV_PS5_WAIT_SPIN_US` 0-100000,
  `RADV_PS5_WAIT_SLEEP_US` 50-10000 (`radv_ps5_winsys.c:101-117`); wait-any sleeps 100 µs
  (`radv_ps5_sync.c:155-162`). Every SyncFull calls `signal_timeline()` (`interface.cpp:1690-1695`)
  → FenceExecutor `fence->wait()` (`rdp_device.cpp:950-957, :1259`). RDRAM waits wake through the
  chain GPU → FenceExecutor poll → condvar → emulation thread.
- **Unknown**: nanosleep granularity on the console (`psgopher_probe`, `psgopher.rs:639-640`).
- **Step 1, no code**: the probe; then SPIN 1500/200/0 × SLEEP 1000/250/100 at 1x and 8x, 60 s each,
  recording `rdram_wait_ms`, `rdram_waits`, CPU ms/VI and speed.
- **Step 2**: if shorter sleeps help, set them from `title_settings.cpp` before the device starts,
  default-off. Optional driver change in PS5_Mesa: let a non-latency-critical waiter (FenceExecutor)
  skip the spin (needs per-call control RADV lacks today).
- **Benefit**: bounded by wake latency × RDRAM waits per second (unknown). With the ring full the late
  wake does not idle the GPU; on RDRAM-conflict waits it adds to the VI.

## X4. Memory

- RDRAM is allocated 64 KiB-aligned (`rdram.rs:189-192`), meeting the import's alignment
  ([../graphics.md](../graphics.md):15) — STATIC (specialist; not re-checked by the reviewer).
- Check the boot log's `rdp:` line for whether RDRAM is host-imported or copied; the copy path turns
  on synchronous uploads (`interface.cpp:575-576`) and changes every conclusion about waits.
- 8x allocates ~4.5 GiB of GPU buffers ([gpu-optimisation-plan.md](gpu-optimisation-plan.md) §4);
  relevant to the runtime upscale change and to the 4x default.
- Unknown: whether addresses 8 MiB apart share a GDDR6 channel or L2 bank (the winsys documents only
  the image pipe interleave, `radv_ps5_winsys.c:162-164`).

## X5. Compiler target (P4, confirmed; RSP-7 weakened)

- **Fact**: `build-core.sh:53, :69` set `-Ctarget-cpu=x86-64-v3` (deliberate, matching upstream,
  comment `:48-49`), overriding the spec's `"cpu": "znver2"` (`x86_64-sie-ps5.json:4`). HOST-MEASURED
  by the specialist (`rustc --print cfg`; not re-run by the reviewer): drops adx, aes, clflushopt,
  pclmulqdq, prfchw, rdrand, rdseed, sha, sse4a, xsavec, xsaveopt, xsaves — none used by the
  interpreters. `build.rs:135-153` puts `-march=x86-64-v3` on rdp, volk, simd and rcheevos, removing
  clang's PS5 default tuning (znver2). Host C/C++ (`build.sh:38`) keeps znver2. No pdep/pext/gather
  in 0x2f0000-0x320000 (STATIC).
- **Profile**: `psgopher` (`Cargo.toml:82-85`) inherits release: opt-level 3, overflow checks and
  debug assertions off, panic=unwind (needed by `catch_unwind`, `psgopher.rs:540-556`).
- **Change (A/B)**: `-Ctarget-cpu=znver2` (or `-Ztune-cpu=znver2` keeping the v3 features) and
  `-march=znver2` under `cfg(psgopher)` in `build.rs`, re-exporting `ps5.patch`.
- **Benefit**: unknown; 0-3% is an estimate. **Risks**: none to semantics (both have FMA; Rust does
  not contract; denormal mode set by the wrappers); re-run host tests and the compatibility ROMs.

## X6. Frame pointers (P7 confirmed; CPU-7 weakened)

- **Fact (STATIC)**: `"frame-pointer": "always"` (`x86_64-sie-ps5.json:10`); every handler has
  push/mov/pop rbp (addiu@0x302b00, lui@0x300de0, vmudh@0x309a00). `ps5/host/crash.c:125-131` walks
  the rbp chain.
- **Option**: `"non-leaf"` as a measured experiment. A frameless leaf leaves rbp at its caller's
  frame, so the walk would lose the leaf's **immediate caller** (the faulting leaf is still named by
  RIP) unless `crash.c` also reads the return address at `mc_rsp`. Do that fix first, then A/B.

## X7. PGO (P5, confirmed)

`ps5/pgo/` is empty, so builds are PGO-off (`build-core.sh:51`). Profile runtime present
(`.deps/pgo/`). Only the gopher64 crate is instrumented (`build-core.sh:54-65`). The handlers are
separate functions called indirectly, so PGO mainly affects `run_game`'s dispatch layout and handler
placement (reviewer's correction). Collect per [../building.md](../building.md) with
`PSGOPHER_PGO=generate` over several games at 1x and 8x, opening and in game, including RSP-heavy
scenes; merge; build `use`; three runs each, off vs use, including a probe not in the training set.
Keep the profile with the pin; check mismatches with warnings on (`-pgo-warn-mismatch=false` at
`build-core.sh:60` hides them). Gains unknown (5-15% is a general estimate, not from this code).

## X8. Verified non-issues (P7, confirmed)

- Emulated TLS (`"tls-model": "emulated"`, spec `:34`): 118 `__emutls_get_address` references, none
  in `cpu_instructions`, `memory`, `cache` or `rsp_*` (STATIC).
- parallel-rdp's `get_current_thread_index` is per command buffer, not per primitive.

## Refuted claims

| Claim | Why |
| --- | --- |
| "8x is GPU-limited whatever the CPU does" (P2) | Per-VI GPU time is unknown without fps or frame-context timestamps; at one frame per two VIs, 14-19 ms/frame is 7-9.5 ms/VI. |
| Helper threads share the emulation thread's core (P1), as an observation | A possibility only; no placement has been logged. |
| The handlers are inlined into one dispatch body (P5) | Each is its own function, e.g. `cpu_instructions::addiu`@0x302b00. |
| `rdp_check_rdram` waits spin on the emulation thread (implied by P2) | They wait on a condvar (`rdp_device.cpp:1092-1097`). |

Resolved (STATIC): the platform reviewer could not identify the 64-bit counter incremented on the
cache-hit path (`incq 0x4a920(%rdi)` at 0x319b27). The VR4300 review identifies offset 0x4a920 as
COP0 Count (the run loop's Count increment at 0x2da680; "incq Count at 0x4a920" in the CPU-5
verdict). It is emulated cycle accounting, so it stays.
