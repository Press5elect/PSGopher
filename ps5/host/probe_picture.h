/*
 * PSGopher - a probe's picture: the file next to a save state that shows the moment it was taken.
 *
 * The contract (docs/dev/architecture.md, "Probe pictures"):
 *   - the probe is data/states/<name>-<hash>.state<slot>; its picture is the same path plus ".png"
 *     (probePicturePath), a PNG of 8-bit RGB, no alpha;
 *   - the pixels are what the video interface outputs, at the N64's own resolution (typically 640x480
 *     or 640x240 for a 320x240 game; the pixels are not square: show it at 4:3), without the
 *     upscaling, the overlay or the board;
 *   - it is written when the probe is taken, by temporary file and rename (a reader sees the old
 *     picture or the new one, never half of one), and replaced when the probe is overwritten;
 *   - a probe may have no picture (an older probe, a blank screen): look for the file, and draw
 *     the fallback when it is not there. Nothing in the title deletes probes; a reader that does
 *     removes the picture with it (removeProbePicture).
 *
 * The core calls psgopher_probe_picture_write (quickmenu.cpp) with the picture's path and pixels.
 * The PNG is written with stored (uncompressed) deflate blocks: no library, and a picture of
 * 640x480 is 0.9 MB, small next to the save state beside it.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

namespace psgopher
{

/* the picture of a probe's save state file: the file's path and ".png" */
inline std::string probePicturePath(const std::string &stateFile)
{
   return stateFile + ".png";
}

/* the picture goes when its probe does */
inline void removeProbePicture(const std::string &stateFile)
{
   unlink(probePicturePath(stateFile).c_str());
}

namespace png_detail
{
inline uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
   static uint32_t table[256];
   static bool ready = false;
   if (!ready) {
      for (uint32_t i = 0; i < 256; i++) {
         uint32_t v = i;
         for (int k = 0; k < 8; k++)
            v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
         table[i] = v;
      }
      ready = true;
   }
   c = ~c;
   for (size_t i = 0; i < n; i++)
      c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
   return ~c;
}
inline void be32(std::vector<uint8_t> &o, uint32_t v)
{
   for (int s = 24; s >= 0; s -= 8)
      o.push_back((uint8_t)(v >> s));
}
inline void chunk(std::vector<uint8_t> &o, const char *type, const std::vector<uint8_t> &data)
{
   be32(o, (uint32_t)data.size());
   const size_t at = o.size();
   o.insert(o.end(), type, type + 4);
   o.insert(o.end(), data.begin(), data.end());
   be32(o, crc(0, o.data() + at, o.size() - at));
}
}

/* a PNG (8-bit RGB, rows of width*3 bytes) as bytes in memory; empty for an impossible size */
inline std::vector<uint8_t> encodePng(int width, int height, const uint8_t *rgb)
{
   using namespace png_detail;
   std::vector<uint8_t> out;
   if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || !rgb)
      return out;
   const size_t row = (size_t)width * 3 + 1; /* (the filter byte: none) */
   std::vector<uint8_t> raw(row * (size_t)height);
   for (int y = 0; y < height; y++) {
      raw[row * y] = 0;
      std::copy(rgb + (size_t)y * width * 3, rgb + (size_t)(y + 1) * width * 3, raw.begin() + row * y + 1);
   }
   /* zlib: the header, stored blocks of up to 65535 bytes, Adler-32 */
   std::vector<uint8_t> z = { 0x78, 0x01 };
   uint32_t a = 1, b = 0;
   for (size_t at = 0; at < raw.size();) {
      const size_t n = raw.size() - at < 5552 ? raw.size() - at : 5552; /* (the most before a reduction) */
      for (size_t i = 0; i < n; i++) {
         a += raw[at + i];
         b += a;
      }
      a %= 65521;
      b %= 65521;
      at += n;
   }
   for (size_t at = 0; at < raw.size() || at == 0;) {
      const size_t n = raw.size() - at < 65535 ? raw.size() - at : 65535;
      const bool last = at + n >= raw.size();
      z.push_back(last ? 1 : 0);
      z.push_back((uint8_t)(n & 0xFF));
      z.push_back((uint8_t)(n >> 8));
      z.push_back((uint8_t)(~n & 0xFF));
      z.push_back((uint8_t)((~n >> 8) & 0xFF));
      z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
      at += n;
      if (last)
         break;
   }
   be32(z, (b << 16) | a);

   static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
   out.insert(out.end(), signature, signature + 8);
   std::vector<uint8_t> header;
   be32(header, (uint32_t)width);
   be32(header, (uint32_t)height);
   header.insert(header.end(), { 8, 2, 0, 0, 0 }); /* 8 bits, RGB, deflate, filter 0, no interlace */
   chunk(out, "IHDR", header);
   chunk(out, "IDAT", z);
   chunk(out, "IEND", {});
   return out;
}

/* the PNG at path, by a temporary file and a rename; false (and no file left) when it fails */
inline bool writePng(const std::string &path, int width, int height, const uint8_t *rgb)
{
   const std::vector<uint8_t> png = encodePng(width, height, rgb);
   if (png.empty())
      return false;
   const std::string temporary = path + ".tmp";
   FILE *f = fopen(temporary.c_str(), "wb");
   if (!f)
      return false;
   bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
   ok = fflush(f) == 0 && ok;
   ok = fclose(f) == 0 && ok;
   if (!ok || rename(temporary.c_str(), path.c_str()) != 0) {
      unlink(temporary.c_str());
      return false;
   }
   return true;
}

}
