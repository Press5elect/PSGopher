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
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
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
void psgopher_rdp_rates(uint32_t *fps, uint32_t *vis, int *pal) __attribute__((weak));

/* ---- the benchmark window (benchmark mode; docs/dev/perf/benchmark-analysis.md B1-B3) ----------
 * Counted in VIs, not seconds: warmup_vis presents after the game's first, then measure_vis presents
 * measured; then the game is ended (interface.cpp). Every total below is kept on the emulation thread
 * (the hooks all run there), in TSC ticks, and turned into time once, after the game, with the TSC's
 * rate measured over the window against CLOCK_MONOTONIC: one clock for every part. Nothing is logged
 * inside the window. */
enum { W_OFF, W_WARMUP, W_MEASURE, W_DONE };
static int w_state = W_OFF, w_warmup, w_measure, w_vis, w_edge;
/* a slow-moment window (normal play: psgopher.ini's slow_capture): started by the speed itself, it
 * does not end the game; its summary is said when it ends */
static int w_auto;
static int slow_seconds, slow_captures;
static uint64_t slow_last_ns;
static uint64_t w_tsc0, w_ns0, w_tsc1, w_ns1, w_cpu0, w_cpu1, w_last_present_tsc;
static uint64_t w_present, w_rsp, w_rsp_rdp, w_rsp_bursts, w_rsp_cycles, w_rdp_total, w_rdp_part[3], w_fence, w_limiter;
static uint64_t w_rdram_ns, w_rdram_waits, w_rsp_rdram_ns, w_underruns0, w_underruns1, w_fence_count;
/* the submissions and next_frame_context (TSC ticks, count), and parallel-rdp's thread CPU time in its
 * commands and frame parts (ns) */
static uint64_t w_submit, w_submit_count, w_rdp_cpu_ns;
static uint64_t *w_intervals; /* present-to-present, ticks */
static int w_in_rsp;
static char w_gpu[1024];

int psgopher_bench_window_auto(void)
{
   return w_auto;
}

void psgopher_bench_window(int warmup_vis, int measure_vis)
{
   w_auto = 0;
   w_state = measure_vis > 0 ? W_WARMUP : W_OFF;
   w_warmup = warmup_vis < 0 ? 0 : warmup_vis, w_measure = measure_vis, w_vis = 0, w_edge = 0;
   w_present = w_rsp = w_rsp_rdp = w_rsp_bursts = w_rsp_cycles = w_rdp_total = w_fence = w_limiter = 0;
   w_rdp_part[0] = w_rdp_part[1] = w_rdp_part[2] = 0;
   w_rdram_ns = w_rdram_waits = w_rsp_rdram_ns = w_fence_count = 0;
   w_submit = w_submit_count = w_rdp_cpu_ns = 0;
   w_gpu[0] = 0;
   free(w_intervals);
   w_intervals = measure_vis > 0 ? calloc((size_t)measure_vis, sizeof(uint64_t)) : NULL;
}

int psgopher_bench_window_state(void)
{
   return w_state;
}

/* 1 when the window began at the last present, 2 when it ended, else 0 (read once) */
int psgopher_bench_window_edge(void)
{
   const int e = w_edge;
   w_edge = 0;
   return e;
}

void psgopher_bench_window_gpu(const char *line)
{
   snprintf(w_gpu, sizeof(w_gpu), "%s", line);
}

static int w_measuring(void)
{
   return w_state == W_MEASURE;
}

/* a present: the window's clock (wall), its present wait, the frame-time distribution */
void psgopher_bench_window_present(uint64_t begin_tsc, uint64_t end_tsc)
{
   if (w_state == W_WARMUP) {
      if (++w_vis >= w_warmup) {
         w_state = W_MEASURE, w_edge = 1, w_vis = 0;
         w_tsc0 = end_tsc, w_ns0 = now_ns(CLOCK_MONOTONIC), w_cpu0 = now_ns(CLOCK_THREAD_CPUTIME_ID);
         w_last_present_tsc = end_tsc;
         psgopher_sdl3_audio_stats audio;
         psgopher_sdl3_audio_stats_read(&audio);
         w_underruns0 = audio.underrun_grains;
      }
      return;
   }
   if (w_state != W_MEASURE)
      return;
   w_present += end_tsc - begin_tsc;
   if (w_intervals && w_vis < w_measure)
      w_intervals[w_vis] = end_tsc - w_last_present_tsc;
   w_last_present_tsc = end_tsc;
   if (++w_vis >= w_measure) {
      w_state = W_DONE, w_edge = 2;
      w_tsc1 = end_tsc, w_ns1 = now_ns(CLOCK_MONOTONIC), w_cpu1 = now_ns(CLOCK_THREAD_CPUTIME_ID);
      psgopher_sdl3_audio_stats audio;
      psgopher_sdl3_audio_stats_read(&audio);
      w_underruns1 = audio.underrun_grains;
   }
}

