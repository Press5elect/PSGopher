/*
 * PSGopher - the title: the console's setup, the game list, Gopher64, the shell exit.
 *
 * First, while the process has one thread, out of the title's sandbox (sandbox.cpp, as OGBox and
 * Crux2: USB drives and the whole of /data are then seen), then the network shares (menu.h,
 * ps5vfs) and the game list (library.h, menu.h). A game chosen there runs in Gopher64 as on the
 * desktop with its ROM on the command line (psgopher_main, vendor/gopher64/src/psgopher.rs); when
 * it ends (OPTIONS with the touch pad, Gopher64's quit), the list comes back. /data/psgopher/
 * autostart.txt still boots its ROM first; benchmark.txt and probe.txt run without the list.
 * The title never calls exit(): main returns and the PS5 layer's catchReturnFromMain asks the shell
 * to close it (platform.c).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <time.h>
#include <x86intrin.h>

#include "library.h"
#include "menu.h"
#include "title_settings.h"

extern "C" {
#include "bench.h"
#include "sandbox.h"
#include "platform.h"
#include "psgopher_sdl3.h"
int psgopher_main(int argc, const char *const *argv);
void psgopher_probe(void);
size_t psgopher_last_error(char *buffer, size_t size);
}

static const char ROOT[] = "/data/psgopher";

/* Everything the title makes stays reachable over FTP: folders 0777, files 0666 (platform contract) */
static void folder(const std::string &path)
{
   if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST)
      say("folder %s: %s", path.c_str(), strerror(errno));
   chmod(path.c_str(), 0777);
}

static std::string first_line(const std::string &path)
{
   FILE *f = fopen(path.c_str(), "r");
   if (!f)
      return {};
   char line[1024] = {};
   if (!fgets(line, sizeof(line), f))
      line[0] = 0;
   fclose(f);
   std::string s = line;
   while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
      s.pop_back();
   return s;
}

static bool exists(const std::string &path)
{
   struct stat st;
   return stat(path.c_str(), &st) == 0;
}

/* ---------------------------------------------------------------- the probe (probe.txt) */

