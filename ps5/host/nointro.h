/*
 * PSGopher - a cartridge's No-Intro name by its header's game ID and revision (gamedata/nointro-ids.tsv,
 * made by ps5/tools/make-nointro-ids.py from the No-Intro database Gopher64 carries). Covers and
 * descriptions are looked up by it, so a ROM file's own name does not matter.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#pragma once

#include <string>

namespace psgopher {

/* The table's file (read once, when a name is first asked for). */
void noIntroUseFile(const std::string &path);

/* The No-Intro name of the cartridge with this four-letter game ID ("NSME") and header revision
 * (-1: unknown): a retail release first, the one of that revision ("(Rev 1)"; none for 0), the
 * shortest of equals. "" when the ID is not in the table. */
std::string noIntroName(const std::string &id, int revision);

} // namespace psgopher
