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
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
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

#include "game_settings.h"
#include "library.h"
#include "menu.h"
#include "title_settings.h"
#include "update_net.h"
#include "write_broker.h"
#include "web.h"
#include "ps5_vfs.h"

extern "C" {
#include "bench.h"
#include "sandbox.h"
#include "platform.h"
#include "psgopher_sdl3.h"
int psgopher_main(int argc, const char *const *argv);
void psgopher_probe(void);
size_t psgopher_last_error(char *buffer, size_t size);
bool psgopher_setting_set(const char *name, int64_t value);
void psgopher_crash_install(void);
int64_t psgopher_setting_get(const char *name);
/* the core's profiling build (PSGOPHER_PGO=generate): LLVM's profile runtime, linked only then */
int __llvm_profile_write_file(void) __attribute__((weak));
void __llvm_profile_set_filename(const char *name) __attribute__((weak));
void __llvm_profile_reset_counters(void) __attribute__((weak));
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

/* a folder's contents taken away (and the folder itself when remove_self): the shader cache of a
 * cold-cache run */
static void remove_tree(const std::string &path, bool remove_self)
{
   if (DIR *dir = opendir(path.c_str())) {
      while (dirent *e = readdir(dir)) {
         if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
         const std::string child = path + "/" + e->d_name;
         struct stat st;
         if (lstat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            remove_tree(child, true);
         else
            unlink(child.c_str());
      }
      closedir(dir);
   }
   if (remove_self)
      rmdir(path.c_str());
}

struct Benchmark {
   bool on = false;
   std::string rom;
   int seconds = 0;
   bool unlimited = false;
   int state = -1;              /* a probe (save state slot) every run starts from: the same scene */
   std::vector<int> upscales;   /* one run for each factor, in order (none: one run as set) */
   int warmup_vis = 300, measure_vis = 0; /* the window, in VIs after the game's first present (0: off) */
   int repeat = 1;              /* the whole sweep this many times, interleaved (1x 2x 4x 8x, 1x 2x ...) */
   bool cold = false;           /* the shader cache emptied before each run (cache cold), else kept (warm) */
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
      else if (!strcmp(word, "state"))
         bench.state = atoi(value);
      else if (!strcmp(word, "warmup_vis"))
         bench.warmup_vis = atoi(value);
      else if (!strcmp(word, "measure_vis"))
         bench.measure_vis = atoi(value);
      else if (!strcmp(word, "repeat"))
         bench.repeat = std::max(1, atoi(value));
      else if (!strcmp(word, "cache"))
         bench.cold = !strcmp(value, "cold");
      else if (!strcmp(word, "upscale")) {
         char *at = value;
         while (*at) {
            char *end = nullptr;
            const long u = strtol(at, &end, 10);
            if (end == at)
               break;
            if (u == 1 || u == 2 || u == 4 || u == 8)
               bench.upscales.push_back((int)u);
            at = end;
         }
      }
   }
   fclose(f);
   std::string factors;
   for (int u : bench.upscales)
      factors += std::to_string(u) + "x ";
   say("benchmark: rom '%s', from %s, upscale %s, %d time(s); a run: %d VIs of warm-up then %d measured%s; "
       "speed limiter %s; shader cache %s",
       bench.rom.c_str(), bench.state >= 0 ? ("probe " + std::to_string(bench.state)).c_str() : "the start",
       factors.empty() ? "as set" : factors.c_str(), bench.repeat, bench.warmup_vis, bench.measure_vis,
       bench.measure_vis ? "" : " (no window: seconds only)", bench.unlimited ? "off" : "on", bench.cold ? "cold" : "warm");
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
   if (!bench.watched_since)
      bench.watched_since = ns(CLOCK_MONOTONIC); /* (the limiter: PSGOPHER_BENCH_UNLIMITED, set before the run) */
   if (bench.seconds > 0 && ns(CLOCK_MONOTONIC) - bench.watched_since >= (uint64_t)bench.seconds * 1000000000u) {
      say("benchmark: %d s are up", bench.seconds);
      send_user_event(3);
      return 0;
   }
   return 100;
}

static int play(const std::string &rom, int load_state);

/* The benchmark's sweep (benchmark.txt, or a probe's Benchmark on the board): one run a factor, each
 * from the same probe, the whole sweep `repeat` times interleaved; Gopher64's upscale is put back
 * after. *results gets each factor's average speed ("1x 98% · 2x 85% ..."). */
static int run_sweep(psgopher::TitleSettings &settings, const std::string &rom, std::string *results)
{
   const int64_t upscale_before = psgopher_setting_get("video.upscale");
   std::vector<int> runs = bench.upscales;
   if (runs.empty())
      runs.push_back(0); /* (as set) */
   std::vector<double> speed_sum(runs.size(), 0.0);
   std::vector<int> speed_n(runs.size(), 0);
   int status = 0;
   if (bench.unlimited)
      setenv("PSGOPHER_BENCH_UNLIMITED", "1", 1);
   const size_t total = runs.size() * (size_t)bench.repeat;
   for (size_t k = 0; k < total; k++) {
      const size_t r = k % runs.size();
      if (runs[r])
         psgopher_setting_set("video.upscale", runs[r]);
      const long long factor = (long long)psgopher_setting_get("video.upscale");
      char label[64];
      snprintf(label, sizeof(label), "%lldx #%zu", factor, k / runs.size() + 1);
      say("benchmark: run %zu of %zu: upscale %s", k + 1, total, label);
      if (bench.cold)
         remove_tree(std::string(ROOT) + "/cache/radv", false); /* the driver's shader cache, emptied */
      char path[160];
      snprintf(path, sizeof(path), "%s/bench-%lld-%lldx-%zu.csv", ROOT, (long long)time(nullptr), factor, k / runs.size() + 1);
      settings.applyHdr();
      bench.watched_since = 0;
      psgopher_bench_window(bench.warmup_vis, bench.measure_vis);
      psgopher_bench_start(path);
      /* (a window ends the game itself; the wall-clock timer only for a run in seconds, and it stops
       * itself when it has ended its game) */
      if (bench.seconds > 0)
         SDL_AddTimer(100, benchmark_timer, nullptr);
      status = play(rom, bench.state);
      psgopher_bench_stop();
      if (bench.measure_vis > 0) {
         const double speed = psgopher_bench_window_report(label); /* (after the game: off the emulation thread) */
         if (speed >= 0)
            speed_sum[r] += speed, speed_n[r]++;
      }
      if (status != 0)
         break; /* (a game that did not start or ended in error: no more runs) */
   }
   unsetenv("PSGOPHER_BENCH_UNLIMITED");
   psgopher_bench_window(0, 0);
   if (upscale_before > 0)
      psgopher_setting_set("video.upscale", upscale_before);
   if (results) {
      results->clear();
      for (size_t r = 0; r < runs.size(); r++) {
         char part[48];
         if (speed_n[r])
            snprintf(part, sizeof(part), "%s%dx %.0f%%", results->empty() ? "" : " \xC2\xB7 ", runs[r] ? runs[r] : (int)upscale_before,
                     speed_sum[r] / speed_n[r]);
         else
            snprintf(part, sizeof(part), "%s%dx not measured", results->empty() ? "" : " \xC2\xB7 ", runs[r] ? runs[r] : (int)upscale_before);
         *results += part;
      }
   }
   return status;
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

/* why a romhack patch could not be made into a ROM (play() sets it; empty when it did) */
static std::string play_error;

/* what Gopher64 said when a game did not start or ended in error (psgopher.rs keeps it) */
static std::string last_error()
{
   if (!play_error.empty())
      return play_error;
   char text[512] = {};
   psgopher_last_error(text, sizeof(text));
   return std::string("The game ended with an error: ") + (text[0] ? text : "see the log");
}

/* Gopher64 with one ROM (and a probe's slot to start from, or -1); its status */
static int play(const std::string &chosen_rom, int load_state = -1)
{
   /* a romhack patch is applied to its base ROM first: Gopher64 is given the patched ROM (a plain
    * ROM's path is left as it is) */
   play_error.clear();
   std::string patch_error;
   const std::string rom = psgopher::playablePath(chosen_rom, &patch_error);
   if (rom.empty()) {
      say("romhack: %s", patch_error.c_str());
      play_error = patch_error;
      return 1;
   }
   const std::string slot = std::to_string(load_state);
   /* a 64DD disk is given with --disk: Gopher64 boots it with the IPL it finds in data/64dd */
   const bool disk = psgopher::isDiskName(rom);
   std::vector<const char *> args = {"psgopher", "--fullscreen"};
   if (load_state >= 0)
      args.insert(args.end(), {"--load-state", slot.c_str()});
   if (disk)
      args.push_back("--disk");
   args.push_back(rom.c_str());
   const char *const *argv = args.data();
   const int argc = (int)args.size();
   say("starting Gopher64 with %s%s%s", disk ? "the 64DD disk " : "", rom.c_str(), load_state >= 0 ? (" from probe " + slot).c_str() : "");
   /* what costs speed, beside the video line (Gopher64's config.json) */
   say("settings: upscale %lldx%s, overclock %s, rewind %s, expansion pak %s",
       (long long)psgopher_setting_get("video.upscale"), psgopher_setting_get("video.ssaa") == 1 ? " supersampled" : "",
       psgopher_setting_get("emulation.overclock") == 1 ? "on" : "off", psgopher_setting_get("emulation.rewind") == 1 ? "on" : "off",
       psgopher_setting_get("emulation.disable_expansion_pak") == 1 ? "off" : "on");
   psgopher_video_reset();
   psgopher_stall_start(); /* the freeze watchdog: logging only, with Slow moments */
   const int status = psgopher_main(argc, argv);
   psgopher_stall_stop();
   /* a profiling build: this game's counts to /data/psgopher/pgo (the title never returns from main,
    * so the runtime's own write at exit never happens; Crux2's way), then counted afresh */
   if (__llvm_profile_write_file && __llvm_profile_set_filename) {
      mkdir("/data/psgopher/pgo", 0777);
      static int written;
      char file[160];
      snprintf(file, sizeof(file), "/data/psgopher/pgo/core-%lld-%d.profraw", (long long)time(nullptr), ++written);
      __llvm_profile_set_filename(file);
      const int result = __llvm_profile_write_file();
      if (__llvm_profile_reset_counters)
         __llvm_profile_reset_counters();
      say("pgo: %s %s", file, result == 0 ? "written" : "could not be written");
   }
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
   psgopher_crash_install();
   psgopher_sandbox_log(log_line);
   say("title folder: %s", title_dir);
   psgopher::helperSetTitleFolder(title_dir);
   /* an update an interrupted install left half done is undone before anything reads the title's
    * files; then the updater is set up (it asks nothing until the front end does) */
   psgopher::update::RecoverTitle(title_dir);
   psgopher::update::StartTitle(title_dir);
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
   psgopher::recoverGameSettings(); /* (a game that did not end left its own settings in Gopher64's config) */
   if (!settings.expansionPakRead) {
      /* from before the Expansion Pak row was Auto, On or Off: Gopher64's "Disable Expansion Pak" it held, once */
      if (psgopher_setting_get("emulation.disable_expansion_pak") == 1) {
         settings.expansionPak = 2;
         psgopher_setting_set("emulation.disable_expansion_pak", 0);
         say("settings: Disable Expansion Pak was on: the Expansion Pak row is Off");
      }
      settings.save();
   }

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
      std::string results;
      return run_sweep(settings, rom, &results);
   }

   psgopher::startShares(title_dir);
   /* the covers and descriptions the front end shows, fetched as it shows them */
   psgopher::webStart(std::string(ROOT) + "/cache/covers");
   psgopher::library().load();
   {
      /* each share woken (Crux2's): read a little of a cartridge the last list had on it, so a
       * NAS's disks are turning before the search lists it (on the console a listing timed out
       * after the USB search, and the list came back empty) */
      std::vector<std::string> preferred;
      for (const psgopher::Game &g : psgopher::library().games())
         if (g.tab == psgopher::TabNetwork && g.size >= (8u << 20))
            preferred.push_back(g.path);
      ps5vfs_wake_mounts(std::move(preferred));
   }
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
      if (chosen.benchmark) {
         settings.applyHdr(); /* (a benchmark runs as set: a game's own settings are not applied) */
         /* a probe's Benchmark on the board: 1x 2x 4x 8x twice, interleaved, from the probe; a short
          * window (600 VIs, 10 s of the game, after 300 of warm-up), the speed limiter as set */
         bench = Benchmark{};
         bench.on = true, bench.state = chosen.load_state, bench.upscales = { 1, 2, 4, 8 };
         bench.warmup_vis = 300, bench.measure_vis = 600, bench.repeat = 2;
         say("benchmark: %s from probe %d: 1x 2x 4x 8x, twice; 300 VIs of warm-up, 600 measured", chosen.rom.c_str(),
             chosen.load_state);
         std::string results;
         const int status = run_sweep(settings, chosen.rom, &results);
         bench = Benchmark{};
         notice = status == 0 ? "Benchmark: " + results + " (each part in the boot log)" : last_error();
         continue;
      }
      const double started = now_seconds();
      {
         /* the game's own settings over the global ones for its length (game_settings.h) */
         psgopher::GameScope scope(chosen.key, chosen.keyFromHeader, settings, chosen.crc);
         scope.settings().applyHdr();
         notice = play(chosen.rom, chosen.load_state) == 0 ? std::string() : last_error();
      }
      if (!chosen.name.empty() && now_seconds() - started > 5) /* (one that did not start is no play) */
         psgopher::addPlayed(chosen.name, now_seconds() - started);
   }
   say("closing");
   return 0;
}
