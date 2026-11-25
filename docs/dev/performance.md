# PSGopher: performance

No benchmark has been run on a console yet; the first game runs gave the observations below. This
document holds the method, the instrumentation, the baseline when it exists, and one entry per
optimisation.

## Method

1. **Baseline first**: unmodified Gopher64 (the pin, PS5 platform code only), every benchmark game,
   identical conditions (below), before any optimisation.
2. **One change at a time**, each built and run against the same baseline build in the same
   session, at least three runs each; the median is reported with the spread.
3. **Keep or revert**: a change stays only with a measured gain larger than the run-to-run spread
   and no accuracy change (the compatibility checks in compatibility.md). Rejected changes stay
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

## First console observations

Not a baseline (no benchmark mode, no fixed scene): what the owner's boot logs show.

| Date, build | Game | Observation |
| --- | --- | --- |
| 2026-11-25, `d7010d1` | a PAL game (its ID was not logged) | 25.4-42.7 VIs a second against 50 over 25 ten-second windows, mostly ~28: 51-85% of full speed, mostly ~56%; the owner hears choppy audio. The log then had no CPU/present/RDRAM split; `33c8907` adds it to the `video:` line and the stats overlay, so the next log says which of them takes the time |
| 2026-11-25, `33c8907` | an NTSC game (ID not logged) | 59.8 VIs a second (100%) most of 150 s, the game at 30 fps; CPU 1.3-7 ms a VI. Down to 87-93% where CPU reached 10-11 ms with 3-5 ms of present wait; 0 underruns at 100%, 11-75 in a 10 s window below it |
| 2026-11-25, `33c8907` | a second NTSC game (ID not logged) | 14.5-39 VIs a second (24-65%), CPU 17.5-21.5 ms a VI, present wait 0.1-4.5 ms: CPU-bound on the emulation thread; 175-1,139 underruns in each 10 s. Both games at 8x upscale, supersampled, 3840x2160. Not yet known: how much of the CPU time is parallel-rdp's (the next build's `RDP` figure) and whether 1x changes it |
| 2026-11-25, `33c8907` | — | The audio underruns follow the speed: Gopher64 keeps about two VIs of audio queued and cannot refill below 100%. The GPU wait in the winsys spins at most 1.5 ms before sleeping (`radv_ps5_queue_wait_seq`), so it does not explain 17-21 ms of CPU time |
| 2026-11-25, `33c8907` | — | The library's search of USB (60,000 entries, its budget) and the share took 10.2 s with `library.tsv` (29.9 s on the run before) |
| 2026-11-25, `d7010d1` | — | The library's first search of an SMB share with 1,424 ROMs took 55.6 s (headers read over the network); later starts read `library.tsv` |

## Baseline

Not measured yet.

## Optimisation log

Each entry: problem, evidence, solution, implementation, conditions, before/after, compatibility,
decision. None yet.

## Candidates, ranked

See ../history/porting-analysis.md section 5. None is started before the baseline exists. Measured
elsewhere on the same stack and worth an A/B build here: profile-guided optimisation (Crux2, with its
own profile runtime) and LTO (Crux2: about 3%; the core is built with thin LTO already).