static uint64_t ns(clockid_t clock)
{
   struct timespec t;
   if (clock_gettime(clock, &t) != 0)
      return 0;
   return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static void probe()
{
   struct timespec res = {};
   clock_getres(CLOCK_MONOTONIC, &res);
   const int calls = 1000000;
   const uint64_t t0 = ns(CLOCK_MONOTONIC);
   uint64_t sink = 0;
   for (int i = 0; i < calls; i++)
      sink += ns(CLOCK_MONOTONIC);
   const uint64_t t1 = ns(CLOCK_MONOTONIC);
   say("probe: clock_gettime(CLOCK_MONOTONIC): %.1f ns a call, clock_getres %ld ns (%llu)",
       (double)(t1 - t0) / calls, res.tv_nsec, (unsigned long long)(sink & 1));
   say("probe: CLOCK_THREAD_CPUTIME_ID: %s", ns(CLOCK_THREAD_CPUTIME_ID) ? "available" : "NOT available");
   /* the TSC against the monotonic clock over 200 ms */
   const uint64_t c0 = ns(CLOCK_MONOTONIC), r0 = __rdtsc();
   while (ns(CLOCK_MONOTONIC) - c0 < 200000000u) {
   }
   const uint64_t c1 = ns(CLOCK_MONOTONIC), r1 = __rdtsc();
   say("probe: TSC %.2f MHz (rdtsc against CLOCK_MONOTONIC, 200 ms)", (double)(r1 - r0) * 1e3 / (double)(c1 - c0));
   psgopher_probe();
}

/* ---------------------------------------------------------------- the benchmark mode (benchmark.txt) */

struct Benchmark {
   bool on = false;
   std::string rom;
   int seconds = 0;
   bool unlimited = false;
   uint64_t watched_since = 0; /* when Gopher64's video was up */
};
static Benchmark bench;

static void read_benchmark(const std::string &path)
{
   FILE *f = fopen(path.c_str(), "r");
   if (!f)
      return;
   bench.on = true;
   char line[1024];
   while (fgets(line, sizeof(line), f)) {
      char word[32] = {}, value[992] = {};
      if (sscanf(line, "%31s %991[^\r\n]", word, value) < 1)
         continue;
      if (!strcmp(word, "rom"))
         bench.rom = value;
      else if (!strcmp(word, "seconds"))
         bench.seconds = atoi(value);
      else if (!strcmp(word, "unlimited"))
         bench.unlimited = true;
   }
   fclose(f);
   say("benchmark: rom '%s', %d s, speed limiter %s", bench.rom.c_str(), bench.seconds,
       bench.unlimited ? "off" : "on");
}

/* Gopher64's own events (parallel-rdp's interface): 4 toggles the speed limiter, 3 ends the game */
static void send_user_event(int code)
{
   SDL_Event event = {};
   event.user.type = SDL_EVENT_USER;
   event.user.code = code;
   SDL_PushEvent(&event);
}

static Uint32 benchmark_timer(void *, SDL_TimerID, Uint32)
{
   if (psgopher_sdl3_watch_count() == 0)
      return 100; /* the video is not up yet */
   if (!bench.watched_since) {
      bench.watched_since = ns(CLOCK_MONOTONIC);
      if (bench.unlimited)
         send_user_event(4);
   }
   if (bench.seconds > 0 && ns(CLOCK_MONOTONIC) - bench.watched_since >= (uint64_t)bench.seconds * 1000000000u) {
      say("benchmark: %d s are up", bench.seconds);
      send_user_event(3);
      return 0;
   }
   return 100;
}

/* autostart.txt's ROM: a path, or a file name in /data/psgopher/roms */
static std::string autostart_rom()
{
   std::string named = first_line(std::string(ROOT) + "/autostart.txt");
   if (!named.empty() && named[0] != '/')
      named = std::string(ROOT) + "/roms/" + named;
   if (!named.empty())
      say("autostart.txt names %s", named.c_str());
   return named;
}

static void log_line(const char *line) { say("%s", line); }

/* what Gopher64 said when a game did not start or ended in error (psgopher.rs keeps it) */
static std::string last_error()
{
   char text[512] = {};
   psgopher_last_error(text, sizeof(text));
   return std::string("The game ended with an error: ") + (text[0] ? text : "see the log");
}

/* Gopher64 with one ROM; its status */
static int play(const std::string &rom)
{
   const char *argv[] = {"psgopher", "--fullscreen", rom.c_str()};
   say("starting Gopher64 with %s", rom.c_str());
   const int status = psgopher_main(3, argv);
   say("Gopher64 returned %d", status);
   return status;
}

int main()
{
   /* out of the sandbox before anything starts a thread (platform_init's klog capture does) */
   static char title_dir[192] = "/app0";
   (void)psgopher_sandbox_leave(title_dir, sizeof(title_dir));
   /* the boot log, as OGBox's and Crux2's: in the title folder, the run before kept beside it; in
    * /data/psgopher when the title folder takes no file */
   mkdir(ROOT, 0777);
   static std::string log_path, log_note;
   for (const std::string &dir : {std::string(title_dir), std::string(ROOT)}) {
      const std::string path = dir + "/psgopher-boot.log", previous = dir + "/psgopher-boot.previous.log";
      const int opened = platform_log_open(path.c_str(), previous.c_str());
      if (opened >= 0) {
         log_path = path;
         log_note = platform_log_note();
         break;
      }
   }
   platform_init("PSGopher");
   say("log: %s (%s)", log_path.empty() ? "none could be written" : log_path.c_str(), log_note.c_str());
#ifdef PSGOPHER_BUILD
   say("build: %s", PSGOPHER_BUILD);
#endif
   psgopher_sandbox_log(log_line);
   say("title folder: %s", title_dir);
   /* Crux2: outside the sandbox the effective group can differ from the real one, and Mesa's
    * shader cache is off while they differ */
   if (getegid() != getgid()) {
      const unsigned was = (unsigned)getegid();
      const bool fixed = setegid(getgid()) == 0;
      say("ids: egid %u, gid %u: the effective group %s", was, (unsigned)getgid(),
          fixed ? "is set to the real one" : "could not be changed (the shader cache is off)");
   }
   /* (no umask: a title's kernel applies no file mode mask, PS5_PayloadSDK's ps5platform/libc.h) */
   for (const char *sub : {"", "/config", "/data", "/data/saves", "/data/states", "/cache", "/cache/radv", "/roms"})
      folder(std::string(ROOT) + sub);

   /* RADV's shader cache with the title's data, not in the read-mostly title folder; Tokio only
    * writes files here, so two workers rather than one per reported CPU (16, of which a title has
    * 13: PROBE.md) */
   setenv("MESA_SHADER_CACHE_DIR", "/data/psgopher/cache/radv", 1);
   setenv("TOKIO_WORKER_THREADS", "2", 0);
   psgopher::TitleSettings settings;
   settings.load();
   settings.applyDisplay(title_dir);

   if (exists(std::string(ROOT) + "/probe.txt")) {
      probe();
      say("probe: done (remove %s/probe.txt to play)", ROOT);
      return 0;
   }
   read_benchmark(std::string(ROOT) + "/benchmark.txt");
   if (bench.on) {
      if (bench.rom.empty()) {
         say("benchmark: benchmark.txt names no rom");
         return 2;
      }
      const std::string rom = bench.rom[0] == '/' ? bench.rom : std::string(ROOT) + "/roms/" + bench.rom;
      char path[128];
      snprintf(path, sizeof(path), "%s/bench-%lld.csv", ROOT, (long long)time(nullptr));
      settings.applyHdr();
      psgopher_bench_start(path);
      SDL_AddTimer(100, benchmark_timer, nullptr);
      const int status = play(rom);
      psgopher_bench_stop();
      return status;
   }

   psgopher::startShares(title_dir);
   psgopher::library().load();
   psgopher::library().rescan();

   std::string notice;
   const std::string first = autostart_rom();
   if (!first.empty()) {
      settings.applyHdr();
      if (play(first) != 0)
         notice = last_error();
   }
   for (;;) {
      if (!notice.empty() && !log_path.empty())
         notice += " (" + log_path + ")";
      const psgopher::MenuResult chosen = psgopher::runMenu(notice);
      if (!chosen.play)
         break;
      settings.load(); /* the list may have changed HDR10 */
      settings.applyHdr();
      notice = play(chosen.rom) == 0 ? std::string() : last_error();
   }
   say("closing");
   return 0;
}
