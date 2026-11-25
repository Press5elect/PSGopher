/*
 * PSGopher - what the front end fetches from the network: each cartridge's box art (libretro's
 * thumbnails) and its description (Wikipedia's summary, found as OGBox finds one). One thread of
 * its own asks for what the front end shows, the latest asked first; everything is kept in
 * /data/psgopher/cache/covers (<name>.png, about4-<name>.txt; <name>.none and about4-<name>.none
 * when there is none, asked again after 30 days). The fetches themselves are the core's
 * (psgopher.rs: rustls over a blocking socket, Tokio's sockets being refused on the console).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <string>

#include "image.h"

namespace psgopher {

/* The folder (made if missing); off until started: the tests and an offline console ask nothing */
void webStart(const std::string &folder);

enum class WebState { Pending, Ready, None };

/* The cartridge's cover by its file name (No-Intro's, as libretro names them): Ready hands its
 * decoded picture over once (into *picture), None when there is none; Pending asks for it. */
WebState webCover(const std::string &name, Image *picture);

/* A cover handed over and let go (its texture freed): asked for again when it is next shown */
void webForget(const std::string &name);

/* Its description: "title\ntext"; "" while it is asked for or when there is none */
std::string webAbout(const std::string &name);

} // namespace psgopher
