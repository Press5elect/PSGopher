/*
 * PSGopher - pictures for the front end (image.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "image.h"

#include <map>
#include <mutex>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_WARNINGS
#include "stb_image.h"

extern "C" void say(const char *format, ...) __attribute__((format(printf, 1, 2)));

namespace psgopher {

Image decodeImage(const std::string &path)
{
	Image img;
	int channels = 0;
	/* the size is read from the header first: a file claiming more than 2048 pixels a side is not
	 * decoded (FE-030: a decompression bomb would take the memory) */
	int w = 0, h = 0;
	if (!stbi_info(path.c_str(), &w, &h, &channels) || w <= 0 || h <= 0 || w > kMaxPictureSide || h > kMaxPictureSide)
		return img;
	if (stbi_uc *pixels = stbi_load(path.c_str(), &img.width, &img.height, &channels, 4)) {
		img.rgba.assign(pixels, pixels + (size_t)img.width * img.height * 4);
		stbi_image_free(pixels);
	} else {
		img.width = img.height = 0;
	}
	return img;
}

const Image &loadImage(const std::string &path)
{
	static std::mutex mutex;
	static std::map<std::string, Image> kept;
	std::lock_guard lock(mutex);
	auto it = kept.find(path);
	if (it != kept.end())
		return it->second;
	Image &img = kept[path];
	int channels = 0;
	if (stbi_uc *pixels = stbi_load(path.c_str(), &img.width, &img.height, &channels, 4)) {
		img.rgba.assign(pixels, pixels + (size_t)img.width * img.height * 4);
		stbi_image_free(pixels);
		say("image: %s %dx%d", path.c_str(), img.width, img.height);
	} else {
		say("image: %s could not be read (%s)", path.c_str(), stbi_failure_reason());
		img.width = img.height = 0;
	}
	return img;
}

} // namespace psgopher
