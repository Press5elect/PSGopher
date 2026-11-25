/*
 * PSGopher - host test of the in-game quick menu (ps5/host/quickmenu.cpp). The interface's side
 * (psgopher_overlay_*) is stood in for by a small software rasterizer, so the triangles the menu
 * hands over become pictures (PPM, over a stand-in game frame), and a scripted pad goes through
 * the menu: each run's answer is checked (resume, save and load a probe, reset, quit).
 *
 *   test_quickmenu <work folder> <pictures folder>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <utime.h>

extern "C" {
#include "bench.h"
#include "platform.h"
#include "psgopher_sdl3.h"
}

static int failures = 0;
#define CHECK(x)                                                                \
   do {                                                                         \
      if (!(x)) {                                                               \
         std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x);    \
         failures++;                                                            \
      }                                                                         \
   } while (0)

struct psgopher_overlay_vertex {
   float x, y, u, v;
   uint32_t rgba;
};
struct psgopher_overlay_command {
   float clip[4];
   uint32_t texture, first_index, index_count;
   int32_t vertex_offset;
};
struct psgopher_quickmenu_answer {
   int action, slot;
};
extern "C" int psgopher_quickmenu_wanted(void);
extern "C" psgopher_quickmenu_answer psgopher_quickmenu_run(void);

/* ---------------------------------------------------------------- the platform, stood in for */

extern "C" void say(const char *format, ...)
{
   va_list args;
   va_start(args, format);
   std::vfprintf(stdout, format, args);
   va_end(args);
   std::fputc('\n', stdout);
}
extern "C" double now_seconds(void)
{
   return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
extern "C" void psgopher_live_read(struct psgopher_live *out)
{
   *out = {};
   out->frames_per_second = 59.94, out->cpu_ms = 10.4, out->present_ms = 3.4, out->speed = 100, out->target_vis = 60;
}
extern "C" int psgopher_live_history(float *out, int max)
{
   for (int i = 0; i < max; i++)
      out[i] = 16.7f + (i % 7 == 0 ? 9.0f : 0.0f);
   return max;
}
extern "C" void psgopher_overlay_set(const struct psgopher_overlay_vertex *, uint32_t, const uint32_t *, uint32_t,
                                     const struct psgopher_overlay_command *, uint32_t)
{
}
extern "C" void psgopher_sdl3_audio_stats_read(psgopher_sdl3_audio_stats *stats)
{
   *stats = {};
   stats->queued_frames = 1600;
}
static std::string g_states;
extern "C" size_t psgopher_game_info(char *name, size_t name_size, char *states, size_t states_size)
{
   snprintf(name, name_size, "%s", "COMET KART");
   snprintf(states, states_size, "%s", g_states.c_str());
   return g_states.size();
}

static std::string g_shot; /* the next present's picture, when set */
static size_t g_shot_frame = (size_t)-1;
static std::string g_shot_name;

/* the pad: a script of buttons, a step a frame */
static std::vector<uint32_t> g_script;
static size_t g_at = 0;
static uint32_t g_held = 0;
static bool g_tap = false;
extern "C" int psgopher_sdl3_take_tap(void)
{
   const bool t = g_tap;
   g_tap = false;
   return t;
}
extern "C" void psgopher_sdl3_menu_pad(struct pad *p)
{
   if (g_at == g_shot_frame)
      g_shot = g_shot_name;
   const uint32_t held = g_at < g_script.size() ? g_script[g_at++] : 0;
   static int idle = 0;
   idle = g_at < g_script.size() ? 0 : idle + 1;
   if (idle > 200) {
      std::fprintf(stderr, "FAILED: the script ended and the menu stayed\n");
      std::exit(1);
   }
   /* as the PS5 layer's pad_poll: "pressed" is against the held buttons the caller's struct had */
   const uint32_t before = p->held;
   *p = {};
   p->held = held;
   p->pressed = held & ~before;
   g_held = held;
}
static int g_holds = 0;
extern "C" void psgopher_sdl3_hold_until_released(void)
{
   g_holds++;
}

/* ---------------------------------------------------------------- the interface, stood in for */

static const int W = 1920, H = 1080;
struct Texture {
   int w, h;
   std::vector<uint8_t> rgba;
};
static std::map<uint32_t, Texture> g_textures;
static std::vector<float> g_frame; /* RGB, 0..1 */
static int g_presents = 0;

extern "C" int psgopher_overlay_texture(uint32_t id, int width, int height, const void *rgba)
{
   Texture &t = g_textures[id];
   t.w = width;
   t.h = height;
   t.rgba.assign((const uint8_t *)rgba, (const uint8_t *)rgba + (size_t)width * height * 4);
   return 0;
}
extern "C" void psgopher_overlay_texture_free(uint32_t id)
{
   g_textures.erase(id);
}
extern "C" void psgopher_overlay_size(int *width, int *height)
{
   *width = W;
   *height = H;
}

/* a stand-in game frame: a dusky checkerboard horizon */
static void gameFrame()
{
   g_frame.assign((size_t)W * H * 3, 0.0f);
   for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
         float *px = &g_frame[((size_t)y * W + x) * 3];
         if (y < H / 2) {
            const float t = (float)y / (H / 2);
            px[0] = 0.05f + 0.1f * t, px[1] = 0.15f + 0.35f * t, px[2] = 0.2f + 0.3f * t;
         } else {
            const float d = (float)(y - H / 2 + 8);
            const int u = (int)std::floor((x - W / 2) / d * 6), v = (int)std::floor(400.0f / d);
            const bool dark = ((u + v) & 1) != 0;
            px[0] = dark ? 0.03f : 0.05f, px[1] = dark ? 0.12f : 0.45f, px[2] = dark ? 0.14f : 0.47f;
         }
      }
}

