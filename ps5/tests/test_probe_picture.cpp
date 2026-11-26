/*
 * PSGopher - a probe's picture (ps5/host/probe_picture.h): the path, the PNG (decoded again by
 * stb_image), the atomic write and its replacement, the removal, the impossible sizes.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <chrono>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

#include "probe_picture.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static std::vector<uint8_t> pattern(int w, int h, int seed)
{
   std::vector<uint8_t> p((size_t)w * h * 3);
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
         uint8_t *d = &p[((size_t)y * w + x) * 3];
         d[0] = (uint8_t)(x + seed);
         d[1] = (uint8_t)(y * 3 + seed);
         d[2] = (uint8_t)((x ^ y) + seed);
      }
   return p;
}

static bool decodes(const std::string &path, int w, int h, const std::vector<uint8_t> &want)
{
   int gw = 0, gh = 0, n = 0;
   uint8_t *px = stbi_load(path.c_str(), &gw, &gh, &n, 3);
   const bool ok = px && gw == w && gh == h && std::equal(want.begin(), want.end(), px);
   stbi_image_free(px);
   return ok;
}

int main(int argc, char **argv)
{
   const std::string dir = argc > 1 ? argv[1] : "/tmp/psgopher-probe-picture";
   if (system(("rm -rf '" + dir + "' && mkdir -p '" + dir + "'").c_str()) != 0)
      return 2;
   const std::string state = dir + "/Name-0123abcd.state3";

   CHECK(psgopher::probePicturePath(state) == state + ".png");

   /* the sizes a game gives: 640x480, 640x240, 320x240, and a small odd one (a block's edge) */
   const int sizes[][2] = { { 640, 480 }, { 640, 240 }, { 320, 240 }, { 3, 5 }, { 1, 1 }, { 21845, 1 } };
   for (const auto &s : sizes) {
      if (s[0] > 16384)
         continue;
      const auto px = pattern(s[0], s[1], s[0]);
      CHECK(psgopher::writePng(psgopher::probePicturePath(state), s[0], s[1], px.data()));
      CHECK(decodes(psgopher::probePicturePath(state), s[0], s[1], px));
   }

   /* a replacement is whole; no temporary file stays */
   const auto first = pattern(640, 480, 1), second = pattern(640, 480, 9);
   CHECK(psgopher::writePng(psgopher::probePicturePath(state), 640, 480, first.data()));
   CHECK(decodes(psgopher::probePicturePath(state), 640, 480, first));
   CHECK(psgopher::writePng(psgopher::probePicturePath(state), 640, 480, second.data()));
   CHECK(decodes(psgopher::probePicturePath(state), 640, 480, second));
   int files = 0;
   if (DIR *d = opendir(dir.c_str())) {
      while (dirent *e = readdir(d))
         files += e->d_name[0] != '.';
      closedir(d);
   }
   CHECK(files == 1);

   /* a failed write leaves the old picture and no temporary file */
   CHECK(!psgopher::writePng(psgopher::probePicturePath(state), 0, 480, second.data()));
   CHECK(!psgopher::writePng(dir + "/missing/x.png", 640, 480, second.data()));
   CHECK(decodes(psgopher::probePicturePath(state), 640, 480, second));

   /* the cost of a full picture (the encoding and the write) */
   const auto t0 = std::chrono::steady_clock::now();
   for (int i = 0; i < 20; i++)
      psgopher::writePng(psgopher::probePicturePath(state), 640, 480, second.data());
   const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20;
   printf("probe picture 640x480: %.2f ms per write\n", ms);

   /* the removal */
   psgopher::removeProbePicture(state);
   struct stat st;
   CHECK(stat(psgopher::probePicturePath(state).c_str(), &st) != 0);

   printf(failures ? "test_probe_picture: %d failure(s)\n" : "test_probe_picture: ok\n", failures);
   return failures ? 1 : 0;
}