void psgopher_bench_fence(uint64_t ticks)
{
   if (w_measuring())
      w_fence += ticks, w_fence_count++;
}

/* a Vulkan submission or next_frame_context on the emulation thread (parallel-rdp) */
void psgopher_bench_submit(uint64_t ticks)
{
   if (w_measuring())
      w_submit += ticks, w_submit_count++;
}

/* the emulation thread's CPU clock at the start of parallel-rdp's commands or frame part, 0 outside a
 * window (one branch); psgopher_bench_rdp_part_cpu closes it */
uint64_t psgopher_bench_cpu_now(void)
{
   return w_measuring() ? now_ns(CLOCK_THREAD_CPUTIME_ID) : 0;
}

void psgopher_bench_rdp_part_cpu(uint64_t begin_cpu_ns)
{
   if (begin_cpu_ns && w_measuring())
      w_rdp_cpu_ns += now_ns(CLOCK_THREAD_CPUTIME_ID) - begin_cpu_ns;
}

static uint64_t w_limiter_begin;
void psgopher_bench_limiter_begin(void)
{
   w_limiter_begin = __rdtsc();
}

void psgopher_bench_limiter_end(void)
{
   if (w_measuring())
      w_limiter += __rdtsc() - w_limiter_begin;
}

static int cmp_u64(const void *a, const void *b)
{
   const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
   return x < y ? -1 : x > y;
}

/* the window's summary, once the game has ended (not on the emulation thread's clock) */
double psgopher_bench_window_report(const char *label)
{
   if (w_state != W_DONE) {
      say("benchmark window %s: not complete (%d of %d VIs measured; warm-up %d)", label, w_state == W_MEASURE ? w_vis : 0,
          w_measure, w_warmup);
      return -1;
   }
   const double tpn = (double)(w_tsc1 - w_tsc0) / (double)(w_ns1 - w_ns0); /* ticks a ns */
   const double n = (double)w_measure;
#define MS(t) ((double)(t) / tpn / 1e6 / n)
   const double wall = MS(w_tsc1 - w_tsc0), present = MS(w_present), limiter = MS(w_limiter);
   const double rsp = MS(w_rsp > w_rsp_rdp ? w_rsp - w_rsp_rdp : 0) - (double)w_rsp_rdram_ns / 1e6 / n;
   const double rsp_rdram = (double)w_rsp_rdram_ns / 1e6 / n, rdram = (double)w_rdram_ns / 1e6 / n;
   const double rdp = MS(w_rdp_total), fence = MS(w_fence);
   const double sync = MS(w_rdp_part[1]), frame = MS(w_rdp_part[2]);
   const double commands = MS(w_rdp_part[0] > w_rdp_part[1] ? w_rdp_part[0] - w_rdp_part[1] : 0);
   const double submits = MS(w_submit);
   /* parallel-rdp's wall time is its commands (syncs inside) and its frame; its CPU time is what the
    * thread clock counted inside the same two parts */
   const double rdp_wall = MS(w_rdp_part[0] + w_rdp_part[2]), rdp_cpu = (double)w_rdp_cpu_ns / 1e6 / n;
   const double work = wall - present - limiter;
   const double rest = work - rsp - rsp_rdram - rdp - (rdram - rsp_rdram);
   const double cpu = (double)(w_cpu1 - w_cpu0) / 1e6 / n;
   double p50 = 0, p99 = 0, pmax = 0;
   if (w_intervals) {
      qsort(w_intervals, (size_t)w_measure, sizeof(uint64_t), cmp_u64);
      p50 = (double)w_intervals[w_measure / 2] / tpn / 1e6;
      p99 = (double)w_intervals[(w_measure * 99) / 100] / tpn / 1e6;
      pmax = (double)w_intervals[w_measure - 1] / tpn / 1e6;
   }
   uint32_t fps = 0, vis = 0;
   int pal = 0;
   if (psgopher_rdp_rates)
      psgopher_rdp_rates(&fps, &vis, &pal);
   const double target = pal ? 50.0 : 60.0 * 1000.0 / 1001.0;
   const double vis_per_s = n / ((double)(w_ns1 - w_ns0) / 1e9);
   say("benchmark window %s: %d VIs after %d of warm-up, %.2f s: %.2f VIs a second (%.1f%% of %.2f); a VI %.2f ms = "
       "work %.2f + present wait %.2f + limiter %.2f; work = RSP %.2f (%.0f bursts, %.0f cycles a VI) + RSP's RDRAM waits %.2f + "
       "parallel-rdp %.2f (commands %.2f of which GPU fence waits %.2f in %.1f, syncs %.2f, frame %.2f, submits %.2f in %.1f) + other RDRAM waits %.2f + "
       "VR4300 and the rest %.2f; thread CPU time %.2f; parallel-rdp off-CPU %.2f; frame intervals p50 %.2f p99 %.2f max %.2f ms; audio underruns %llu",
       label, w_measure, w_warmup, (double)(w_ns1 - w_ns0) / 1e9, vis_per_s, vis_per_s / target * 100.0, target, wall, work,
       present, limiter, rsp, (double)w_rsp_bursts / n, (double)w_rsp_cycles / n, rsp_rdram, rdp, commands, fence,
       (double)w_fence_count / n, sync, frame, submits, (double)w_submit_count / n, rdram - rsp_rdram, rest, cpu,
       rdp_wall - rdp_cpu, p50, p99, pmax,
       (unsigned long long)(w_underruns1 - w_underruns0));
   if (w_gpu[0])
      say("benchmark window %s: gpu a VI: %s", label, w_gpu);
#undef MS
   return vis_per_s / target * 100.0;
}

