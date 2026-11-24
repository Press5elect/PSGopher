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

Benchmark mode is on when `/data/psgopher/benchmark.txt` exists. Its lines:

| Line | Meaning |
| --- | --- |
| `rom <path>` | the ROM (overrides autostart) |
| `seconds <n>` | run n seconds of emulated time, then exit through the shell |
| `state <slot>` | load a savestate first (a fixed scene) |
| `unlimited` | speed limiter off: measures the emulator's maximum speed |
| `upscale <1,2,4,8>` | parallel-rdp's factor for this run |

Fixed for every run: console model and firmware recorded in the log, video output mode, no other
title running, the same build identity, the shader cache state stated (cold or warm).

## Instrumentation (D11)

Counters, written as one `bench:` line a second to klog and as `/data/psgopher/bench-<time>.csv`:

| Counter | Where |
| --- | --- |
| frames presented, VIs, emulated CPU cycles | VI field event, COUNT |
| emulation thread: wall time per second, CPU time per second | the run loop |
| time in `rdp_check_rdram` waits (CPU waiting for GPU) | `interface.cpp` |
| time in present (`end_frame`, FIFO wait) | `rdp_update_screen` |
| time sleeping in the speed limiter | `vi::speed_limiter` |
| audio: grains played, grains underrun, queued ms | `ps5/sdl3` audio stream |
| frame time min/median/99th/max | present timestamps |
| GPU time per frame | timestamp queries around RDP work and scanout (later step) |

The counters cost a clock read per event; anything per instruction is out.

## Timing facts to establish on the console (none assumed)

| Question | How | Status |
| --- | --- | --- |
| `CLOCK_MONOTONIC` resolution and call cost | 1e6 calls, `clock_getres` | [unknown] |
| `nanosleep(1 ms)` real duration (spin_sleep's native part) | 1,000 sleeps, histogram | [unknown] |
| Present cadence at 59.94 Hz | present timestamps | [unknown] |
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
