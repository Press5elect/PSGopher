# Cartridge art: design

Status: a design awaiting the owner's approval, not built (2026-11-26). Sources chosen by the owner:
ScreenScraper, the n64-flashcart-menu-metadata repository, and the owner's own photos. The owner wants real N64 cartridge fronts as the
library's artwork: the shell with its original label, correct for the region and physical variant
of the ROM in hand. Box art may stay as a fallback but is never shown as a cartridge.

## What exists today

From the code (details in the inventory of 2026-11-26):

- **Fetching.** Covers are libretro-thumbnails' `Named_Boxarts`, fetched by the file name taken as a
  No-Intro name (`psgopher.rs:978`, `web.cpp`). There is no header, CRC or serial in the lookup,
  and no manifest: the state is the `.png` and `.none` files in `cache/covers/`.
- **Identity.** `library.cpp:readFacts` reads the header's game code (0x3B-0x3E) and CRC1/CRC2. The
  revision byte (0x3F) is not stored, and `.zip` and `.7z` files get no facts at all.
- **Decoding.** Only PNG is decoded (`image.cpp`, `STBI_ONLY_PNG`), and there is no size check
  before decoding (FE-030).
- **Drawing.** Every view takes its art through `Frontend::lookFor` and `Menu::cover`:
  - The strip draws a procedural shell and bends the cover into its label recess (236x208,
    `board.cpp:drawLabelPart`).
  - The RACK panel shows a large label.
  - The textures are an LRU of 48, RGBA8 with mips, with one upload a frame.

## Sources

None of the sources researched (2026-11-26) licenses real cartridge fronts for redistribution, so
**no images ship in the title or the repository**. The pipeline fetches with the owner's own
access, and its output stays on the owner's PC and console.

| Source | Cartridge fronts | Regions | Key | Access | Use |
| --- | --- | --- | --- | --- | --- |
| ScreenScraper | "support" media (to confirm) | per region (to confirm) | its game id; CRC, serial, name searches | account + developer id; daily quota (up to 50,000-100,000 a day by donation tier); CC BY-NC-SA from contributors | adapter, the owner's credentials |
| LaunchBox Games DB "Cart - Front" | yes, 400-1200 px | per image, uneven | LaunchBox id, No-Intro-like names | web; terms unread | adapter only if its terms allow |
| ABeezy's N64 Carts pack | rendered fronts, high-res | NTSC (296 + 15 alternates), 7 PAL, no Japan | No-Intro names | the owner's downloaded copy | local-folder adapter |
| n64-flashcart-menu-metadata | `gamepak_front` (size unread) | `E` fallback, others when different | game code path `N/G/E/E` | GitHub | local-checkout adapter; derived from ABeezy's |
| NESWORLD catalogue | scans per region, product codes | worldwide | NUS product code | refused automated reading | identification cross-check by hand, no scraping |
| The owner's own photos | yes | any | named by product code | a folder | local-folder adapter, highest trust |

Not yet read: ScreenScraper's API media names, LaunchBox's terms, EmuMovies' terms, and NESWORLD's
permissions.

## Identity and matching

The key is the cartridge as printed: `NUS-N<ID2><dest>` plus the ROM revision, where:

- `<ID2>` is the header's two-letter game ID at 0x3C-0x3D;
- `<dest>` is the destination letter at 0x3E: E (USA), P (Europe), J (Japan), U (Australia),
  B (Brazil), D, F, I, S, X, Y and so on (the full table is to be confirmed from the n64brew wiki);
- the revision is 0x3F.

Matching priority, the owner's:

1. game code + destination + revision;
2. a product code from the source (`NUS-NXXP-EUR`, `-AUS`, `-NOE`, …) mapped to the same key;
3. the header's CRC1/CRC2, through a verified table;
4. a verified No-Intro name, the file name only as a last resort with its tags (region, revision)
   checked against the header.

A source image is accepted for a key only when its region and revision agree with the header.
Two releases share an image only when the source says they share the physical cartridge, never
by inference. When a key has several variants (shell colours, editions), the default is the
source's first standard release; the others can be chosen on the game page. Ambiguous means no
cartridge image, recorded as such.

Changes this needs in the title:

- store 0x3F;
- read the header from inside `.zip` and `.7z` files (the play cache already extracts them);
- add a `cart` column to `library.tsv` (the key), so lookups are by key, not by file name.

## Manifest

One row per image, in `carts/manifest.tsv` (UTF-8, tab-separated, versioned):

| Field | Example |
| --- | --- |
| key | `NXXP-0` |
| product code | `NUS-NXXP-EUR` |
| region | Europe |
| variant | standard / shell colour / edition text from the source |
| shell colour | grey, black, … as the source states |
| display title | the source's |
| ROM file name, No-Intro name | when known |
| source, source id, source URL | ScreenScraper, 1234, … |
| attribution, licence | as the source states |
| original file, size, sha256 | the kept original |
| width, height | the original's |
| asset file, sha256, width, height | the made frontend asset |
| perceptual hash | 64-bit dHash, for near-duplicates |
| verification | `verified` (key agrees), `probable` (name only, held back), `rejected` (+ why) |

## The pipeline (PC side, `ps5/tools/cartart/`)

A Python tool, resumable from a SQLite state file in its work folder (outside the repository):

1. **Catalogue**: each adapter lists what it has, with keys, product codes, regions and variants.
2. **Fetch** what is permitted: per-source rate limits and robots.txt, a few parallel requests at
   most, conditional requests, and nothing downloaded again once verified.
3. **Identify** region and variant from the source's fields; product codes are parsed, never
   guessed from titles.
4. **Match** against the owner's library: `library.tsv` copied from the console, or a ROM folder
   scanned by the same header code.
