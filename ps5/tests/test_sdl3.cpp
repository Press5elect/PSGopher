/*
 * PSGopher - host tests of ps5/sdl3: the audio stream's accounting and resampling, the controller
 * and window answers Gopher64 relies on, and text rendering. Runs on the PC with the PS5 layer's
 * host build (no pads, no audio device) and a stand-in for RADV's entry point.
 *
 *   ps5/tests/run-host-tests.sh
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3_ttf/SDL_ttf.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "psgopher_sdl3.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance, const char *)
{
   return nullptr;
}

static int failures;
#define CHECK(cond, ...)                                                                                     \
   do {                                                                                                      \
      if (!(cond)) {                                                                                         \
         failures++;                                                                                         \
         fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);                                     \
         fprintf(stderr, __VA_ARGS__);                                                                       \
         fputc('\n', stderr);                                                                                \
      }                                                                                                      \
   } while (0)

static std::vector<int16_t> sine(int rate, double hz, double seconds, double amplitude)
{
   std::vector<int16_t> out((size_t)(rate * seconds) * 2);
   for (size_t i = 0; i < out.size() / 2; i++) {
      const int16_t v = (int16_t)std::lrint(amplitude * 32767 * std::sin(2 * M_PI * hz * i / rate));
      out[i * 2] = v;
      out[i * 2 + 1] = (int16_t)-v;
   }
   return out;
}

/* amplitude of `hz` in a 48 kHz channel, by correlation (Goertzel-like) */
static double tone(const std::vector<int16_t> &pcm, int channel, size_t from, size_t frames, double hz)
{
   double re = 0, im = 0;
   for (size_t i = 0; i < frames; i++) {
      const double v = pcm[(from + i) * 2 + channel] / 32767.0;
      re += v * std::cos(2 * M_PI * hz * i / 48000);
      im += v * std::sin(2 * M_PI * hz * i / 48000);
   }
   return 2 * std::sqrt(re * re + im * im) / frames;
}

