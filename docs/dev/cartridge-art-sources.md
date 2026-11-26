# Cartridge art: sources, licensing and attribution

Research of 2026-11-26, from the pages listed below. "Unconfirmed" means the page was not read,
not that the claim is false. Games are not named. The design is in
[`cartridge-art.md`](cartridge-art.md).

## Summary

No source found licenses real N64 cartridge fronts for redistribution. PSGopher therefore ships
no images and keeps none in its repository. The pipeline fetches with the owner's own access,
and its output stays on the owner's machines, with each image's source and terms recorded in the
manifest. The owner chose ScreenScraper, the n64-flashcart-menu-metadata repository and their own
photos.

## The sources

### ScreenScraper (chosen)

- **Access**: a member account; software use needs a developer id. The daily quota is tied to
  the IP and rises with donation tiers to 50,000-100,000 requests a day. Extra accounts and
  sharing an account are banned.
- **Media**: it carries cartridge ("support") media. The exact media names, region keys and N64
  coverage are unconfirmed, and so is whether the images are straight-on shells.
- **Licence**: contributors license their media under CC BY-NC-SA 4.0. The terms for media reused
  through the API are unconfirmed; ask the operators.
- **Attribution**: ScreenScraper and the contributor, per image, kept in the manifest.

### n64-flashcart-menu-metadata (chosen)

- **Repositories**: github.com/n64-tools/n64-flashcart-menu-metadata, for
  github.com/Polprzewodnikowy/N64FlashcartMenu.
- **Layout**: `metadata/<c1>/<c2>/<c3>/<c4>/`, by the game code's characters. Each folder holds
  `gamepak_front` and `gamepak_back` PNGs, box art and `metadata.ini`.
- **Regions**: destination `E` is the fallback; other destinations get their own folder only when
  they differ.
- **Coverage and size**: incomplete, by its own account (a `to_sort` folder, and no current PAL
  art). The size of the gamepak images was not read.
- **Licence**: the repository is Unlicense, but its credits show the NTSC cartridge art comes from
  ABeezy's pack, which states no licence. The rights to the images are therefore not clean:
  personal use only, never redistributed.

### The owner's photos (chosen)

- **Rights**: the owner's own, and the highest trust.
- **Naming**: by product code (`NUS-NXXP-EUR.png`, a variant suffix where needed).

### ABeezy's Nintendo 64 Carts (not chosen)

- **Where**: forums.launchbox-app.com, file 2605. Version 2.0.0 (April 2022), 957 MB.
- **Contents**: 296 NTSC fronts, 15 alternates and 7 PAL-only, with none from Japan. They look
  rendered on a common shell template ("reduced light sources").
- **Access and licence**: an account is needed to download, and no licence is stated.

### LaunchBox Games Database "Cart - Front" (not chosen)

- **Images**: a region per image, 400-1200 px, with uneven coverage.
- **Terms**: unread. The images are user-contributed. Its image CDN is not a documented interface.

### EmuMovies (not chosen)

Paid membership, with FTP and a sync app. The terms for this use are unconfirmed.

### NESWORLD worldwide catalogue (reference only)

- **Contents**: per-region release pages with GamePak (NUS) product codes and scans where
  available (from search snippets).
- **Automated reading**: refused (HTTP 403). It is read by hand to check product codes, never
  scraped.

### libretro-thumbnails (the current fallback)

- **Images**: Named_Boxarts, Named_Logos, Named_Snaps and Named_Titles, with no cartridge type.
  Names are No-Intro's.
- **Licence**: none stated. It is used today as box art, fetched per game at run time.

## Identification

- **The product code** is printed as `NUS-TGGR-V`:
  - T is the type: N, a cartridge;
  - GG is the game;
  - R is the region (E USA, P Europe, J Japan, …);
  - V is the version.
  - Printed suffixes such as `-USA`, `-EUR`, `-AUS` and `-NOE` are also seen (dumping.guide;
    nintendo64ever.com).
- **The ROM header** holds the game code at 0x3B-0x3E (category, two-letter ID, destination) and
  the revision at 0x3F, per the n64brew wiki (read through a search snippet; the full destination
  table is to be confirmed).
- **No-Intro's DAT**: whether it carries serials is unconfirmed (check a DAT-o-MATIC download).

## Pages read

- https://forums.launchbox-app.com/files/file/2605-abeezys-nintendo-64-carts/
- https://github.com/n64-tools/n64-flashcart-menu-metadata
- https://github.com/networkfusion/N64FlashcartMenu
- https://github.com/libretro-thumbnails/Nintendo_-_Nintendo_64
- https://screenscraper.fr/membreinscription.php
- https://dumping.guide/carts/nintendo/n64
- https://emumovies.com/files/category/1101-console-media/
- Search snippets only: n64brew.dev/wiki/ROM_Header, nesworld.com's N64 list
