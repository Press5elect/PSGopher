/*
 * PSGopher - which games use the Expansion Pak (ps5/gamedata/expansion-pak.tsv, docs/dev/expansion-pak.md):
 * required, enhanced, or incompatible (a game that misbehaves with 8 MB and must run with the Jumper Pak).
 * The Expansion Pak row of BENCH is Auto, On or Off; Auto leaves it on, except for a game that is
 * incompatible with it. A game that is not listed is "not listed".
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psgopher {

enum class PakStatus { NotListed, Required, Enhanced, Incompatible };

struct PakInfo {
	PakStatus status{ PakStatus::NotListed };
	std::string source; // the entry's sources ("nintendo,wikipedia"), "" when not listed
};

/* The log's word ("required", "enhanced", "incompatible", "not listed") and the game's page's ("Required",
 * "Enhanced", "Must be off", "Not used") */
const char *pakStatusName(PakStatus s);
const char *pakFactText(PakStatus s);

/* BENCH's row, as TitleSettings::expansionPak and the per-game file hold it */
enum PakSetting { PakAuto = 0, PakOn = 1, PakOff = 2 };

/* Whether the Expansion Pak is on for a game: On and Off as set, Auto off only for an incompatible game */
bool pakIsOn(int setting, PakStatus status);

/* The table: "key<TAB>status<TAB>sources<TAB>confidence" lines, # comments. A key is a game code ("NXXE"), a game
 * code with its revision ("NXXE-01") or the cartridge's CRC1-CRC2. */
class PakTable {
public:
	/* adds the lines of `text`; returns how many entries were added (a line that is not one is left out) */
	size_t parse(const std::string &text);
	bool loadFile(const std::string &path);
	size_t size() const { return entries_.size(); }
	/* key: library.h's gameKey ("NXXE-01"; "file-..." has no code); crc: Game::crc ("CRC1-CRC2-C:xx", "" none).
	 * The CRC's entry first, then the code with the revision, then the code alone. */
	PakInfo find(const std::string &key, const std::string &crc) const;

private:
	struct Entry {
		std::string key;
		PakStatus status;
		std::string source;
	};
	std::vector<Entry> entries_;
};

/* The table the title ships (the title folder's gamedata/expansion-pak.tsv), read once on first use */
void pakUseFile(const std::string &path);
const PakTable &pakTable();
PakInfo pakInfoOf(const std::string &key, const std::string &crc);

} // namespace psgopher
