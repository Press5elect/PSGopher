/*
 * PSGopher - the title's own settings, /data/psgopher/config/psgopher.ini (Gopher64's are its
 * config.json): the display's refresh and HDR10. The game list's Settings edit them; the file stays
 * one that can be edited over FTP.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <string>

namespace psgopher {

struct TitleSettings {
	std::string display{ "60" }; // 60, 120 or vrr: from the title's start (the display opens once)
	bool hdr{ false };           // HDR10 when the console's output is HDR: from the next game
	int hdrWhite{ 203 };         // nits, 80..400
	int hdrPeak{ 1000 };         // nits, 0 (off) or up to 4000
	bool stats{ false };         // the stats overlay in a game
	std::string file;            // "" is /data/psgopher/config/psgopher.ini (the host tests give another)

	/* Reads the file (written with the defaults when it is missing). */
	void load();
	/* Writes it whole. */
	bool save() const;
	/* The driver's display switches: once, before anything opens the display. title_dir is where
	 * the title folder is now (its sce_sys/param.json declares 119.88 Hz). */
	void applyDisplay(const std::string &titleDir) const;
	/* parallel-rdp's HDR10 switches: before each game (vendor/gopher64/parallel-rdp/interface.cpp). */
	void applyHdr() const;
};

extern const char kTitleSettingsFile[];

} // namespace psgopher
