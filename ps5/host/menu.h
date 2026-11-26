/*
 * PSGopher - the game list and its settings, drawn with Dear ImGui on the console's display through
 * RADV, between games (each game's Vulkan is Gopher64's own: the list makes and ends its own).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <string>

namespace psgopher {

struct MenuResult {
	bool play{ false }; // false: the title is to close
	std::string rom;    // the ROM to play (a local path: a share's was copied)
	std::string name;   // the game's name in the list (its play time is kept by it)
	int load_state{ -1 }; // a probe (save state slot) to start from, -1 none
	bool benchmark{ false }; // a benchmark from that probe (1x 2x 4x 8x), not a game to play
	std::string key;    // the game's key for its own settings (library.h's gameKey)
	bool keyFromHeader{ true }; // false: the key is the file's name (no header facts)
};

/* Shows the list until a game is chosen or the title is closed. `notice` is said at the top once
 * (why the last game ended, a failed copy). */
MenuResult runMenu(const std::string &notice);

/* Mounts the shares of /data/psgopher/config/network.cfg (an example is written the first time). */
void startShares(const std::string &titleDir);

} // namespace psgopher