static void raster(const psgopher_overlay_vertex &a, const psgopher_overlay_vertex &b, const psgopher_overlay_vertex &c,
                   const Texture &tex, const float clip[4])
{
   const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
   if (std::fabs(area) < 1e-6f)
      return;
   const int x0 = std::max((int)std::floor(std::min({ a.x, b.x, c.x })), std::max(0, (int)clip[0]));
   const int x1 = std::min((int)std::ceil(std::max({ a.x, b.x, c.x })), std::min(W, (int)clip[2]));
   const int y0 = std::max((int)std::floor(std::min({ a.y, b.y, c.y })), std::max(0, (int)clip[1]));
   const int y1 = std::min((int)std::ceil(std::max({ a.y, b.y, c.y })), std::min(H, (int)clip[3]));
   auto col = [](uint32_t c, int k) { return ((c >> (8 * k)) & 0xff) / 255.0f; };
   for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++) {
         const float px = x + 0.5f, py = y + 0.5f;
         const float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
         const float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
         const float w2 = 1.0f - w0 - w1;
         if (w0 < 0 || w1 < 0 || w2 < 0)
            continue;
         const float u = a.u * w0 + b.u * w1 + c.u * w2, v = a.v * w0 + b.v * w1 + c.v * w2;
         const int tx = std::clamp((int)(u * tex.w), 0, tex.w - 1), ty = std::clamp((int)(v * tex.h), 0, tex.h - 1);
         const uint8_t *t = &tex.rgba[((size_t)ty * tex.w + tx) * 4];
         float rgba[4];
         for (int k = 0; k < 4; k++)
            rgba[k] = (col(a.rgba, k) * w0 + col(b.rgba, k) * w1 + col(c.rgba, k) * w2) * (t[k] / 255.0f);
         float *out = &g_frame[((size_t)y * W + x) * 3];
         for (int k = 0; k < 3; k++)
            out[k] = rgba[k] * rgba[3] + out[k] * (1.0f - rgba[3]);
      }
}

extern "C" void psgopher_overlay_present(const psgopher_overlay_vertex *vertices, uint32_t vertex_count,
                                         const uint32_t *indices, uint32_t index_count,
                                         const psgopher_overlay_command *commands, uint32_t command_count)
{
   g_presents++;
   for (uint32_t i = 0; i < index_count; i++)
      CHECK(indices[i] < 65536);
   if (g_shot.empty())
      return;
   gameFrame();
   for (uint32_t c = 0; c < command_count; c++) {
      const auto &cmd = commands[c];
      const auto t = g_textures.find(cmd.texture);
      CHECK(t != g_textures.end());
      if (t == g_textures.end())
         continue;
      CHECK(cmd.first_index + cmd.index_count <= index_count);
      for (uint32_t i = 0; i + 2 < cmd.index_count; i += 3) {
         const uint32_t *ix = &indices[cmd.first_index + i];
         const uint32_t va = ix[0] + cmd.vertex_offset, vb = ix[1] + cmd.vertex_offset, vc = ix[2] + cmd.vertex_offset;
         CHECK(va < vertex_count && vb < vertex_count && vc < vertex_count);
         if (va < vertex_count && vb < vertex_count && vc < vertex_count)
            raster(vertices[va], vertices[vb], vertices[vc], t->second, cmd.clip);
      }
   }
   if (FILE *f = std::fopen(g_shot.c_str(), "wb")) {
      std::fprintf(f, "P6\n%d %d\n255\n", W, H);
      std::vector<uint8_t> rgb(g_frame.size());
      for (size_t i = 0; i < rgb.size(); i++)
         rgb[i] = (uint8_t)std::clamp((int)(g_frame[i] * 255.0f + 0.5f), 0, 255);
      std::fwrite(rgb.data(), 1, rgb.size(), f);
      std::fclose(f);
   }
   g_shot.clear();
}

