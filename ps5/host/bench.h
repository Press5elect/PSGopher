/*
 * PSGopher - the benchmark mode's counters (bench.c).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the once-a-second logger; the CSV goes to `path` at psgopher_bench_stop. */
bool psgopher_bench_start(const char *path);
void psgopher_bench_stop(void);

/* parallel-rdp's interface calls these (vendor/gopher64/parallel-rdp/interface.cpp, PSGOPHER) */
void psgopher_bench_present(uint64_t begin_ns, uint64_t end_ns);
void psgopher_bench_rdram_wait(uint64_t ns);
void psgopher_bench_rdp(uint64_t tsc_ticks); /* the emulation thread's time in parallel-rdp */
void psgopher_bench_rdp_part(int part, uint64_t tsc_ticks); /* 0 drawing commands, 1 syncs, 2 the frame */

/* PSGopher: the quick menu's live figures, the last whole second's: frames shown a second, the
 * emulation thread's CPU time a frame (its waits are none), the present's wait a frame
 * (cpu_ms -1 when the thread clock is not there) */
struct psgopher_live {
   double frames_per_second, cpu_ms, present_ms;
   double rdram_ms, rdram_waits;  /* RDRAM syncs' wait a VI, and how many */
   double rdp_ms;                 /* of the CPU time, parallel-rdp's (commands, the frame's render); -1 unknown */
   double rdp_part_ms[3];         /* of that: drawing commands, SyncFull's submissions, the frame's render */
   double speed;                  /* VIs a second against the console's 50 or 60, in % */
   unsigned game_fps, target_vis; /* pictures the game drew a second (Gopher64's count); 50 or 60 */
   unsigned long long audio_queue, audio_underruns;
};
void psgopher_live_read(struct psgopher_live *out);
/* the last frames' times (ms between VIs), oldest first, at most max; their number */
int psgopher_live_history(float *out, int max);

/* PSGopher: the video line's counts start again (before each game) */
void psgopher_video_reset(void);

#ifdef __cplusplus
}
#endif
