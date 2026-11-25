/*
 * PSGopher - the benchmark mode's counters (docs/dev/performance.md; docs/dev/architecture.md D11).
 *
 * On when /data/psgopher/benchmark.txt exists. parallel-rdp's interface reports each present (the
 * time Gopher64's thread spent in end_frame + begin_frame: the FIFO wait and the acquire) and each
 * wait for the GPU before the CPU touches RDRAM; the SDL layer counts audio grains. Once a second
 * a thread of its own writes one `bench:` line to klog and keeps a CSV row, written once at the end
 * (a write() costs ~3.3 ms on the console: PROBE.md). The hooks cost two clock reads a frame.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bench.h"

#include "platform.h"
#include "psgopher_sdl3.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#define MAX_FRAMES 1024 /* presents kept per second */

static atomic_bool enabled;
static atomic_bool running;
static pthread_t thread;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t frame_ns[MAX_FRAMES];
static int frames;
static uint64_t last_present_end;
static uint64_t block_ns;     /* in present this second */
static uint64_t rdram_ns;     /* waiting for the GPU this second */
static uint64_t rdram_waits;  /* ... how many times */
static int64_t emu_cpu_ns = -1; /* the emulation thread's CPU time, as of its last present */
static char *csv;
static size_t csv_length, csv_capacity;
static char csv_path[256];

