# PSGopher: performance

No PSGopher number has been measured on a console yet. This document holds the method, the
instrumentation, the baseline when it exists, and one entry per optimisation.

## Method

1. **Baseline first**: unmodified Gopher64 (the pin, PS5 platform code only), every benchmark game,
   identical conditions (below), before any optimisation.
2. **One change at a time**, each built and run against the same baseline build in the same
   session, at least three runs each; the median is reported with the spread.
3. **Keep or revert**: a change stays only with a measured gain larger than the run-to-run spread
   and no accuracy change (the compatibility checks in PS5_COMPATIBILITY.md). Rejected changes stay
   documented here.
4. **Separate the clocks**: wall time (`CLOCK_MONOTONIC`), the thread's own CPU time
   (`CLOCK_THREAD_CPUTIME_ID`, [unknown] on the console: probed at boot), emulated cycles
   (COUNT), and GPU time (timestamp queries). A thread that is busy is not necessarily the
   bottleneck: a frame's critical path is what decides.

## Benchmark conditions

Benchmark mode is on when `/data/psgopher/benchmark.txt` exists (`ps5/host/main.cpp`). Its lines:

| Line | Meaning |
| --- | --- |
| `rom <path>` | the ROM (overrides autostart) |
| `seconds <n>` | end the game n seconds (wall clock) after its video is up, through Gopher64's own exit event |
| `unlimited` | speed limiter off (Gopher64's fast-forward event): the emulator's maximum speed |

Not yet: a fixed starting scene (savestate slot) and the upscale factor per run; the upscale is
set in `config/config.json` (`video.upscale`) for now.

Fixed for every run: console model and firmware recorded in the log, video output mode, no other
title running, the same build identity, the shader cache state stated (cold or warm).

## Instrumentation (D11)

`ps5/host/bench.c`, fed by two hooks in parallel-rdp's interface (`PSGOPHER`) and by the SDL
layer's audio counters. One `bench:` line a second to klog, and a row of
`/data/psgopher/bench-<time>.csv`, written in one `write()` at the end:

| Column | Meaning |
| --- | --- |
| `presents` | frames presented this second |
| `frame_min/median/p99/max_ms` | present-to-present intervals |
| `present_block_ms` | time the emulation thread spent in `end_frame` + `begin_frame` (FIFO wait, acquire) |
| `emu_cpu_ms` | the emulation thread's CPU time this second (`CLOCK_THREAD_CPUTIME_ID`; -1 if the console lacks it) |
| `rdram_wait_ms`, `rdram_waits` | the CPU waiting for the GPU before touching RDRAM the RDP has pending |
| `audio_underrun_grains` | 256-frame grains the stream could not fill |
| `audio_queued_ms` | input audio waiting at the second's end |

Emulation-thread wall time minus `emu_cpu_ms` minus `present_block_ms` is time it slept (the speed
limiter) or was descheduled. The hooks cost two clock reads a frame and one per RDRAM wait.

The probe (`/data/psgopher/probe.txt`, then the title exits) writes `probe:` lines: the
monotonic clock's cost and resolution, whether `CLOCK_THREAD_CPUTIME_ID` works, the TSC rate
against it, a std `Mutex`/`Condvar` ping-pong across two threads (FreeBSD's `_umtx_op`),
`Instant::now`'s cost and step, `std::thread::sleep(1 ms)` and `spin_sleep::sleep(1 ms)`
(200 each: min/median/p99/max), `SysRng`, a 4 MiB file written and read back, a Tokio file write,
`available_parallelism`.

## Timing facts to establish on the console (none assumed)

| Question | How | Status |
| --- | --- | --- |
| `CLOCK_MONOTONIC` resolution and call cost | probe: 1e6 calls, `clock_getres` | [unknown] |
| `sleep(1 ms)` real duration, std's and spin_sleep's | probe: 200 each | [unknown] |
| Present cadence at 59.94 Hz | benchmark: frame intervals | [unknown] |
| `sceAudioOutOutput` grain period | timestamps in the audio thread | [unknown] |
| Core placement of the main thread | `ps5_pthread_getaffinity_np` | [unknown] |

OGBox's figures (~1 kHz time-call resolution, `Sleep(1)` ~1.07 ms) are from its Windows-API
emulation layer and are not used here.

## Baseline

Not measured yet.

## Optimisation log

Each entry: problem, evidence, solution, implementation, conditions, before/after, compatibility,
decision. None yet.

## Candidates, ranked

See PS5_PORTING_ANALYSIS.md section 5. None is started before the baseline exists.
