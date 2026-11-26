/*
 * PSGopher - host test of the freeze watchdog (ps5/host/bench.c): a fake emulation thread draws VIs,
 * then blocks 400 ms in a named place; the watchdog's two lines must name it. A held game (the place
 * PAUSED) logs nothing. Slow moments does not matter: the watchdog is logging only. Hitches (VI
 * intervals over 50 ms) are counted into the video line's tail (psgopher_hitch_summary).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bench.h"
#include "psgopher_sdl3.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static pthread_mutex_t say_lock = PTHREAD_MUTEX_INITIALIZER;
static char lines[8192];
void say(const char *format, ...)
{
   char one[1024];
   va_list ap;
   va_start(ap, format);
   vsnprintf(one, sizeof(one), format, ap);
   va_end(ap);
   if (strncmp(one, "stall", 5))
      return;
   pthread_mutex_lock(&say_lock);
   strncat(lines, one, sizeof(lines) - strlen(lines) - 2);
   strcat(lines, "\n");
   pthread_mutex_unlock(&say_lock);
   puts(one);
}
void psgopher_sdl3_audio_stats_read(psgopher_sdl3_audio_stats *stats) { memset(stats, 0, sizeof(*stats)); }

static void sleep_ms(int ms)
{
   struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
   nanosleep(&t, NULL);
}

/* VIs at 60 a second for `ms` */
static void vis(int ms)
{
   for (int i = 0; i < ms / 16; i++) {
      sleep_ms(16);
      psgopher_stall_vi();
   }
}

static struct { int place, block_ms; } job;
static void *emulation(void *unused)
{
   (void)unused;
   vis(300);
   const int before = psgopher_where(job.place);
   sleep_ms(job.block_ms);
   psgopher_where_restore(before);
   vis(300);
   return NULL;
}

/* one game: its stall lines */
static void game(int place, int block_ms)
{
   lines[0] = 0;
   job.place = place;
   job.block_ms = block_ms;
   psgopher_stall_start();
   pthread_t t;
   pthread_create(&t, NULL, emulation, NULL);
   pthread_join(t, NULL);
   sleep_ms(60);
   psgopher_stall_stop();
}

static void *hitching(void *unused)
{
   (void)unused;
   for (int i = 1; i <= 20; i++) {
      sleep_ms(16);
      if (i % 4 == 0) {
         const int before = psgopher_where(PSGOPHER_WHERE_AUDIO);
         sleep_ms(80);
         psgopher_where_restore(before);
      }
      psgopher_stall_vi();
   }
   return NULL;
}

static void hitch_run(char *tail, size_t size)
{
   psgopher_stall_start();
   pthread_t t;
   pthread_create(&t, NULL, hitching, NULL);
   pthread_join(t, NULL);
   sleep_ms(60);
   psgopher_hitch_summary(tail, size);
   psgopher_stall_stop();
}

int main(void)
{
   unsetenv("PSGOPHER_SLOW_CAPTURE");

   game(PSGOPHER_WHERE_SAVE_DATA, 400);
   const char *first = strstr(lines, "stall: no VI for 250 ms; the emulation thread in save data for ");
   CHECK(first, "the stall line does not name the place:\n%s", lines);
   const char *over = strstr(lines, "stall over: ");
   CHECK(over && first && over > first, "no 'stall over' line after it:\n%s", lines);
   if (first) {
      const double in_ms = atof(first + strlen("stall: no VI for 250 ms; the emulation thread in save data for "));
      CHECK(in_ms >= 200 && in_ms < 450, "place age %.0f ms", in_ms);
   }
   if (over) {
      const double total = atof(over + strlen("stall over: "));
      CHECK(total >= 380 && total < 600, "stall total %.0f ms, 400 expected", total);
      const char *where = strstr(over, "(20 ms samples): save data ");
      CHECK(where, "the samples do not lead with save data:\n%s", lines);
      if (where)
         CHECK(atoi(where + strlen("(20 ms samples): save data ")) >= 8, "too few samples in save data");
      CHECK(strstr(over, "of CPU") || strstr(over, "not readable"), "no CPU time said");
      const char *cpu = strstr(over, "used ");
      if (cpu)
         CHECK(atof(cpu + 5) < 100, "a sleeping thread used %.1f ms of CPU", atof(cpu + 5));
   }
   CHECK(strstr(lines, "stall: ") == strstr(lines, "stall: no VI") && strstr(strstr(lines, "stall over") + 1, "stall: no VI") == NULL,
         "more than one stall reported:\n%s", lines);

   /* another place names itself */
   game(PSGOPHER_WHERE_GPU_FENCE, 400);
   CHECK(strstr(lines, "in a GPU fence wait for "), "second place not named:\n%s", lines);

   /* a held (paused) game is no stall */
   game(PSGOPHER_WHERE_PAUSED, 400);
   CHECK(!lines[0], "a paused game logged:\n%s", lines);

   /* nor is a short block */
   game(PSGOPHER_WHERE_PRESENT, 150);
   CHECK(!lines[0], "a 150 ms block logged:\n%s", lines);

   /* Slow moments off: the watchdog still logs */
   setenv("PSGOPHER_SLOW_CAPTURE", "0", 1);
   game(PSGOPHER_WHERE_SAVE_DATA, 400);
   CHECK(strstr(lines, "stall: no VI for 250 ms; the emulation thread in save data"), "Slow moments off silenced the watchdog:\n%s", lines);
   unsetenv("PSGOPHER_SLOW_CAPTURE");

   /* hitches: 20 VIs, every 4th one after an 80 ms block in the audio queue */
   char tail[1024];
   hitch_run(tail, sizeof(tail));
   CHECK(strstr(tail, "; hitches: 5 over 50 ms, longest "), "5 hitches expected: %s", tail);
   const char *longest = strstr(tail, "longest ");
   CHECK(longest && atof(longest + 8) >= 80 && atof(longest + 8) < 140, "longest hitch off: %s", tail);
   CHECK(strstr(tail, "(in the audio queue)") && strstr(tail, "(20 ms samples): the audio queue "), "place not named: %s", tail);
   /* nothing since: none */
   psgopher_hitch_summary(tail, sizeof(tail));
   CHECK(!strcmp(tail, "; hitches: none"), "second read: %s", tail);

   /* the same with Slow moments off */
   setenv("PSGOPHER_SLOW_CAPTURE", "0", 1);
   hitch_run(tail, sizeof(tail));
   CHECK(strstr(tail, "; hitches: 5 over 50 ms"), "Slow moments off: hitches not counted: %s", tail);
   unsetenv("PSGOPHER_SLOW_CAPTURE");

   if (failures) {
      fprintf(stderr, "test_stall: %d failure(s)\n", failures);
      return 1;
   }
   puts("test_stall: ok");
   return 0;
}
