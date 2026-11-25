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
};

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
};

Library &library();

/* The tests' hook: the console's /data is `root` (the title's data at <root>/psgopher). */
void libraryUseRoot(const std::string &root);

/* A share's ROM is copied to /data/psgopher/cache/play first (Gopher64 reads its ROM with std::fs);
 * a local path is given as it is. Returns the path to give Gopher64, or "" (and why). */
std::string localCopy(const std::string &path, std::string *error);

} // namespace psgopher
