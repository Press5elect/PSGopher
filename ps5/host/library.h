/*
 * PSGopher - the game list: N64 ROMs found on the console's storage, USB drives and network shares.
 *
 * The places are OGBox's and Crux2's (the console's /data, USB drives /mnt/usb0-7, extended storage
 * /mnt/ext0-1, every share mounted at /smb/<name>), each searched a few folders deep on a thread.
 * The list is kept in /data/psgopher/cache/library.tsv, so the next start shows it at once.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psgopher {

enum Tab { TabInternal = 0, TabUsb = 1, TabNetwork = 2, TabCount = 3 };
const char *tabName(int tab);
int tabOfPath(const std::string &path);

struct Game {
	std::string name;   // the file's name without its extension
	std::string path;   // where it is (a /smb/... path is on a share)
	uint64_t size{ 0 };
	int tab{ TabInternal };
	/* from the ROM's first 4 KiB (a .zip or .7z has none): Gopher64's facts (psgopher_rom_facts) */
	bool facts{ false };
	std::string id;     // the header's four-letter code ("NABE": category, game, region)
	std::string header; // the header's name, as Gopher64 names its saves by it (saveName)
	uint32_t cic{ 0 };  // 6101..6106, 5167; 0 unknown
	uint32_t save{ 0 }; // bits: 1 EEPROM 4K, 2 EEPROM 16K, 4 SRAM, 8 SRAM 768K, 16 SRAM 1M, 32 Flash
	bool rumble{ false }; // the controllers hold a Rumble Pak by default (else a Controller Pak)
	std::string crc;    // Gopher64's key of the game ("CRC1-CRC2-C:country": its cheats, by storage::get_game_crc)
};

/* "EEPROM 4K", "Flash", "EEPROM 4K or SRAM" (Gopher64's choice for a game it does not list), "None" */
std::string saveText(uint32_t save);
/* "CIC-6102"; "Unknown" */
std::string cicText(uint32_t cic);
/* The region the header's last letter names ("USA", "Europe", "Japan"...), or "" */
std::string regionText(const std::string &id);

/* Gopher64's name for a game's saves and states (storage.rs get_game_name: the header's name with
 * only letters, digits, '_', ' ', '-'; trimmed), "" when it has none. */
std::string saveName(const std::string &header);

/* A game's save states ("probes"), slots 0-9, newest first: data/states/<saveName>-*.state<slot>. */
struct Probe {
	int slot;
	int64_t time;
};
std::vector<Probe> probesOf(const Game &g);

/* Play time, by the game's name: kept in /data/psgopher/cache/played.tsv. */
struct Played {
	double seconds{ 0 };
	int64_t last{ 0 }; // when it was last played (0 never)
};
Played playedOf(const std::string &name);
/* every game's, by name (one read of the file) */
std::vector<std::pair<std::string, Played>> playedAll();
void addPlayed(const std::string &name, double seconds);

/* Whether a file name is one the list shows (.z64 .n64 .v64; .zip whose first file is one; .7z
 * in a folder named for the N64 or in /data/psgopher/roms). */
bool isRomName(const std::string &name);

class Library {
public:
	/* The list kept from the last search (empty the first time). */
	void load();
	/* Starts a search of every place on a thread (one at a time; a second call while one runs is
	 * ignored). The list is replaced when it ends and kept. */
	void rescan();
	bool scanning() const;
	/* A copy of the list, sorted by name; `version` changes whenever the list does. */
	std::vector<Game> games(unsigned *version = nullptr) const;
	/* What the search is looking at now ("" when none). */
	std::string status() const;
	/* Gopher64's save states' folder (data/states) */
	std::string statesFolder() const;
};

Library &library();

/* The tests' hook: the console's /data is `root` (the title's data at <root>/psgopher). */
void libraryUseRoot(const std::string &root);

/* A share's ROM is copied to /data/psgopher/cache/play first (Gopher64 reads its ROM with std::fs);
 * a local path is given as it is. Returns the path to give Gopher64, or "" (and why). */
std::string localCopy(const std::string &path, std::string *error);

} // namespace psgopher
