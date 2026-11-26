# Which games use the Expansion Pak

PSGopher knows which cartridges need the Expansion Pak (the N64's extra 4 MB, 8 MB in all), which use
it for more, and which misbehave with it and must run with the Jumper Pak (4 MB). The data is
`ps5/gamedata/expansion-pak.tsv` (shipped in the title folder as `gamedata/expansion-pak.tsv`), read by
`ps5/host/expansion_pak.cpp`. This page is where it comes from and how it is used.

## What it does

- BENCH > Emulation > **Expansion Pak** is Auto (Default), On or Off; it replaces Gopher64's
  "Disable Expansion Pak" switch (an On/Off pair that said it the other way round). It is also a row of
  a game's page (Use Global Setting first).
- **Auto** turns the Expansion Pak on (Gopher64's own default) for every game except one listed as
  *incompatible*. A game that is *required*, *enhanced* or not listed gets it. **On** and **Off** force it.
  A game's own choice on its page wins over the global one, Auto included.
- At a game's start `GameScope` (`game_settings.cpp`) settles the row and sets Gopher64's
  `emulation.disable_expansion_pak` for the length of the game, journalled and put back like the game's
  other rows. The boot log says `settings: expansion pak <on|off> (<required|enhanced|incompatible|not
  listed>; <source>)`.
- HOME's panel and the game's page show **Expansion Pak: Required / Enhanced / Must be off / Not used**
  ("Not used" is a game that is not in the table) and **N64 Memory: 8 MB / 4 MB**, as set for that game.
  Starting a *required* game with the Expansion Pak set to Off asks first (Cross starts anyway, Circle
  goes back).
- Benchmark mode runs with Gopher64's switch as it is (no game's settings are applied to it).
- A psgopher.ini from before the row (no `expansion_pak` line) takes Gopher64's old switch over once: On
  there becomes Off here. A game's file with the old `emulation.disable_expansion_pak` line reads as the
  new row.

## The file

Tab-separated, `#` comments: `key`, `status`, `sources`, `confidence`.

- `key`: a game code (`NXXE`: the header's four letters, destination included), a game code and the
  header's revision byte in hex (`NXXE-01`, the key of a game's own settings), or `CRC1-CRC2`
  (`library.cpp`'s `Game::crc` without its `-C:xx`). A CRC entry is looked up first, then the code with
  its revision, then the code.
- `status`: `required`, `enhanced`, `incompatible`.
- `sources`: `nintendo`, `wikipedia`, `pj64`, `m64p` (below).
- `confidence`: `verified` (two independent sources agree), `cited` (one source that names its own
  sources), `unverified` (one weak source).

146 entries: 9 required (all verified), 133 enhanced (cited), 4 incompatible (1 verified, 3 unverified).
The verified ones are the codes of the three required games (three regions each) and the one
incompatible CRC.

## Sources, and what was taken from each

Facts (which game needs 8 MB) are not copyrightable; nothing was copied but the fact and a game's code.
No titles are kept in the repository.

| Id | Source | Licence | Used for |
| --- | --- | --- | --- |
| `nintendo` | Nintendo's accessories page for the Nintendo 64 (nintendo.com, "Accessories"); the list of Expansion Pak games Wikipedia cites is an archived page of Nintendo of America | Nintendo's, all rights reserved: a fact is taken, no text | Which two games require it, and that one more runs in a restricted mode without it |
| `wikipedia` | English Wikipedia, "Nintendo 64 accessories", the Expansion Pak section (a table of the games that support it with "Pak required", and the text on a game that may crash with it) | CC BY-SA 4.0 | The games it improves; the required games; the game that may crash with it |
| `pj64` | Project64's `Project64.rdb` (v4.0 official, 2022/08/20), per game `RDRAM Size=`, keyed by CRC1-CRC2-country | GPL-2.0 (the project's `license.md`); the file has no licence line of its own | The one game with `RDRAM Size=4` (by CRC) |
| `m64p` | Mupen64Plus-Core's `mupen64plus.ini`, field `DisableExtraMem=1` (3 entries of 3,048), keyed by CRC | GPL-2.0 (the core's `LICENSES`) | Three cartridges that must run without the extra memory (unverified: no second source) |

Looked at and not used:

- **ares** (ISC): the Expansion Pak is a system setting there (`system.expansionPak`); its game database
  (`mia`) has no memory field. No data.
- **Gopher64** (`vendor/gopher64/src`): only the global `disable_expansion_pak` (config, command line,
  netplay); no per-game memory handling.
- **Project64's `RDRAM Size=8`**: 1,180 of 1,584 entries carry it, including games that do not use the
  Expansion Pak at all; it reads as "may have 8 MB", not as a need, so those entries add nothing here.
  Project64 and the Mupen64Plus ini also disagree on one cartridge: the ini says to disable the memory
  for the Japanese version of the three, Project64 has `RDRAM Size=8` on it. It stays in as unverified:
  Off is the stock console's memory and cannot break a game that does not use the Expansion Pak.
- **Retailer and fan lists** (a retailer's blog post, a game site's article): the same Wikipedia and
  Nintendo facts and a few more titles with no source named. Not taken.
- **libretro-database** (`metadat/no-intro`, a Creative Commons licence in its `LICENSE`): used only to
  turn a title into the header's four-letter code (its `serial` is the code); no other data taken. The
  codes of the listed titles came from matching names there, so a title whose name differs in that list
  may be missing.

## How an entry was made

1. Wikipedia's table and Nintendo's page gave titles, and "required" or not.
2. Each title became its codes (every region the list has; the code covers every revision) through the
   No-Intro names and serials. A regional edition with a code of its own that Wikipedia does not describe
   (the Japanese edition of one sequel) was left out.
3. Required: Nintendo says two games require the Expansion Pak and a third runs in a restricted mode
   without it; Wikipedia agrees (and says the Japanese version of the third fully requires it). All three
   are `required`, `verified`.
4. Enhanced: every other Wikipedia row, including those where it only unlocks some content (multiplayer, a
   campaign, a mode). All `cited`.
5. Incompatible: the one `RDRAM Size=4` of the RDB (one revision of a US cartridge, by CRC), which
   Wikipedia's text agrees with (the game may crash in areas with the Expansion Pak): `verified`. The
   three `DisableExtraMem` entries of the Mupen64Plus ini (by CRC): `unverified`.

## What could not be confirmed

- No source says which other games crash with 8 MB; there may be more.
- "Enhanced" is Wikipedia's list; the retailer lists name more games than its table. Those are "not
  listed" (Auto: on) until a source can be cited. The panel's "Not used" for them is the table's word,
  not a measurement.
- Whether Project64 means `RDRAM Size=8` as a need or as its choice for a game that runs well either way.
- Nothing here has run on a console. Auto's choice and the log line are host-tested; whether the four
  incompatible cartridges really misbehave with 8 MB in Gopher64 is not known.
