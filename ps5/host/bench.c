/*
 * PSGopher - the benchmark mode's counters (PS5_PERFORMANCE.md, D11).
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
static uint64_t video_since_ns, video_count, video_total;

/* a new game: its first frame is said again */
void psgopher_video_reset(void)
{
   video_since_ns = video_count = video_total = 0;
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
      say("video: %llu frames in %.1f s (%.2f a second), %llu since the start", (unsigned long long)video_count,
          (double)span / 1e9, (double)video_count * 1e9 / (double)span, (unsigned long long)video_total);
      video_since_ns = end_ns;
      video_count = 0;
   }
}

void psgopher_bench_present(uint64_t begin_ns, uint64_t end_ns)
{
   video_heartbeat(end_ns);
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
