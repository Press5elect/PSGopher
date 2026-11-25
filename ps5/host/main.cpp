/*
 * PSGopher - the title: the console's setup, the ROM to boot, Gopher64, the shell exit.
 *
 * The ROM: the first line of /data/psgopher/autostart.txt (a path, or a file name in
 * /data/psgopher/roms), else the first ROM in /data/psgopher/roms by name. Gopher64 then runs as on
 * the desktop with that ROM on its command line (psgopher_main, vendor/gopher64/src/psgopher.rs).
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

#include <SDL3/SDL.h>
#include <time.h>
#include <x86intrin.h>

extern "C" {
#include "bench.h"
#include "platform.h"
#include "psgopher_sdl3.h"
int psgopher_main(int argc, const char *const *argv);
void psgopher_probe(void);
}

static const char ROOT[] = "/data/psgopher";

/* Everything the title makes stays reachable over FTP: folders 0777, files 0666 (platform contract) */
static void folder(const std::string &path)
{
   if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST)
      say("folder %s: %s", path.c_str(), strerror(errno));
   chmod(path.c_str(), 0777);
}

static bool is_rom(const std::string &name)
{
   static const char *const extensions[] = {".z64", ".n64", ".v64", ".zip", ".7z", ".bin"};
   std::string lower = name;
   std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
   for (const char *e : extensions) {
      const size_t n = strlen(e);
      if (lower.size() > n && lower.compare(lower.size() - n, n, e) == 0)
         return true;
   }
   return false;
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

/* ---------------------------------------------------------------- the title's own settings */

/* /data/psgopher/config/psgopher.ini (Gopher64's own are in config.json): the display's refresh and
 * HDR10, which must be known before Vulkan starts. Written with its defaults when missing, so it can be
 * edited over FTP. Variable refresh and HDR10 are off until a console run has shown them working
 * (default-off, the owner's rule for what no console has tested yet). */
static const char SETTINGS[] = "/data/psgopher/config/psgopher.ini";
static const char SETTINGS_DEFAULT[] =
   "# PSGopher's own settings (Gopher64's are in config.json). Read when the title starts.\n"
   "#\n"
   "# display: 60   59.94 Hz, the N64's own rate (PAL games' 50 Hz alternate one and two refreshes)\n"
   "#          120  119.88 Hz\n"
   "#          vrr  119.88 Hz with variable refresh: each frame shown when it is ready (NTSC and PAL\n"
   "#               alike), on a TV that has it; otherwise as 120\n"
   "display = 60\n"
   "\n"
   "# hdr: on sends HDR10 when the console's own output is HDR (its settings, and the TV); otherwise SDR\n"
   "hdr = off\n"
   "# hdr_white: the nits white is shown at (80 to 400; 203 is HDR's reference)\n"
   "hdr_white = 203\n"
   "# hdr_peak: the nits the brightest parts of the picture are raised to (0 off; up to 4000)\n"
   "hdr_peak = 1000\n";

static void apply_settings()
{
   if (FILE *f = fopen(SETTINGS, "r"))
      fclose(f);
   else if (FILE *w = fopen(SETTINGS, "w")) {
      fputs(SETTINGS_DEFAULT, w); /* (one fwrite of a small file: fine) */
      fclose(w);
      chmod(SETTINGS, 0666);
   }
   std::string display = "60", hdr = "off", white = "203", peak = "1000";
   if (FILE *f = fopen(SETTINGS, "r")) {
      char line[256];
      while (fgets(line, sizeof(line), f)) {
         char key[64] = {}, value[128] = {};
         if (line[0] == '#' || sscanf(line, " %63[a-z_] = %127s", key, value) != 2)
            continue;
         if (!strcmp(key, "display"))
            display = value;
         else if (!strcmp(key, "hdr"))
            hdr = value;
         else if (!strcmp(key, "hdr_white"))
            white = value;
         else if (!strcmp(key, "hdr_peak"))
            peak = value;
      }
      fclose(f);
   }
   /* the driver's switches (Crux2's RADV, patches 0015/0016): param.json declares 119.88 Hz; 59.94
    * unless asked otherwise, variable refresh only on request */
   if (display == "vrr") {
      setenv("PS5_VIDEOOUT_VRR", "1", 1);
      say("display: 119.88 Hz with variable refresh asked for (%s)", SETTINGS);
   } else if (display == "120") {
      say("display: 119.88 Hz asked for (%s)", SETTINGS);
   } else {
      setenv("PS5_VIDEOOUT_59HZ", "1", 1);
      say("display: 59.94 Hz (%s: display = %s)", SETTINGS, display.c_str());
   }
   /* parallel-rdp's interface reads these (vendor/gopher64/parallel-rdp/interface.cpp, PSGOPHER) */
   if (hdr == "on") {
      setenv("PSGOPHER_HDR", "1", 1);
      setenv("PSGOPHER_HDR_WHITE", white.c_str(), 1);
      setenv("PSGOPHER_HDR_PEAK", peak.c_str(), 1);
      say("display: HDR10 asked for: white %s nits, highlights %s nits", white.c_str(), peak.c_str());
   }
}

static std::string choose_rom()
{
   const std::string roms = std::string(ROOT) + "/roms";
   std::string named = first_line(std::string(ROOT) + "/autostart.txt");
   if (!named.empty()) {
      if (named[0] != '/')
         named = roms + "/" + named;
      say("autostart.txt names %s", named.c_str());
      return named;
   }
   std::vector<std::string> found;
   if (DIR *dir = opendir(roms.c_str())) {
      while (dirent *entry = readdir(dir))
         if (entry->d_name[0] != '.' && is_rom(entry->d_name))
            found.push_back(entry->d_name);
      closedir(dir);
   }
   std::sort(found.begin(), found.end());
   say("%zu ROM(s) in %s", found.size(), roms.c_str());
   return found.empty() ? std::string() : roms + "/" + found.front();
}

int main()
{
   platform_init("PSGopher");
#ifdef PSGOPHER_BUILD
   say("build: %s", PSGOPHER_BUILD);
#endif
   /* (no umask: a title's kernel applies no file mode mask, PS5_PayloadSDK's ps5platform/libc.h) */
   for (const char *sub : {"", "/config", "/data", "/data/saves", "/data/states", "/cache", "/cache/radv", "/roms"})
      folder(std::string(ROOT) + sub);

   /* RADV's shader cache with the title's data, not in the read-mostly title folder; Tokio only
    * writes files here, so two workers rather than one per reported CPU (16, of which a title has
    * 13: PROBE.md) */
   setenv("MESA_SHADER_CACHE_DIR", "/data/psgopher/cache/radv", 1);
   setenv("TOKIO_WORKER_THREADS", "2", 0);
   apply_settings();

   if (exists(std::string(ROOT) + "/probe.txt")) {
      probe();
      say("probe: done (remove %s/probe.txt to play)", ROOT);
      return 0;
   }
   read_benchmark(std::string(ROOT) + "/benchmark.txt");

   std::string rom = choose_rom();
   if (!bench.rom.empty())
      rom = bench.rom[0] == '/' ? bench.rom : std::string(ROOT) + "/roms/" + bench.rom;
   if (rom.empty()) {
      say("no ROM: put one in %s/roms or name it in %s/autostart.txt", ROOT, ROOT);
      return 2;
   }
   const char *argv[] = {"psgopher", "--fullscreen", rom.c_str()};
   if (bench.on) {
      char path[128];
      snprintf(path, sizeof(path), "%s/bench-%lld.csv", ROOT, (long long)time(nullptr));
      psgopher_bench_start(path);
      SDL_AddTimer(100, benchmark_timer, nullptr);
   }
   say("starting Gopher64 with %s", rom.c_str());
   const int status = psgopher_main(3, argv);
   psgopher_bench_stop();
   say("Gopher64 returned %d", status);
   return status;
}
