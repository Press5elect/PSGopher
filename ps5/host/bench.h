/*
 * PSGopher - the benchmark mode's counters (bench.c).
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
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
/* parallel-rdp's shading: a dispatch with its specialised pipeline (0) or the general one while that
 * compiles (1); a compile asked for; one done and its time */
void psgopher_bench_shading_dispatch(int general);
void psgopher_bench_shader_compile_asked(void);
void psgopher_bench_shader_compiled(uint64_t tsc_ticks);
/* an RSP task on the emulation thread (the core, psgopher.rs) */
void psgopher_bench_rsp_begin(void);
void psgopher_bench_rsp_end(uint64_t cycles);
/* the benchmark window (benchmark mode): warmup_vis presents, then measure_vis measured (0: off);
 * the hooks parallel-rdp's interface and the core call; the summary after the game */
void psgopher_bench_window(int warmup_vis, int measure_vis);
int psgopher_bench_window_state(void);            /* 0 off, 1 warming up, 2 measuring, 3 done */
int psgopher_bench_window_auto(void);             /* the window is a slow moment's (normal play): it does not end the game */
int psgopher_bench_window_edge(void);             /* 1 began, 2 ended at the last present (read once) */
void psgopher_bench_window_present(uint64_t begin_tsc, uint64_t end_tsc);
void psgopher_bench_window_gpu(const char *line); /* the GPU stages over the window */
double psgopher_bench_window_report(const char *label); /* its speed in %, -1 not complete */
void psgopher_bench_fence(uint64_t tsc_ticks);    /* parallel-rdp waiting for a render pass's fence */
void psgopher_bench_submit(uint64_t tsc_ticks);   /* one Vulkan submit or next_frame_context, timed */
uint64_t psgopher_bench_cpu_now(void);            /* thread CPU ns for a parallel-rdp part; 0 outside a window */
void psgopher_bench_rdp_part_cpu(uint64_t begin_cpu_ns); /* closes it (commands, frame parts only) */
void psgopher_bench_limiter_begin(void);          /* Gopher64's speed limiter's sleep */
void psgopher_bench_limiter_end(void);

/* PSGopher: the quick menu's live figures, the last whole second's: frames shown a second, the
 * emulation thread's CPU time a frame (its waits are none), the present's wait a frame
 * (cpu_ms -1 when the thread clock is not there) */
struct psgopher_live {
   double frames_per_second, cpu_ms, present_ms;
   double rdram_ms, rdram_waits;  /* RDRAM syncs' wait a VI, and how many */
   double rdp_ms;                 /* of the CPU time, parallel-rdp's (commands, the frame's render); -1 unknown */
   double rdp_part_ms[3];         /* of that: drawing commands, SyncFull's submissions, the frame's render */
   double rsp_ms;                 /* of the CPU time, the RSP's (less the RDP work its tasks start); -1 unknown */
   double general_share;          /* shading dispatches with the general pipeline, 0..1 (-1 none) */
   unsigned long long compiles_done, compiles_waiting; /* specialised pipelines: compiled, still asked for */
   double compile_ms;             /* their average compile time */
   double speed;                  /* VIs a second against the console's 50 or 60, in % */
   unsigned game_fps, target_vis; /* pictures the game drew a second (Gopher64's count); 50 or 60 */
   unsigned long long audio_queue, audio_underruns;
};
void psgopher_live_read(struct psgopher_live *out);
/* the last frames' times (ms between VIs), oldest first, at most max; their number */
int psgopher_live_history(float *out, int max);

/* PSGopher: where the emulation thread is, for the freeze watchdog (bench.c). The emulation thread
 * marks the places it can block in; the watchdog thread reads the mark. One relaxed load and two
 * stores a region (and one TSC read); only the emulation thread calls these. Keep the numbers in
 * step with the copies in the core (vendor/gopher64: psgopher.rs, parallel-rdp). */