static struct psgopher_live live_now;
static uint64_t live_since_ns, live_frames, live_present_ns, live_cpu_start, live_last_end;
static _Atomic uint64_t live_rdram_ns, live_rdram_waits;
/* parallel-rdp's share in TSC ticks, and the TSC at the second's start (its rate measured against
 * the monotonic clock over the same second) */
static _Atomic uint64_t live_rdp_ticks, live_rdp_part_ticks[3];
static uint64_t live_tsc_start;
static _Atomic uint64_t live_frame_part_max; /* the longest single frame part this 10 s, TSC ticks */
static double live_ticks_per_ns;

/* the RSP's tasks (on the emulation thread only: no atomics needed for the nesting) */
static uint64_t rsp_begin_tsc, live_rsp_ticks, live_rdp_in_rsp_ticks;
static int in_rsp;

void psgopher_bench_rsp_begin(void)
{
   in_rsp = 1;
   w_in_rsp = 1;
   rsp_begin_tsc = __rdtsc();
}

/* Emulated time, in the N64 CPU's cycles, for the video line: what keeps the emulated N64 busy or waiting
 * (0 the RDP's timing model, 1 RSP tasks, 2 the CPU's idle loops skipped, 3 cartridge DMA, 4 VI periods: the
 * whole; the CPU's memory stalls: 5 instruction cache, 6 data cache, 7 uncached RDRAM, 8 its cartridge reads).
 * Added on the emulation thread, read on the video line's. */
static _Atomic uint64_t emu_cycles[9];

void psgopher_emu_add(int what, uint64_t cycles)
{
   if (what >= 0 && what < 9)
      atomic_fetch_add_explicit(&emu_cycles[what], cycles, memory_order_relaxed);
}

void psgopher_bench_rsp_end(uint64_t cycles)
{
   const uint64_t t = __rdtsc() - rsp_begin_tsc;
   live_rsp_ticks += t;
   in_rsp = 0;
   w_in_rsp = 0;
   if (w_measuring())
      w_rsp += t, w_rsp_bursts++, w_rsp_cycles += cycles;
}

