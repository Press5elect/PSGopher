/*
 * PSGopher - pictures for the front end: a PNG file to RGBA8 pixels (stb_image), kept once decoded.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psgopher {

struct Image {
	int width = 0, height = 0;
	std::vector<uint8_t> rgba; // empty when the file could not be read
};

/* The picture at path, decoded the first time it is asked for and kept (the board is 4000x2500:
 * decoded once a run of the title, not each time the list comes back). */
const Image &loadImage(const std::string &path);

/* the most pixels a side of a picture decodeImage reads (covers, probe pictures) */
constexpr int kMaxPictureSide = 2048;

/* The picture at path, decoded each time (the covers: decoded on their thread, not kept here); an
 * empty one when it is missing, not a PNG, or larger than kMaxPictureSide a side */
Image decodeImage(const std::string &path);

} // namespace psgopher