static uint64_t now_ns(clockid_t clock)
{
   struct timespec t;
   if (clock_gettime(clock, &t) != 0)
      return UINT64_MAX;
   return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

/* PSGopher: the boot log's video line, whatever the benchmark mode: the first frame shown, then
 * the frames of each 10 s (one line; a game that stops showing frames stops it) */
/* PSGopher: the live figures (the quick menu, the stats overlay, the boot log's video line), each
 * the last whole second's; always on: one thread CPU clock read a VI (waiting, in present, the speed
 * limiter or an RDRAM sync, is no CPU time) */
static struct psgopher_live live_now;
static uint64_t live_since_ns, live_frames, live_present_ns, live_cpu_start, live_last_end;
static _Atomic uint64_t live_rdram_ns, live_rdram_waits;
/* parallel-rdp's share in TSC ticks, and the TSC at the second's start (its rate measured against
 * the monotonic clock over the same second) */
static _Atomic uint64_t live_rdp_ticks, live_rdp_part_ticks[3];
static uint64_t live_tsc_start;

void psgopher_bench_rdp(uint64_t ticks)
{
   atomic_fetch_add_explicit(&live_rdp_ticks, ticks, memory_order_relaxed);
}

static _Atomic uint64_t live_dispatch_general, live_dispatch_special, compiles_asked, compiles_done, compiles_ticks;

void psgopher_bench_shading_dispatch(int general)
{
   atomic_fetch_add_explicit(general ? &live_dispatch_general : &live_dispatch_special, 1, memory_order_relaxed);
}

void psgopher_bench_shader_compile_asked(void)
{
   atomic_fetch_add_explicit(&compiles_asked, 1, memory_order_relaxed);
}

void psgopher_bench_shader_compiled(uint64_t ticks)
{
   atomic_fetch_add_explicit(&compiles_ticks, ticks, memory_order_relaxed);
   atomic_fetch_add_explicit(&compiles_done, 1, memory_order_relaxed);
}

void psgopher_bench_rdp_part(int part, uint64_t ticks)
{
   if (part >= 0 && part < 3)
      atomic_fetch_add_explicit(&live_rdp_part_ticks[part], ticks, memory_order_relaxed);
}
#define HISTORY 180
static float history_ms[HISTORY];
static int history_at, history_n;

void psgopher_rdp_rates(uint32_t *fps, uint32_t *vis, int *pal) __attribute__((weak));

void psgopher_live_read(struct psgopher_live *out)
{
   *out = live_now;
}

int psgopher_live_history(float *out, int max)
{
   const int n = history_n < max ? history_n : max;
   for (int i = 0; i < n; i++)
      out[i] = history_ms[(history_at - n + i + HISTORY) % HISTORY];
   return n;
}

static void live_frame(uint64_t begin_ns, uint64_t end_ns)
{
   const uint64_t cpu = now_ns(CLOCK_THREAD_CPUTIME_ID);
   if (live_last_end) {
      history_ms[history_at] = (float)((double)(end_ns - live_last_end) / 1e6);
      history_at = (history_at + 1) % HISTORY;
      if (history_n < HISTORY)
         history_n++;
   }
   live_last_end = end_ns;
   if (!live_since_ns) {
      live_since_ns = end_ns;
      live_cpu_start = cpu;
      live_tsc_start = __rdtsc();
      atomic_store(&live_rdp_ticks, 0);
      for (int i = 0; i < 3; i++)
         atomic_store(&live_rdp_part_ticks[i], 0);
      return;
   }
   live_frames++;
   live_present_ns += end_ns - begin_ns;
   const uint64_t span = end_ns - live_since_ns;
   if (span >= 1000000000ull) {
      live_now.frames_per_second = (double)live_frames * 1e9 / (double)span;
      live_now.present_ms = (double)live_present_ns / 1e6 / (double)live_frames;
      live_now.cpu_ms = cpu == UINT64_MAX || live_cpu_start == UINT64_MAX
                           ? -1.0 : (double)(cpu - live_cpu_start) / 1e6 / (double)live_frames;
      const uint64_t rdram = atomic_exchange(&live_rdram_ns, 0), waits = atomic_exchange(&live_rdram_waits, 0);
      live_now.rdram_ms = (double)rdram / 1e6 / (double)live_frames;
      live_now.rdram_waits = (double)waits / (double)live_frames;
      const uint64_t tsc = __rdtsc(), rdp_ticks = atomic_exchange(&live_rdp_ticks, 0);
      const double ticks_per_ns = (double)(tsc - live_tsc_start) / (double)span;
      live_now.rdp_ms = ticks_per_ns > 0 ? (double)rdp_ticks / ticks_per_ns / 1e6 / (double)live_frames : -1.0;
      {
         const uint64_t g = atomic_exchange(&live_dispatch_general, 0), sp = atomic_exchange(&live_dispatch_special, 0);
         live_now.general_share = g + sp ? (double)g / (double)(g + sp) : -1.0;
         const uint64_t asked = atomic_load(&compiles_asked), done = atomic_load(&compiles_done);
         live_now.compiles_done = done;
         live_now.compiles_waiting = asked > done ? asked - done : 0;
         live_now.compile_ms = done && ticks_per_ns > 0 ? (double)atomic_load(&compiles_ticks) / ticks_per_ns / 1e6 / (double)done : 0.0;
      }
      for (int i = 0; i < 3; i++) {
         const uint64_t part = atomic_exchange(&live_rdp_part_ticks[i], 0);
         live_now.rdp_part_ms[i] = ticks_per_ns > 0 ? (double)part / ticks_per_ns / 1e6 / (double)live_frames : -1.0;
      }
      live_tsc_start = tsc;
      if (psgopher_rdp_rates) {
         uint32_t fps = 0, vis = 0;
         int pal = 0;
         psgopher_rdp_rates(&fps, &vis, &pal);
         live_now.game_fps = fps;
         live_now.target_vis = pal ? 50 : 60;
         live_now.speed = (double)live_frames * 1e9 / (double)span / live_now.target_vis * 100.0;
      }
      psgopher_sdl3_audio_stats audio;
      psgopher_sdl3_audio_stats_read(&audio);
      live_now.audio_queue = audio.queued_frames;
      live_now.audio_underruns = audio.underrun_grains;
      live_since_ns = end_ns;
      live_cpu_start = cpu;
      live_frames = live_present_ns = 0;
   }
}

static uint64_t video_since_ns, video_count, video_total, video_underruns_at;

/* a new game: its first frame is said again */
void psgopher_video_reset(void)
{
   video_since_ns = video_count = video_total = 0;
   live_since_ns = live_frames = live_present_ns = live_last_end = 0;
   atomic_store(&live_rdram_ns, 0);
   atomic_store(&live_rdram_waits, 0);
   atomic_store(&live_rdp_ticks, 0);
   for (int i = 0; i < 3; i++)
      atomic_store(&live_rdp_part_ticks[i], 0);
   atomic_store(&live_dispatch_general, 0);
   atomic_store(&live_dispatch_special, 0);
   atomic_store(&compiles_asked, 0);
   atomic_store(&compiles_done, 0);
   atomic_store(&compiles_ticks, 0);
   history_at = history_n = 0;
   live_now = (struct psgopher_live){ 0 };
   psgopher_sdl3_audio_stats audio;
   psgopher_sdl3_audio_stats_read(&audio);
   video_underruns_at = audio.underrun_grains;
}

static void video_heartbeat(uint64_t end_ns)
{
   if (++video_total == 1) {
      say("video: the first frame is shown");
      video_since_ns = end_ns;
      return;
   }
   video_count++;
   const uint64_t span = end_ns - video_since_ns;
   if (span >= 10000000000ull) {
      /* the last second's figures beside the ten seconds' count: where a VI's time goes */
      const struct psgopher_live l = live_now;
      psgopher_sdl3_audio_stats audio;
      psgopher_sdl3_audio_stats_read(&audio);
      say("video: %llu VIs in %.1f s (%.2f a second, %.0f%% speed); game %u fps; a VI: CPU %.2f ms (RDP %.2f: draw %.2f, sync %.2f, frame %.2f), present wait %.2f ms, "
          "RDRAM sync %.2f ms (%.1f); shading %.0f%% general (%llu compiled, %.0f ms each, %llu waiting); "
          "audio queue %llu, %llu underrun(s) in 10 s",
          (unsigned long long)video_count, (double)span / 1e9, (double)video_count * 1e9 / (double)span,
          l.target_vis ? (double)video_count * 1e9 / (double)span / l.target_vis * 100.0 : 0.0, l.game_fps, l.cpu_ms,
          l.rdp_ms, l.rdp_part_ms[0], l.rdp_part_ms[1], l.rdp_part_ms[2], l.present_ms, l.rdram_ms, l.rdram_waits,
          l.general_share < 0 ? 0.0 : l.general_share * 100.0, l.compiles_done, l.compile_ms, l.compiles_waiting, (unsigned long long)audio.queued_frames,
          (unsigned long long)(audio.underrun_grains - video_underruns_at));
      video_underruns_at = audio.underrun_grains;
      video_since_ns = end_ns;
      video_count = 0;
   }
}

void psgopher_bench_present(uint64_t begin_ns, uint64_t end_ns)
{
   video_heartbeat(end_ns);
   live_frame(begin_ns, end_ns);
   if (!atomic_load_explicit(&enabled, memory_order_relaxed))
      return;
   const uint64_t cpu = now_ns(CLOCK_THREAD_CPUTIME_ID);
   pthread_mutex_lock(&lock);
   if (last_present_end && frames < MAX_FRAMES)
      frame_ns[frames++] = end_ns - last_present_end;
   last_present_end = end_ns;
   block_ns += end_ns - begin_ns;
   emu_cpu_ns = cpu == UINT64_MAX ? -1 : (int64_t)cpu;
   pthread_mutex_unlock(&lock);
}

void psgopher_bench_rdram_wait(uint64_t ns)
{
   atomic_fetch_add(&live_rdram_ns, ns);
   atomic_fetch_add(&live_rdram_waits, 1);
   if (!atomic_load_explicit(&enabled, memory_order_relaxed))
      return;
   pthread_mutex_lock(&lock);
   rdram_ns += ns;
   rdram_waits++;
   pthread_mutex_unlock(&lock);
}

static int compare(const void *a, const void *b)
{
   const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
   return x < y ? -1 : x > y;
}

static void csv_add(const char *line)
{
   const size_t n = strlen(line);
   if (csv_length + n + 1 > csv_capacity) {
      const size_t capacity = (csv_capacity + n + 1) * 2;
      char *grown = realloc(csv, capacity);
      if (!grown)
         return;
      csv = grown;
      csv_capacity = capacity;
   }
   memcpy(csv + csv_length, line, n + 1);
   csv_length += n;
}

static void *bench_main(void *unused)
{
   (void)unused;
   uint64_t sorted[MAX_FRAMES];
   const uint64_t start = now_ns(CLOCK_MONOTONIC);
   uint64_t tick = start;
   int64_t last_cpu = -1;
   psgopher_sdl3_audio_stats last_audio = {0};
   csv_add("second,presents,frame_min_ms,frame_median_ms,frame_p99_ms,frame_max_ms,present_block_ms,"
           "emu_cpu_ms,rdram_wait_ms,rdram_waits,audio_underrun_grains,audio_queued_ms\n");
   while (atomic_load(&running)) {
      tick += 1000000000u;
      const uint64_t now = now_ns(CLOCK_MONOTONIC);
      if (tick > now)
         usleep((useconds_t)((tick - now) / 1000));
      pthread_mutex_lock(&lock);
      const int n = frames;
      memcpy(sorted, frame_ns, (size_t)n * sizeof(sorted[0]));
      const uint64_t blocked = block_ns, waited = rdram_ns, waits = rdram_waits;
      const int64_t cpu = emu_cpu_ns;
      frames = 0;
      block_ns = rdram_ns = rdram_waits = 0;
      pthread_mutex_unlock(&lock);
      qsort(sorted, (size_t)n, sizeof(sorted[0]), compare);
      psgopher_sdl3_audio_stats audio;
      psgopher_sdl3_audio_stats_read(&audio);
      const double ms = 1e-6;
      const double cpu_ms = (cpu >= 0 && last_cpu >= 0) ? (cpu - last_cpu) * ms : -1;
      last_cpu = cpu;
      const int second = (int)((now_ns(CLOCK_MONOTONIC) - start) / 1000000000u);
      char line[320];
      snprintf(line, sizeof(line), "%d,%d,%.2f,%.2f,%.2f,%.2f,%.1f,%.0f,%.1f,%llu,%llu,%.1f\n", second, n,
               n ? sorted[0] * ms : 0, n ? sorted[n / 2] * ms : 0, n ? sorted[(n * 99) / 100] * ms : 0,
               n ? sorted[n - 1] * ms : 0, blocked * ms, cpu_ms, waited * ms, (unsigned long long)waits,
               (unsigned long long)(audio.underrun_grains - last_audio.underrun_grains),
               audio.queued_frames / 48.0);
      last_audio = audio;
      csv_add(line);
      line[strlen(line) - 1] = 0;
      say("bench: %s", line);
   }
   return NULL;
}

bool psgopher_bench_start(const char *path)
{
   if (atomic_load(&running))
      return true;
   snprintf(csv_path, sizeof(csv_path), "%s", path);
   say("bench: columns second,presents,frame_min_ms,frame_median_ms,frame_p99_ms,frame_max_ms,"
       "present_block_ms,emu_cpu_ms,rdram_wait_ms,rdram_waits,audio_underrun_grains,audio_queued_ms");
   if (now_ns(CLOCK_THREAD_CPUTIME_ID) == UINT64_MAX)
      say("bench: CLOCK_THREAD_CPUTIME_ID is not available: emu_cpu_ms reads -1");
   atomic_store(&enabled, true);
   atomic_store(&running, true);
   pthread_attr_t attributes;
   pthread_attr_init(&attributes);
   pthread_attr_setstacksize(&attributes, 256 << 10);
   const bool ok = pthread_create(&thread, &attributes, bench_main, NULL) == 0;
   pthread_attr_destroy(&attributes);
   if (!ok) {
      atomic_store(&running, false);
      atomic_store(&enabled, false);
   }
   return ok;
}

void psgopher_bench_stop(void)
{
   if (!atomic_load(&running))
      return;
   atomic_store(&running, false);
   pthread_join(thread, NULL);
   atomic_store(&enabled, false);
   const int fd = open(csv_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
   if (fd >= 0) {
      const ssize_t written = write(fd, csv, csv_length);
      close(fd);
      chmod(csv_path, 0666);
      say("bench: %zd bytes to %s", written, csv_path);
   }
   free(csv);
   csv = NULL;
   csv_length = csv_capacity = 0;
}