/* ---------------------------------------------------------------- the runs */

static std::string g_pictures;


/* a run: each button pressed for two frames and let go for two; a picture after button
 * `shot_after` (-1: as the menu opens) */
static psgopher_quickmenu_answer run(std::vector<uint32_t> buttons, int shot_after = -2, const char *shot = nullptr)
{
   std::vector<uint32_t> frames = { 0, 0 };
   for (uint32_t b : buttons) {
      frames.push_back(b);
      frames.push_back(b);
      frames.push_back(0);
      frames.push_back(0);
   }
   g_script = frames;
   g_at = 0;
   g_held = 0;
   g_tap = true;
   CHECK(psgopher_quickmenu_wanted() == 1);
   CHECK(psgopher_quickmenu_wanted() == 0);
   g_tap = false;
   g_shot_frame = shot ? (shot_after < 0 ? 1 : 2 + (size_t)shot_after * 4 + 3) : (size_t)-1;
   g_shot_name = shot ? g_pictures + "/" + shot + ".ppm" : "";
   return psgopher_quickmenu_run();
}

int main(int argc, char **argv)
{
   const std::string work = argc > 1 ? argv[1] : "/tmp/psgopher-quickmenu";
   g_pictures = argc > 2 ? argv[2] : work;
   if (std::system(("rm -rf '" + work + "' && mkdir -p '" + work + "/states' '" + g_pictures + "'").c_str()) != 0)
      return 2;
   g_states = work + "/states/COMET KART-ABCD.state";
   /* probes 1 and 3 kept, 3 the newer */
   for (int slot : { 1, 3 }) {
      const std::string path = g_states + std::to_string(slot);
      FILE *f = std::fopen(path.c_str(), "wb");
      std::fputs("state", f);
      std::fclose(f);
      struct utimbuf t = { time(nullptr) - (slot == 1 ? 86400 * 3 : 3600), time(nullptr) - (slot == 1 ? 86400 * 3 : 3600) };
      utime(path.c_str(), &t);
   }

   /* the main page as it opens */
   psgopher_quickmenu_answer a = run({ PAD_CIRCLE }, -1, "1-quickmenu");
   CHECK(a.action == 0);
   /* take a probe: the second slot (a picture of the page with it chosen) */
   a = run({ PAD_DOWN, PAD_CROSS, PAD_DOWN, PAD_CROSS }, 2, "2-take");
   CHECK(a.action == 1 && a.slot == 2);
   /* load a probe: the newest is chosen first (3) */
   a = run({ PAD_DOWN, PAD_DOWN, PAD_CROSS, PAD_CROSS }, 2, "3-load");
   CHECK(a.action == 2 && a.slot == 3);
   /* reset (below the stats overlay's switch), quit */
   a = run({ PAD_DOWN, PAD_DOWN, PAD_DOWN, PAD_DOWN, PAD_CROSS });
   CHECK(a.action == 3);
   a = run({ PAD_UP, PAD_CROSS });
   CHECK(a.action == 4);
   /* back out of a page with Circle, then resume */
   a = run({ PAD_DOWN, PAD_CROSS, PAD_CIRCLE, PAD_CROSS });
   CHECK(a.action == 0);
   /* the textures are given back at each close; every close holds the game's input */
   CHECK(g_textures.empty());
   CHECK(g_holds == 6);
   CHECK(g_presents > 30);
   if (failures) {
      std::fprintf(stderr, "test_quickmenu: %d check(s) failed\n", failures);
      return 1;
   }
   std::printf("test_quickmenu: all checks passed (%d frames drawn)\n", g_presents);
   return 0;
}