void psgopher_bench_rdp(uint64_t ticks)
{
   atomic_fetch_add_explicit(&live_rdp_ticks, ticks, memory_order_relaxed);
   if (in_rsp)
      live_rdp_in_rsp_ticks += ticks;
   if (w_measuring()) {
      w_rdp_total += ticks;
      if (w_in_rsp)
         w_rsp_rdp += ticks;
   }
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
   if (part >= 0 && part < 3) {
      atomic_fetch_add_explicit(&live_rdp_part_ticks[part], ticks, memory_order_relaxed);
      if (part == 2 && ticks > atomic_load_explicit(&live_frame_part_max, memory_order_relaxed))
         atomic_store_explicit(&live_frame_part_max, ticks, memory_order_relaxed); /* (the emulation thread only) */
      if (w_measuring())
         w_rdp_part[part] += ticks;
   }
}
#define HISTORY 180
static float history_ms[HISTORY];
static int history_at, history_n;


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
      live_rsp_ticks = live_rdp_in_rsp_ticks = 0;
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
      live_ticks_per_ns = ticks_per_ns;
      live_now.rdp_ms = ticks_per_ns > 0 ? (double)rdp_ticks / ticks_per_ns / 1e6 / (double)live_frames : -1.0;
      {
         const uint64_t rsp = live_rsp_ticks > live_rdp_in_rsp_ticks ? live_rsp_ticks - live_rdp_in_rsp_ticks : 0;
         live_now.rsp_ms = ticks_per_ns > 0 ? (double)rsp / ticks_per_ns / 1e6 / (double)live_frames : -1.0;
         live_rsp_ticks = live_rdp_in_rsp_ticks = 0;
      }
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
      /* a slow moment (normal play, slow_capture on): below 95% three whole seconds running (one slow
       * second can be the quick menu's pause), at most three a game, two minutes apart: the next 600 VIs
       * are measured as a benchmark window, without ending the game */
      if (w_state == W_OFF || (w_auto && w_state == W_DONE)) {
         const char *e = getenv("PSGOPHER_SLOW_CAPTURE"); /* (once a second; set before each game) */
         const int enabled = !e || strcmp(e, "0") != 0;
         slow_seconds = live_now.target_vis && live_now.speed < 95.0 ? slow_seconds + 1 : 0;
         if (enabled && slow_seconds >= 3 && slow_captures < 3 && (!slow_last_ns || end_ns - slow_last_ns > 120000000000ull)) {
            psgopher_bench_window(0, 600);
            w_auto = 1;
            slow_captures++, slow_last_ns = end_ns, slow_seconds = 0;
         }
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


/* ---- where the emulation thread is, and the freeze watchdog --------------------------------------
 * The emulation thread marks the places it can block in (psgopher_where, bench.h); a thread of its
 * own samples the mark every 20 ms. When no VI has completed for 250 ms it says so once, with the
 * place the thread is in; when VIs resume it says how long the stall was and the places its 20 ms
 * samples found it in, and the CPU time the thread used meanwhile (a stall where the CPU time stays
 * low is a block off the CPU: the place names the call). Logging only. */
static _Atomic int where_place;
static _Atomic uint64_t where_since_tsc;
static _Atomic uint32_t where_pauses;

int psgopher_where(int place)
{
   const int previous = atomic_load_explicit(&where_place, memory_order_relaxed);
   atomic_store_explicit(&where_place, place, memory_order_relaxed);
   atomic_store_explicit(&where_since_tsc, __rdtsc(), memory_order_relaxed);
   if (place == PSGOPHER_WHERE_PAUSED)
      atomic_store_explicit(&where_pauses, atomic_load_explicit(&where_pauses, memory_order_relaxed) + 1, memory_order_relaxed);
   return previous;
}

void psgopher_where_restore(int previous)
{
   atomic_store_explicit(&where_place, previous, memory_order_relaxed);
   atomic_store_explicit(&where_since_tsc, __rdtsc(), memory_order_relaxed);
}

const char *psgopher_where_name(int place)
{
   static const char *const names[PSGOPHER_WHERE_COUNT] = {
      "the VR4300 or other code", "the RSP", "parallel-rdp commands", "parallel-rdp frame", "a GPU fence wait",
      "a Vulkan submit", "an RDRAM sync wait", "present (vsync/FIFO wait)", "the speed limiter", "the audio queue",
      "a cartridge DMA", "save data", "a save state", "paused", "a main-thread call", "controller input", "achievements",
      "the frame: command ring drain", "the frame: waiting for the RDP's idle lock", "the frame: flush and signal", "the frame: video interface scanout",
      "the frame: request_program", "the frame: request_command_buffer", "the frame: HDR layers", "the frame: swapchain render pass",
      "the frame: quick menu/overlay", "GPU memory allocation",
   };
   return place >= 0 && place < PSGOPHER_WHERE_COUNT ? names[place] : "an unknown place";
}

int pthread_getcpuclockid(pthread_t thread, clockid_t *clock) __attribute__((weak));

static _Atomic uint64_t stall_vis, stall_last_vi_ns, stall_peak_ns; /* the VI count; the last VI's time; the longest VI interval not yet looked at */
/* hitches (a VI interval over 50 ms), counted since the last video line */
#define HITCH_MS 50
static _Atomic uint32_t hitch_count, hitch_longest_ms;
static _Atomic int hitch_longest_place;
static _Atomic uint32_t hitch_samples[PSGOPHER_WHERE_COUNT];
static _Atomic bool stall_running, stall_emu_known;
static pthread_t stall_thread, stall_emu;
static bool stall_started;
static int stall_threshold_ms = 250, stall_period_ms = 20;

int psgopher_where_emu(int place)
{
   if (!atomic_load_explicit(&stall_emu_known, memory_order_relaxed) || !pthread_equal(pthread_self(), stall_emu))
      return -1;
   return psgopher_where(place);
}

void psgopher_where_emu_restore(int previous)
{
   if (previous >= 0)
      psgopher_where_restore(previous);
}

void psgopher_stall_configure(int threshold_ms, int period_ms)
{
   stall_threshold_ms = threshold_ms > 0 ? threshold_ms : 250;
   stall_period_ms = period_ms > 0 ? period_ms : 20;
}

/* a VI completed on the emulation thread (the first call says which thread that is) */
void psgopher_stall_vi(void)
{
   if (!atomic_load_explicit(&stall_emu_known, memory_order_relaxed)) {
      stall_emu = pthread_self();
      atomic_store(&stall_emu_known, true);
   }
   const uint64_t now = now_ns(CLOCK_MONOTONIC), previous = atomic_load_explicit(&stall_last_vi_ns, memory_order_relaxed);
   if (previous && now - previous > atomic_load_explicit(&stall_peak_ns, memory_order_relaxed))
      atomic_store_explicit(&stall_peak_ns, now - previous, memory_order_relaxed);
   atomic_store_explicit(&stall_last_vi_ns, now, memory_order_relaxed);
   atomic_store_explicit(&stall_vis, atomic_load_explicit(&stall_vis, memory_order_relaxed) + 1, memory_order_relaxed);
}

static void stall_sleep_ms(int ms)
{
   struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
   nanosleep(&t, NULL);
}

/* the emulation thread's CPU clock, ns; UINT64_MAX when the console cannot give another thread's */
static uint64_t stall_emu_cpu(int *resolved, clockid_t *id)
{
   if (!atomic_load(&stall_emu_known) || !pthread_getcpuclockid)
      return UINT64_MAX;
   if (!*resolved) {
      if (pthread_getcpuclockid(stall_emu, id) != 0)
         return UINT64_MAX;
      *resolved = 1;
   }
   return now_ns(*id);
}

static void stall_report(uint64_t total_ms, const unsigned *histogram, uint64_t cpu_ns, const char *how)
{
   char list[640] = "";
   unsigned left[PSGOPHER_WHERE_COUNT];
   memcpy(left, histogram, sizeof(left));
   size_t used = 0;
   for (int k = 0; k < PSGOPHER_WHERE_COUNT; k++) {
      int best = -1;
      for (int i = 0; i < PSGOPHER_WHERE_COUNT; i++)
         if (left[i] && (best < 0 || left[i] > left[best]))
            best = i;
      if (best < 0)
         break;
      const int n = snprintf(list + used, sizeof(list) - used, "%s%s %u", used ? ", " : "", psgopher_where_name(best), left[best]);
      if (n < 0 || (size_t)n >= sizeof(list) - used)
         break;
      used += (size_t)n;
      left[best] = 0;
   }
   char cpu[96] = "; the emulation thread's CPU time is not readable here";
   if (cpu_ns != UINT64_MAX)
      snprintf(cpu, sizeof(cpu), "; the emulation thread used %.1f ms of CPU", (double)cpu_ns / 1e6);
   say("stall over: %llu ms without a VI%s; where it was (%d ms samples): %s%s", (unsigned long long)total_ms, how, stall_period_ms,
       list, cpu);
}

static void *stall_main(void *unused)
{
   (void)unused;
   unsigned histogram[PSGOPHER_WHERE_COUNT] = { 0 };
   int clock_resolved = 0;
   clockid_t clock_id = 0;
   uint64_t seen_vis = atomic_load(&stall_vis), seen_pauses = atomic_load(&where_pauses);
   uint64_t since_ns = now_ns(CLOCK_MONOTONIC), cpu_then = UINT64_MAX;
   bool stalled = false;
   /* TSC ticks a ms, from a short sleep (the place's age is in ticks) */
   double ticks_per_ms;
   {
      const uint64_t t0 = __rdtsc(), n0 = now_ns(CLOCK_MONOTONIC);
      stall_sleep_ms(10);
      ticks_per_ms = (double)(__rdtsc() - t0) / ((double)(now_ns(CLOCK_MONOTONIC) - n0) / 1e6);
   }
   while (atomic_load(&stall_running)) {
      stall_sleep_ms(stall_period_ms);
      const uint64_t vis = atomic_load(&stall_vis), pauses = atomic_load_explicit(&where_pauses, memory_order_relaxed);
      const int place = atomic_load_explicit(&where_place, memory_order_relaxed);
      const uint64_t now = now_ns(CLOCK_MONOTONIC);
      /* a held game (paused, the quick menu) is no stall; before the first VI there is nothing to wait for */
      const bool held = place == PSGOPHER_WHERE_PAUSED || pauses != seen_pauses;
      if (vis != seen_vis || held || vis == 0) {
         /* a VI after a gap over 50 ms (not across a pause) is a hitch; it was in its most sampled place */
         const uint64_t peak = atomic_exchange_explicit(&stall_peak_ns, 0, memory_order_relaxed);
         if (vis != seen_vis && !held && pauses == seen_pauses && peak > (uint64_t)HITCH_MS * 1000000u) {
            int top = 0;
            for (int i = 1; i < PSGOPHER_WHERE_COUNT; i++)
               if (histogram[i] > histogram[top])
                  top = i;
            const uint32_t ms = (uint32_t)(peak / 1000000u);
            atomic_fetch_add_explicit(&hitch_count, 1, memory_order_relaxed);
            if (ms > atomic_load_explicit(&hitch_longest_ms, memory_order_relaxed)) {
               atomic_store_explicit(&hitch_longest_ms, ms, memory_order_relaxed);
               atomic_store_explicit(&hitch_longest_place, top, memory_order_relaxed);
            }
         }
         if (stalled) {
            const uint64_t cpu_now = stall_emu_cpu(&clock_resolved, &clock_id);
            stall_report((now - since_ns) / 1000000u, histogram,
                         cpu_then == UINT64_MAX || cpu_now == UINT64_MAX ? UINT64_MAX : cpu_now - cpu_then,
                         vis != seen_vis ? "" : " (ended by a pause)");
         }
         stalled = false;
         memset(histogram, 0, sizeof(histogram));
         seen_vis = vis, seen_pauses = pauses, since_ns = now;
         cpu_then = stall_emu_cpu(&clock_resolved, &clock_id);
         continue;
      }
      histogram[place >= 0 && place < PSGOPHER_WHERE_COUNT ? place : 0]++;
      {
         const uint64_t last = atomic_load_explicit(&stall_last_vi_ns, memory_order_relaxed);
         if (last && now > last && now - last > (uint64_t)HITCH_MS * 1000000u)
            atomic_fetch_add_explicit(&hitch_samples[place >= 0 && place < PSGOPHER_WHERE_COUNT ? place : 0], 1, memory_order_relaxed);
      }
      if (!stalled && (now - since_ns) / 1000000u >= (uint64_t)stall_threshold_ms) {
         stalled = true;
         const uint64_t ticks = __rdtsc() - atomic_load_explicit(&where_since_tsc, memory_order_relaxed);
         say("stall: no VI for %d ms; the emulation thread in %s for %.0f ms", stall_threshold_ms, psgopher_where_name(place),
             ticks_per_ms > 0 ? (double)ticks / ticks_per_ms : -1.0);
      }
   }
   return NULL;
}

void psgopher_stall_start(void)
{
   if (stall_started)
      return;
   /* (logging only, so on whatever Slow Moments says: it controls the 600-VI window alone) */
   atomic_store(&stall_vis, 0);
   atomic_store(&stall_last_vi_ns, 0);
   atomic_store(&stall_peak_ns, 0);
   atomic_store(&hitch_count, 0);
   atomic_store(&hitch_longest_ms, 0);
   atomic_store(&hitch_longest_place, 0);
   for (int i = 0; i < PSGOPHER_WHERE_COUNT; i++)
      atomic_store(&hitch_samples[i], 0);
   atomic_store(&stall_emu_known, false);
   atomic_store(&where_place, PSGOPHER_WHERE_CPU);
   atomic_store(&where_pauses, 0);
   atomic_store(&stall_running, true);
   pthread_attr_t attributes;
   pthread_attr_init(&attributes);
   pthread_attr_setstacksize(&attributes, 128 << 10);
   stall_started = pthread_create(&stall_thread, &attributes, stall_main, NULL) == 0;
   pthread_attr_destroy(&attributes);
   if (!stall_started)
      atomic_store(&stall_running, false);
}

void psgopher_stall_stop(void)
{
   if (!stall_started)
      return;
   atomic_store(&stall_running, false);
   pthread_join(stall_thread, NULL);
   stall_started = false;
}

/* the hitches since the last call, as the tail of the video line; starts the next count */
void psgopher_hitch_summary(char *out, size_t size)
{
   const uint32_t count = atomic_exchange(&hitch_count, 0), longest = atomic_exchange(&hitch_longest_ms, 0);
   const int longest_place = atomic_load(&hitch_longest_place);
   unsigned samples[PSGOPHER_WHERE_COUNT];
   for (int i = 0; i < PSGOPHER_WHERE_COUNT; i++)
      samples[i] = atomic_exchange(&hitch_samples[i], 0);
   if (!count) {
      snprintf(out, size, "; hitches: none");
      return;
   }
   int used = snprintf(out, size, "; hitches: %u over %d ms, longest %u ms (in %s); time in hitches by place (%d ms samples): ", count,
                       HITCH_MS, longest, psgopher_where_name(longest_place), stall_period_ms);
   bool any = false;
   for (int k = 0; k < 4 && used > 0 && (size_t)used < size; k++) {
      int best = -1;
      for (int i = 0; i < PSGOPHER_WHERE_COUNT; i++)
         if (samples[i] && (best < 0 || samples[i] > samples[best]))
            best = i;
      if (best < 0)
         break;
      used += snprintf(out + used, size - (size_t)used, "%s%s %u", any ? ", " : "", psgopher_where_name(best), samples[best]);
      any = true;
      samples[best] = 0;
   }
   if (!any && used > 0 && (size_t)used < size)
      snprintf(out + used, size - (size_t)used, "none sampled");
}

static uint64_t video_since_ns, video_count, video_total, video_underruns_at;

/* a new game: its first frame is said again */
void psgopher_video_reset(void)
{
   slow_seconds = slow_captures = 0;
   slow_last_ns = 0;
   if (w_auto)
      psgopher_bench_window(0, 0);
   video_since_ns = video_count = video_total = 0;
   live_since_ns = live_frames = live_present_ns = live_last_end = 0;
   atomic_store(&live_rdram_ns, 0);
   atomic_store(&live_rdram_waits, 0);
   atomic_store(&live_frame_part_max, 0);
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
      /* the frame part's figure above is the last second's, averaged over its VIs: one long call shows as a long average */
      char longest_frame[64] = "", hitches[512];
      const double frame_max_ms = live_ticks_per_ns > 0 ? (double)atomic_exchange(&live_frame_part_max, 0) / live_ticks_per_ns / 1e6 : 0.0;
      if (frame_max_ms > 50.0)
         snprintf(longest_frame, sizeof(longest_frame), "; longest frame part %.0f ms", frame_max_ms);
      psgopher_hitch_summary(hitches, sizeof(hitches));
      say("video: %llu VIs in %.1f s (%.2f a second, %.0f%% speed); game %u fps; a VI: CPU %.2f ms (RSP %.2f, RDP %.2f: draw %.2f, sync %.2f, frame %.2f), present wait %.2f ms, "
          "RDRAM sync %.2f ms (%.1f); shading %.0f%% general (%llu compiled, %.0f ms each, %llu waiting); "
          "audio queue %llu, %llu underrun(s) in 10 s%s%s",
          (unsigned long long)video_count, (double)span / 1e9, (double)video_count * 1e9 / (double)span,
          l.target_vis ? (double)video_count * 1e9 / (double)span / l.target_vis * 100.0 : 0.0, l.game_fps, l.cpu_ms,
          l.rsp_ms, l.rdp_ms, l.rdp_part_ms[0], l.rdp_part_ms[1], l.rdp_part_ms[2], l.present_ms, l.rdram_ms, l.rdram_waits,
          l.general_share < 0 ? 0.0 : l.general_share * 100.0, l.compiles_done, l.compile_ms, l.compiles_waiting, (unsigned long long)audio.queued_frames,
          (unsigned long long)(audio.underrun_grains - video_underruns_at), longest_frame, hitches);
      video_underruns_at = audio.underrun_grains;
      {
         /* the emulated N64's own time: where a game that runs below its frame rate at full speed waits */
         uint64_t e[9];
         for (int i = 0; i < 9; i++)
            e[i] = atomic_exchange(&emu_cycles[i], 0);
         if (e[4]) {
            const double all = (double)e[4];
            say("video: emulated time: CPU waiting %.0f%%, RSP busy %.0f%%, RDP busy %.0f%% (its timing model), cartridge DMA %.0f%%; "
                "the CPU's memory stalls: instruction cache %.0f%%, data cache %.0f%%, uncached RDRAM %.0f%%, cartridge reads %.0f%%",
                e[2] * 100.0 / all, e[1] * 100.0 / all, e[0] * 100.0 / all, e[3] * 100.0 / all, e[5] * 100.0 / all, e[6] * 100.0 / all,
                e[7] * 100.0 / all, e[8] * 100.0 / all);
         }
      }
      video_since_ns = end_ns;
      video_count = 0;
   }
}

