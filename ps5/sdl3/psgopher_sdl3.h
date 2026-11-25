/*
 * PSGopher - what ps5/sdl3 offers beyond SDL's own API: the audio counters the benchmark mode logs
 * (docs/dev/performance.md) and the render call the host tests drive without an audio device.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_AudioStream SDL_AudioStream;

typedef struct psgopher_sdl3_audio_stats {
   uint64_t grains;          /* 256-frame grains the output asked for */
   uint64_t underrun_grains; /* ... of which the stream could not fill completely while playing */
   uint64_t silent_frames;   /* frames of silence those underruns played */
   uint64_t queued_frames;   /* input frames waiting now (at the stream's rate) */
} psgopher_sdl3_audio_stats;

/* The event watches registered (parallel-rdp's interface adds one at rdp_init) */
int psgopher_sdl3_watch_count(void);
/* The quick menu: whether a touch pad tap was seen since the last call (and forgets it); player 1's
 * pad for the menu (the tap watched there too); the pads at rest for the game until all is let go. */
int psgopher_sdl3_take_tap(void);
struct pad;
void psgopher_sdl3_menu_pad(struct pad *pad);
void psgopher_sdl3_hold_until_released(void);

void psgopher_sdl3_audio_stats_read(psgopher_sdl3_audio_stats *stats);

/* Makes `frames` stereo frames at 48 kHz from the stream (silence where it has none): what the
 * PS5 layer's audio thread calls each grain. */
void psgopher_sdl3_audio_render(SDL_AudioStream *stream, int16_t *out, int frames);

#ifdef __cplusplus
}
#endif