enum psgopher_place {
   PSGOPHER_WHERE_CPU,        /* the VR4300 and anything not marked (the default) */
   PSGOPHER_WHERE_RSP,
   PSGOPHER_WHERE_RDP_COMMANDS,
   PSGOPHER_WHERE_RDP_FRAME,
   PSGOPHER_WHERE_GPU_FENCE,  /* waiting for a fence */
   PSGOPHER_WHERE_VK_SUBMIT,  /* a Vulkan submit or next_frame_context */
   PSGOPHER_WHERE_RDRAM_SYNC, /* wait_for_timeline: the RDRAM sync, and the RDP's idle */
   PSGOPHER_WHERE_PRESENT,    /* end_frame + begin_frame: the FIFO/vsync wait, the acquire */
   PSGOPHER_WHERE_LIMITER,    /* the speed limiter's sleep */
   PSGOPHER_WHERE_AUDIO,      /* the audio queue: the stream's lock and data */
   PSGOPHER_WHERE_PI_DMA,     /* a cartridge DMA */
   PSGOPHER_WHERE_SAVE_DATA,  /* save files (EEPROM, SRAM, flash, mempak) */
   PSGOPHER_WHERE_SAVE_STATE, /* save states, load states, rewind */
   PSGOPHER_WHERE_PAUSED,     /* the game is held (pause, the quick menu): no stall */
   PSGOPHER_WHERE_MAIN_CALL,  /* (reserved: Gopher64 waits for no main-thread call) */
   PSGOPHER_WHERE_INPUT,      /* reading the controllers */
   PSGOPHER_WHERE_ACHIEVEMENTS,
   /* the steps of parallel-rdp's frame (3 is the rest of it), and the GPU's memory */
   PSGOPHER_WHERE_F_DRAIN,    /* 17: the command ring's drain */
   PSGOPHER_WHERE_F_LOCK,     /* 18: lock_command_processing (waiting for the RDP to be idle) */
   PSGOPHER_WHERE_F_FLUSH,    /* 19: flush_and_signal and the coherency resolve */
   PSGOPHER_WHERE_F_VI,       /* 20: the video interface's scanout */
   PSGOPHER_WHERE_F_PROGRAM,  /* 21: request_program */
   PSGOPHER_WHERE_F_CMD,      /* 22: request_command_buffer */
   PSGOPHER_WHERE_F_HDR,      /* 23: the HDR layers */
   PSGOPHER_WHERE_F_PASS,     /* 24: the swapchain render pass */
   PSGOPHER_WHERE_F_OSD,      /* 25: the quick menu and the stats overlay */
   PSGOPHER_WHERE_GPU_ALLOC,  /* 26: Vulkan memory, image and buffer creation (emulation thread only) */
   PSGOPHER_WHERE_COUNT
};
/* enters `place`; returns the place it was in, for psgopher_where_restore */
int psgopher_where(int place);
void psgopher_where_restore(int previous);
/* the name the log uses */
const char *psgopher_where_name(int place);
/* for code other threads also run (the allocator): marks only on the emulation thread; -1 elsewhere */
int psgopher_where_emu(int place);
void psgopher_where_emu_restore(int previous);

/* the freeze watchdog (a thread of its own, started with each game, stopped when it ends): logs a
 * stall when no VI has completed for 250 ms, and when VIs resume, where the time went. Logging
 * only, so on whatever Slow Moments says. It also counts hitches (VI intervals over 50 ms). The VI counter is bumped by psgopher_bench_present. */
void psgopher_stall_start(void);
void psgopher_stall_stop(void);
/* tuning for the host test: the threshold and the sample period, in ms */
void psgopher_stall_configure(int threshold_ms, int period_ms);
/* a VI completed on the emulation thread (psgopher_bench_present does it; the test calls it) */
void psgopher_stall_vi(void);
/* the hitches (VI intervals over 50 ms) since the last call, as the video line's tail */
void psgopher_hitch_summary(char *out, size_t size);

/* PSGopher: the video line's counts start again (before each game) */
void psgopher_video_reset(void);

#ifdef __cplusplus
}
#endif