void psgopher_bench_present(uint64_t begin_ns, uint64_t end_ns)
{
   psgopher_stall_vi();
   if (w_state == W_OFF || w_state == W_DONE)
      video_heartbeat(end_ns); /* (no logging inside a benchmark window) */
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
   if (w_measuring()) {
      w_rdram_ns += ns, w_rdram_waits++;
      if (w_in_rsp)
         w_rsp_rdram_ns += ns;
   }
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

/* the run's summary (benchmark mode): the live figures of each second after the first five
 * (a probe loading, shaders made), averaged */
static int run_seconds;
static double run_vis, run_fps, run_cpu, run_rsp, run_rdp, run_part[3], run_speed;

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
      if (second >= 5) {
         struct psgopher_live l;
         psgopher_live_read(&l);
         run_seconds++;
         run_vis += l.frames_per_second, run_fps += l.game_fps, run_speed += l.speed;
         run_cpu += l.cpu_ms, run_rsp += l.rsp_ms, run_rdp += l.rdp_ms;
         for (int i = 0; i < 3; i++)
            run_part[i] += l.rdp_part_ms[i];
      }
   }
   return NULL;
}

bool psgopher_bench_start(const char *path)
{
   if (atomic_load(&running))
      return true;
   snprintf(csv_path, sizeof(csv_path), "%s", path);
   run_seconds = 0;
   run_vis = run_fps = run_cpu = run_rsp = run_rdp = run_speed = 0;
   run_part[0] = run_part[1] = run_part[2] = 0;
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
   if (run_seconds > 0) {
      const double n = run_seconds;
      say("benchmark summary: %d s measured: %.2f VIs a second (%.0f%% speed), game %.1f fps; a VI: CPU %.2f ms "
          "(RSP %.2f, RDP %.2f: draw %.2f, sync %.2f, frame %.2f)",
          run_seconds, run_vis / n, run_speed / n, run_fps / n, run_cpu / n, run_rsp / n, run_rdp / n, run_part[0] / n,
          run_part[1] / n, run_part[2] / n);
   } else
      say("benchmark summary: the run was shorter than its first five seconds");
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