static void test_audio()
{
   const SDL_AudioSpec spec = {SDL_AUDIO_S16LE, 2, 32000};
   SDL_AudioStream *s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
   CHECK(s, "open: %s", SDL_GetError());
   CHECK(!SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr),
         "a second stream is refused");
   CHECK(SDL_GetAudioStreamQueued(s) == 0 && SDL_GetAudioStreamAvailable(s) == 0, "empty at first");

   /* paused: rendering plays silence and consumes nothing */
   const auto pcm = sine(32000, 1000, 1.0, 0.5);
   CHECK(SDL_PutAudioStreamData(s, pcm.data(), (int)(pcm.size() * 2)), "put");
   CHECK(SDL_GetAudioStreamQueued(s) == (int)(pcm.size() * 2), "queued counts input bytes: %d",
         SDL_GetAudioStreamQueued(s));
   int16_t grain[256 * 2];
   psgopher_sdl3_audio_render(s, grain, 256);
   CHECK(SDL_GetAudioStreamQueued(s) == (int)(pcm.size() * 2), "paused: nothing consumed");

   /* one second at 32 kHz is about 48000 output frames */
   CHECK(SDL_ResumeAudioStreamDevice(s), "resume");
   const int available = SDL_GetAudioStreamAvailable(s) / 4;
   CHECK(std::abs(available - 48000) < 16, "available %d frames, expected about 48000", available);

   std::vector<int16_t> out((size_t)available * 2);
   for (int done = 0; done < available;) {
      const int n = std::min(256, available - done);
      psgopher_sdl3_audio_render(s, out.data() + done * 2, n);
      done += n;
   }
   CHECK(SDL_GetAudioStreamAvailable(s) == 0, "drained");
   CHECK(SDL_GetAudioStreamQueued(s) > 0 && SDL_GetAudioStreamQueued(s) <= 9 * 4,
         "the filter's look-ahead stays queued: %d bytes", SDL_GetAudioStreamQueued(s));

   /* the 1 kHz tone survives at its amplitude; nothing at 2 kHz, its image at 31 kHz is gone */
   const double a = tone(out, 0, 4800, 38400, 1000), r = tone(out, 1, 4800, 38400, 1000);
   CHECK(std::fabs(a - 0.5) < 0.01 && std::fabs(r - 0.5) < 0.01, "1 kHz amplitude %.4f / %.4f, expected 0.5", a,
         r);
   CHECK(tone(out, 0, 4800, 38400, 2000) < 0.002, "no 2 kHz: %.5f", tone(out, 0, 4800, 38400, 2000));
   CHECK(tone(out, 0, 4800, 38400, 48000 - 31000) < 0.002, "the 31 kHz image (folds to 17 kHz) is filtered: %.5f",
         tone(out, 0, 4800, 38400, 48000 - 31000));

   /* starved while playing: silence, counted */
   psgopher_sdl3_audio_stats stats;
   psgopher_sdl3_audio_stats_read(&stats);
   const uint64_t before = stats.underrun_grains;
   psgopher_sdl3_audio_render(s, grain, 256);
   psgopher_sdl3_audio_stats_read(&stats);
   CHECK(stats.underrun_grains == before + 1, "an underrun is counted");
   bool silent = true;
   for (int16_t v : grain)
      silent &= v == 0;
   CHECK(silent, "an underrun plays silence");

   /* the frequency ratio speeds consumption: 1.005 makes 0.5 % fewer frames */
   CHECK(SDL_ClearAudioStream(s), "clear");
   CHECK(SDL_GetAudioStreamQueued(s) == 0, "cleared");
   SDL_SetAudioStreamFrequencyRatio(s, 1.005f);
   SDL_PutAudioStreamData(s, pcm.data(), (int)(pcm.size() * 2));
   const int faster = SDL_GetAudioStreamAvailable(s) / 4;
   CHECK(std::abs(faster - (int)(48000 / 1.005)) < 16, "ratio 1.005: %d frames", faster);

   /* gain */
   SDL_SetAudioStreamGain(s, 0.5f);
   CHECK(std::fabs(SDL_GetAudioStreamGain(s) - 0.5f) < 1e-6, "gain read back");

   /* a new input rate (the AI's frequency changed) */
   SDL_ClearAudioStream(s);
   SDL_SetAudioStreamFrequencyRatio(s, 1.0f);
   const SDL_AudioSpec higher = {SDL_AUDIO_S16LE, 2, 44100};
   CHECK(SDL_SetAudioStreamFormat(s, &higher, nullptr), "format");
   const auto pcm44 = sine(44100, 440, 1.0, 0.25);
   SDL_PutAudioStreamData(s, pcm44.data(), (int)(pcm44.size() * 2));
   CHECK(std::abs(SDL_GetAudioStreamAvailable(s) / 4 - 48000) < 16, "44.1 kHz -> 48 kHz frames");
   SDL_DestroyAudioStream(s);
}

static void test_window_and_controllers()
{
   CHECK(SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "init");
   CHECK(SDL_WasInit(SDL_INIT_GAMEPAD) == SDL_INIT_GAMEPAD, "was init");
   SDL_Window *w = SDL_CreateWindow("gopher64", 640, 480, SDL_WINDOW_VULKAN);
   CHECK(w, "window");
   int width = 0, height = 0;
   SDL_GetWindowSizeInPixels(w, &width, &height);
   CHECK(width == 3840 && height == 2160, "the display's size before the surface: %dx%d", width, height);
   CHECK(std::fabs(SDL_GetWindowDisplayScale(w) - 2.0f) < 1e-6, "2160p is scale 2");
   CHECK(!(SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED), "never minimised (Gopher64 skips presents if so)");
   Uint32 count = 0;
   const char *const *ext = SDL_Vulkan_GetInstanceExtensions(&count);
   CHECK(count == 2 && strcmp(ext[1], "VK_KHR_display") == 0, "instance extensions");
   CHECK(SDL_Vulkan_GetVkGetInstanceProcAddr() == (SDL_FunctionPointer)vk_icdGetInstanceProcAddr, "RADV's entry");

   SDL_PumpEvents();
   int n = -1;
   SDL_JoystickID *ids = SDL_GetJoysticks(&n);
   CHECK(ids && n == 0, "no controller on the host: %d", n);
   SDL_free(ids);
   CHECK(!SDL_OpenGamepad(1), "no controller 1");
   CHECK(SDL_GetJoystickPathForID(1) == nullptr, "no path for a missing controller");
   const bool *keys = SDL_GetKeyboardState(nullptr);
   CHECK(keys && !keys[SDL_SCANCODE_LALT], "a keyboard state with nothing down");
   SDL_Window *aw;
   SDL_Renderer *ar;
   CHECK(!SDL_CreateWindowAndRenderer("x", 1, 1, 0, &aw, &ar) && ar == nullptr && SDL_GetError()[0],
         "no renderer, with an error");
   SDL_DestroyWindow(w);
}

