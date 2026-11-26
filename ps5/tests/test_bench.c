/*
 * PSGopher - host test of the benchmark window (ps5/host/bench.c, benchmark analysis B1): a run of
 * fake VIs with known work (RSP, RDP with a sync and a fence wait inside, an RDRAM wait inside the
 * RSP, the limiter's sleep, the present), then the window's summary, checked part by part against
 * what was done, and the edges and VI counting.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bench.h"
#include "psgopher_sdl3.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <x86intrin.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static char last_say[4096];
void say(const char *format, ...)
{
   va_list ap;
   va_start(ap, format);
   vsnprintf(last_say, sizeof(last_say), format, ap);
   va_end(ap);
   if (strstr(last_say, "benchmark window"))
      puts(last_say);
}
void psgopher_sdl3_audio_stats_read(psgopher_sdl3_audio_stats *stats) { memset(stats, 0, sizeof(*stats)); }

static uint64_t ns_now(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
/* busy for us microseconds; its TSC ticks */
static uint64_t busy(int us)
{
   const uint64_t t0 = __rdtsc(), end = ns_now() + (uint64_t)us * 1000u;
   while (ns_now() < end)
      ;
   return __rdtsc() - t0;
}

/* the number after `key` in the summary */
static double field(const char *key)
{
   const char *at = strstr(last_say, key);
   return at ? atof(at + strlen(key)) : -1;
}

int main(void)
{
   /* 3 VIs of warm-up, then 20 measured: each VI RSP 2 ms (of which an RDRAM wait 0.3 ms and RDP
    * work 0.4 ms started inside it), RDP 1.5 ms (a sync 0.2 ms, a fence wait 0.5 ms inside),
    * the VR4300 1 ms, the limiter 0.8 ms, the present 1 ms: 6.3 ms a VI */
   psgopher_bench_window(3, 20);
   int began = 0, ended = 0, vis = 0;
   for (int vi = 0; vi < 40 && psgopher_bench_window_state() != 3; vi++) {
      psgopher_bench_rsp_begin();
      busy(1300);
      const uint64_t t_rdp_in = busy(400);
      psgopher_bench_rdp(t_rdp_in);
      psgopher_bench_rdram_wait(300000);
      busy(300); /* (the RDRAM wait's time, reported in ns above) */
      psgopher_bench_rsp_end(1234);
      const uint64_t rdp0 = __rdtsc();
      busy(800);
      psgopher_bench_rdp_part(1, busy(200));
      psgopher_bench_fence(busy(500));
      const uint64_t rdp_t = __rdtsc() - rdp0;
      psgopher_bench_rdp_part(0, rdp_t);
      psgopher_bench_rdp(rdp_t);
      busy(1000); /* the VR4300 */
      psgopher_bench_limiter_begin();
      busy(800);
      psgopher_bench_limiter_end();
      const uint64_t p0 = __rdtsc();
      busy(1000);
      psgopher_bench_window_present(p0, __rdtsc());
      const int e = psgopher_bench_window_edge();
      began += e == 1, ended += e == 2;
      vis++;
   }
   CHECK(began == 1 && ended == 1, "edges %d %d", began, ended);
   CHECK(vis == 23, "the window ends after 3 + 20 VIs: %d", vis);
   psgopher_bench_window_gpu("depth-blending-upscaled 1.000 ms (1.0x)");
   psgopher_bench_window_report("test");
   /* the gpu line came last; the summary before it */
   CHECK(strstr(last_say, "gpu a VI: depth-blending-upscaled"), "%s", last_say);
   psgopher_bench_window_gpu("");
   psgopher_bench_window_report("test");
   const double vi = field("a VI "), work = field("= work "), present = field("present wait "), limiter = field("limiter ");
   const double rsp = field("RSP "), rdram_rsp = field("RSP's RDRAM waits "), rdp = field("parallel-rdp ");
   const double commands = field("commands "), fence = field("GPU fence waits "), sync = field("syncs "), rest = field("VR4300 and the rest ");
   printf("parsed: VI %.2f work %.2f present %.2f limiter %.2f RSP %.2f rdram %.2f rdp %.2f commands %.2f fence %.2f sync %.2f rest %.2f\n",
          vi, work, present, limiter, rsp, rdram_rsp, rdp, commands, fence, sync, rest);
   const double tol = 0.15; /* ms: busy-waits on a shared PC */
   CHECK(vi > 6.1 && vi < 6.5, "a VI is 6.3 ms of known work (RSP 2.0 with what started inside it, RDP 1.5, VR4300 1.0, limiter 0.8, present 1.0): %.2f", vi);
   CHECK(present > 1 - tol && present < 1 + tol, "present %.2f", present);
   CHECK(limiter > 0.8 - tol && limiter < 0.8 + tol, "limiter %.2f", limiter);
   CHECK(rsp > 1.3 - tol && rsp < 1.3 + tol, "RSP without its RDP and RDRAM wait: %.2f", rsp);
   CHECK(rdram_rsp > 0.29 && rdram_rsp < 0.31, "RSP's RDRAM waits %.2f", rdram_rsp);
   CHECK(rdp > 1.9 - tol && rdp < 1.9 + tol, "parallel-rdp 0.4 + 1.5: %.2f", rdp);
   CHECK(sync > 0.2 - tol && sync < 0.2 + tol, "syncs %.2f", sync);
   CHECK(fence > 0.5 - tol && fence < 0.5 + tol, "fence %.2f", fence);
   CHECK(commands > 1.3 - tol && commands < 1.3 + tol, "commands without the syncs %.2f", commands);
   CHECK(rest > 1.0 - 2 * tol && rest < 1.0 + 2 * tol, "the VR4300's 1 ms is the rest: %.2f", rest);
   CHECK(work > vi - present - limiter - 0.01 && work < vi - present - limiter + 0.01, "work = VI - present - limiter");
   if (failures) {
      fprintf(stderr, "test_bench: %d failure(s)\n", failures);
      return 1;
   }
   puts("test_bench: all checks passed");
   return 0;
}
