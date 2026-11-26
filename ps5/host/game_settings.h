/*
 * PSGopher - a game's own settings: each of BENCH's Video, Screen & Sound, Emulation and Controllers
 * rows can be set for one game and starts at "Use Global Setting". They are kept per game in
 * /data/psgopher/config/games/<key>.ini (the key: library.h's gameKey, the header's game code with
 * its destination and the revision), applied when the game starts over the global settings
 * (Gopher64's through its config, the title's through a copy of TitleSettings) and put back when it
 * ends. The file is plain "row key = number" lines, one per setting the game has.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "title_settings.h"

namespace psgopher {

struct GameSettings {
	std::vector<std::pair<std::string, int64_t>> values; // BENCH's row key (Gopher64's: "video.upscale") and its value
	bool empty() const { return values.empty(); }
	const int64_t *find(const std::string &key) const;
	void set(const std::string &key, int64_t value);
	void clear(const std::string &key);
	/* "video.upscale=4, final_filter=1" (the log's line) */
	std::string text() const;
};

/* Whether a row can be set for one game: Gopher64's video, emulation and controller switches, and the
 * title's own picture and performance rows (TitleSettings::isField). Not the display's refresh rate and
 * HDR10, hotkeys, profiles, files or the diagnostics: those are the console's. */
bool gameSettingEligible(const std::string &row_key);

std::string gameSettingsFile(const std::string &key);
GameSettings loadGameSettings(const std::string &key);
/* Writes the game's file (taken away when the game has none left) */
bool saveGameSettings(const std::string &key, const GameSettings &settings);

/* A game's settings applied for its length. settings() is the global TitleSettings with the game's
 * rows over it (what applyHdr is given); Gopher64's config has the game's rows until restore() (also
 * the destructor), with the globals kept in restore.txt meanwhile so that a game that never ended
 * (the console closed, a crash) is put right at the next start (recoverGameSettings). Logs
 * "settings: per-game for <key>: ..." or "settings: no per-game settings for <key>". The Expansion Pak is Gopher64's too
 * (its "Disable Expansion Pak"), set the same way from the row's Auto, On or Off (expansion_pak.h) and logged as
 * "settings: expansion pak <on|off> (<required|enhanced|incompatible|not listed>; <source>)". */
class GameScope {
public:
	/* crc: the game's Game::crc ("" none), for the Expansion Pak's table */
	GameScope(const std::string &key, bool key_from_header, const TitleSettings &global, const std::string &crc = std::string());
	~GameScope();
	GameScope(const GameScope &) = delete;
	GameScope &operator=(const GameScope &) = delete;
	const TitleSettings &settings() const { return effective_; }
	void restore();

private:
	TitleSettings effective_;
	std::vector<std::pair<std::string, int64_t>> before_; // Gopher64's rows as they were
};

/* Puts Gopher64's config back after a game that did not end (title start) */
void recoverGameSettings();

} // namespace psgopher