static int watched;
static bool watch(void *, SDL_Event *event)
{
   if (event->type == SDL_EVENT_USER && event->user.code == 3)
      watched++;
   return false;
}
static int fired;
static Uint32 once(void *, SDL_TimerID, Uint32)
{
   fired++;
   return 0;
}
static void on_main(void *data) { *(int *)data += 1; }

static void test_events()
{
   SDL_AddEventWatch(watch, nullptr);
   SDL_Event e = {};
   e.user.type = SDL_EVENT_USER;
   e.user.code = 3;
   SDL_PushEvent(&e);
   CHECK(watched == 1, "a pushed event reaches the watch");
   SDL_RemoveEventWatch(watch, nullptr);
   SDL_PushEvent(&e);
   CHECK(watched == 1, "a removed watch is not called");

   SDL_AddTimer(1, once, nullptr);
   SDL_PumpEvents();
   std::this_thread::sleep_for(std::chrono::milliseconds(5));
   SDL_PumpEvents();
   CHECK(fired == 1, "a timer fires once from SDL_PumpEvents");
   SDL_PumpEvents();
   CHECK(fired == 1, "and not again");

   int calls = 0;
   SDL_RunOnMainThread(on_main, &calls, false);
   CHECK(calls == 1, "on the main thread it runs at once");
   std::thread other([&] { SDL_RunOnMainThread(on_main, &calls, false); });
   other.join();
   CHECK(calls == 1, "from another thread it waits for the next pump");
   SDL_PumpEvents();
   CHECK(calls == 2, "and runs there");
}

static void test_text(const char *font_path)
{
   FILE *f = fopen(font_path, "rb");
   CHECK(f, "font %s", font_path);
   if (!f)
      return;
   std::vector<unsigned char> data(1 << 20);
   data.resize(fread(data.data(), 1, data.size(), f));
   fclose(f);
   CHECK(TTF_Init(), "ttf init");
   TTF_Font *font = TTF_OpenFontIO(SDL_IOFromConstMem(data.data(), data.size()), true, 50.0f);
   CHECK(font, "font opens");
   const SDL_Color fg = {255, 255, 255, 255}, bg = {0, 0, 0, 0};
   SDL_Surface *one = TTF_RenderText_LCD_Wrapped(font, "Savestate slot 1", 0, fg, bg, 0);
   CHECK(one && one->w > 200 && one->h >= 50 && one->pitch == one->w * 4, "one line: %dx%d", one ? one->w : 0,
         one ? one->h : 0);
   unsigned lit = 0;
   for (int i = 0; one && i < one->w * one->h; i++)
      lit += (((uint32_t *)one->pixels)[i] & 0xff) > 128;
   CHECK(lit > 500, "glyphs drawn: %u bright pixels", lit);
   SDL_Surface *two = TTF_RenderText_LCD_Wrapped(font, "Savestate slot 1", 0, fg, bg, one ? one->w / 2 : 100);
   CHECK(two && one && two->h == 2 * one->h, "wrapped at the space into two lines");
   SDL_Surface *lines = TTF_RenderText_LCD_Wrapped(font, "a\nb\nc", 0, fg, bg, 0);
   CHECK(lines && one && lines->h == 3 * one->h, "newlines make lines");
   SDL_DestroySurface(one);
   SDL_DestroySurface(two);
   SDL_DestroySurface(lines);
   TTF_SetFontSize(font, 25.0f);
   CHECK(TTF_GetFontSize(font) == 25.0f && TTF_GetFontLineSkip(font) > 20, "resized");
   TTF_CloseFont(font);
}

int main(int argc, char **argv)
{
   test_window_and_controllers();
   test_events();
   test_audio();
   test_text(argc > 1 ? argv[1] : "vendor/gopher64/data/ui/RobotoMono-Regular.ttf");
   if (failures) {
      fprintf(stderr, "%d failures\n", failures);
      return 1;
   }
   puts("ps5/sdl3: all host tests passed");
   return 0;
}
