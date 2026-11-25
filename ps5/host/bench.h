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

/* PSGopher: the video line's counts start again (before each game) */
void psgopher_video_reset(void);

#ifdef __cplusplus
}
#endif