5. **Check**:
   - integrity: the PNG or JPEG decodes, its dimensions are sane, its sha256 recorded;
   - a front, not a box: aspect ratio and a shell-shape test;
   - near-duplicates: dHash distance;
   - one image claimed by two keys with different regions is flagged.
6. **Make assets**:
   - the original is kept untouched;
   - the asset is cropped to the cartridge with an alpha background where the source has one,
     with a fixed height (512 px), no upscaling (smaller originals stay smaller and are marked),
     and PNG;
   - WebP only if decoding it on the console is measured and worth it.
7. **Report** (Markdown and HTML): per key what was found, from where and why it was accepted or
   rejected; the library's games with no cartridge; per-region coverage.

The output is a folder copied to `/data/psgopher/carts/` (FTP, or the USB drive the title
already reads).

## On the console

- **Lookup.** `Menu::cover` takes the game's key, looks it up in the manifest, and loads
  `carts/<key>[-<variant>].png`.
- **Drawing.** A cartridge image is drawn whole in place of the procedural shell (strip, slot,
  game panel), straight on, keeping its alpha.
- **Decoding.** A size check is added before decoding (FE-030: no more than 2048 px a side).
- **Texture memory.** A 512 px tall cartridge is about 1 MB with mips, so 48 resident is about
  50 MB.

## Fallback order (the owner's)

1. The exact regional cartridge front (game code + destination + revision, the chosen variant).
2. A verified alternative front of the same physical edition (another revision or source showing
   the same printed cartridge, recorded as the same edition by the source).
3. A generic verified cartridge front for the game (another region's), shown with its region
   marked so it is never taken for the ROM's own.
4. Libretro's box art, as now, drawn as a box (the game panel, a box-shaped card on the strip),
   never on the cartridge shell and never called a cartridge.
5. PSGopher's placeholder: the procedural shell with the title and code printed.

Missing and uncertain images are visible: the game page shows which level was used and why, and
a library filter lists the games below level 1.

## Catalogue and coverage

Coverage is measured against a documented catalogue, not the images found:

- **Releases**: No-Intro's N64 DAT (one entry per dumped release, with region and revision; whether
  it carries serials is to be checked) joined to the header game codes.
- **Physical variants**: what the sources record (ScreenScraper's per-region media, the flashcart
  repository's per-destination folders, product codes); NESWORLD's catalogue is read by hand to
  check product codes, never scraped.
- **The report** gives, per region (NTSC-U, PAL by country, NTSC-J, Australia, Brazil, others):
  the releases in the catalogue, those with a verified front, and those only at levels 2-5.
  "Worldwide" is not claimed until every region's row is checked.

## Validation (automated, in the pipeline and the host tests)

- missing images, against the catalogue;
- regional associations: the image's region agrees with the key;
- duplicates, exact (sha256) and near (dHash);
- invalid files: they decode, have sane dimensions, and are no larger than the limits;
- aspect ratio: a cartridge front, not a box or a label;
- low resolution: below 400 px tall is marked;
- broken mappings: keys with no catalogue entry, entries with no key;
- the fallback chosen, for sample games with several regional releases and different shell
  colours (synthetic images in the tests; real ones in the report).

## Feasibility

- **NTSC-U**: likely high through ScreenScraper and the flashcart repository (both draw on
  well-covered US releases).
- **PAL and NTSC-J**: thin in every source read; expect gaps to be filled by the owner's photos.
- **Australia, Brazil and variants**: sparse; the report will show how sparse.
- **ScreenScraper**: needs the owner's account. Software use needs a developer id, which its team
  grants on request (the owner applies, or allows the lead to draft the request). The daily quota
  of a free account is enough for one library a day.

## Roadmap and agents

Each phase is verified by the lead (compile, host tests, the pipeline's own tests) before the next.

1. **Identity in the title**: store 0x3F; read headers inside `.zip` and `.7z`; the `cart`
   key column in `library.tsv` (version 4, the old file read); the destination table; host tests.
2. **Catalogue and schema**: the No-Intro DAT reader, the release catalogue (key, region,
   revision, product code), and the manifest's schema and round trip.
3. **The pipeline**: adapters for the flashcart repository (a local checkout), the owner's
   photos (a folder), and ScreenScraper (the owner's credentials, rate-limited, resumable); the
   checks, the assets (Pillow on the PC only; nothing new on the console) and the report.
   A stronger agent only if the matching rules prove hard.
4. **The console side**: the manifest reader, the five-level fallback, cartridge images
   drawn in the strip, the slot and the game panel; box art moved to a box-shaped card; the FE-030
   size check; the missing-art filter; interface pictures.
5. **The first real run** (the lead): the owner's library through the pipeline; coverage report;
   console test plan.

Deliverables, where each will be:

| Deliverable | Where |
| --- | --- |
| Research report and licensing/attribution report | `docs/dev/cartridge-art-sources.md` |
| Architecture | this document |
| Regional metadata database | the catalogue and manifest made by the pipeline (on the owner's PC) |
| Acquisition and processing scripts | `ps5/tools/cartart/` |
| The artwork library | the pipeline's output folder (not in the repository: not redistributable) |
| Frontend integration | `ps5/host/` |
| Missing artwork and regional coverage report | the pipeline's report |
| Testing and validation report | the pipeline's tests and host tests, summarised in `testing.md` |

## Acceptance

- Every image shown as a cartridge has `verified` status, with its key equal to the ROM's game
  code, destination and revision.
- No box art is drawn on or as a cartridge.
- Games with no image show the fallback, never another region's cartridge.
- A second run downloads nothing already verified.
- The report lists every library game.
- Host tests cover matching (region, revision, variants, ambiguity), the manifest round trip and
  the fallback order.
