# Front end architecture (audit view)

Part of the front end audit of 2026-11-25. Index: [MASTER_PLAN.md](MASTER_PLAN.md). Defects are in
[BUGS.md](BUGS.md), proposals in [IMPROVEMENTS.md](IMPROVEMENTS.md), what was and was not looked at
in [COVERAGE.md](COVERAGE.md).

All paths are relative to the repository root. Line numbers are those of the tree the audit read;
they move with every edit. Nothing here was checked on a console.

The front end is C++ Dear ImGui, immediate mode: every frame redraws everything. The questions a
retained-mode audit asks (re-renders, effects, stale closures) become, here: work done every frame,
cached state and when it is invalidated, what `std::function` callbacks capture, which threads touch
shared state, which objects outlive a frame or a screen, and how long textures live.

## 1. Entry points and the menu <-> game loop

`ps5/host/main.cpp:207` `main()`: leaves the sandbox, opens the log, makes the folders, loads
`TitleSettings` and applies the display mode. Then:

- `probe.txt` / `benchmark.txt` modes: no list at all.
- Otherwise: `startShares` -> `webStart` -> `library().load()` + `rescan()` -> an optional
  `autostart.txt` ROM -> the loop
  `runMenu(notice)` -> `settings.load` / `applyHdr` -> `play(rom, load_state)` (= `psgopher_main`,
  Gopher64) -> `addPlayed` (more than 5 s).

**The menu and the game never coexist.** `runMenu` (`ps5/host/menu.cpp:1180`) creates its own Vulkan
instance, device and swapchain, an ImGui context, the font atlas, uploads `board.png` (4000x2500
with a mip chain) and `logo.png`, builds a fresh `Menu` and a fresh `Frontend`, and tears all of it
down (`closeDisplay`) before Gopher64 makes its own device. Everything owned by `Menu` or `Frontend`
therefore lives for one visit to the menu; everything in a function static or a global lives for the
process. That split is behind several defects (FE-002, FE-010, FE-036 in [BUGS.md](BUGS.md)).

## 2. The menu frame (main thread)

`ps5/host/menu.cpp:1337-1365`, once per presented frame (FIFO):

1. `pad_poll`, then the release gate (input starts only once every button is up).
2. Two input models are filled from the same sample:
   - `Buttons b`: edges from the pad layer, `double` times; used by the ImGui pages and the keyboard.
   - `PadInput menu.input`: derived edges plus `held_since` / `last_repeat` in **float** seconds,
     L2/R2 past half as digital bits; used by the Live Board.
3. `feedImGui`: D-pad, Cross and the left stick as ImGui navigation.
4. `frame()` (`menu.cpp:1054`): fence wait, acquire, `ImGui::NewFrame`, `Menu::draw`, render pass,
   present.

`Menu::draw` (`menu.cpp:1015`): consume a finished share copy, then **always** `Frontend::frame` on
the background draw list (with a quiet pad while a shares page or the keyboard is up), then the
shares pages as an ImGui window, then the keyboard window.

## 3. The Live Board (`ps5/host/frontend.cpp`, `class Frontend`)

Everything is laid out in 1920x1080 stage units scaled by `u_ = h / 1080`.

`Frontend::frame` (`frontend.cpp:455`), every frame:

1. `refresh()`: copies the Library's list, and rebuilds `strip_` / `rack_` / `played_` when the
   library version changed.
2. L1/R1 view switch, pose blend.
3. Machine settings read through `host_.get`, `machine_.step`, the stage light.
4. `board::drawBoard`.
5. `home()` / `rack()` / `bench()`, then `header()`, then the notice pill.

| State | Owner | Lifetime |
| --- | --- | --- |
| `view_`, `from_`, `blend_` | `Frontend` | one menu visit |
| `games_` (a copy), `version_`, `played_` (by index), `strip_`, `rack_` (index vectors) | `Frontend` | one menu visit; rebuilt on a version change |
| `strip_sel_`, `rack_sel_`, `bench_sel_`, `sort_`, `show_` | `Frontend` | one menu visit |
| `probes_open_`, `cheats_open_` (picker cursors), `probes_`, `probe_files_`, `probes_for_`, `cheats_` | `Frontend` | one menu visit; not tied to a game |
| `machine_` (seat, boot wave, a copy of the seated `Look` with its cover texture id), `projector_` | `Frontend` | one menu visit |
| fingerprint cache (`frontend.cpp:326`), `with_probes` / `probes_version` (`frontend.cpp:987-988`) | function statics | the process |

### The host interface

Everything the board asks of the title goes through `FrontendHost`, a set of `std::function`
callbacks built in `runMenu` (`menu.cpp:1250-1328`) that capture `Menu*` and `Display*`:

`get` / `set` (`Menu::cache` over `psgopher_setting_get` / `_set`), `title` (`TitleSettings*`),
`play`, `shares`, `close`, `tell` / `notice`, `elevated` / `elevation_off` / `set_elevation_off`,
`share_count`, `busy`, `cheats_of` / `cheat_set`, `cover`, `about`.

`cover` is the one with hidden work: it looks the name up in `Menu::covers` (an LRU map, `kCovers`
= 48), otherwise takes a decoded picture from the web cache, uploads it (at most one upload per
frame, each waiting for the queue) and may evict another entry (waiting for the device).
`Frontend::lookFor` calls it for every caller, including callers that draw only a colour.

## 4. The board (`ps5/host/board.cpp`)

A pose per view (`poseOf`), a CSS-like projection (`Projector`), a textured 40x25 mesh, and buses,
dashes, meters and lights drawn in board pixels and then transformed in place (`transformFrom` over
the draw list's vertices). The cartridge carries a procedural label (clipped polygons) or a cover
(6x6 image quads). `Machine::step` owns the seat and boot animation and keeps a copy of the seated
`Look`, cover texture id included.

## 5. Threads

| # | Thread | Where | Notes |
| --- | --- | --- | --- |
| 1 | Main / UI | `menu.cpp` loop | draws; also mounts and unmounts shares |
| 2 | Library scan | `library.cpp:429` | detached, one at a time; replaces `state().games` under a mutex and bumps the version |
| 3 | Web | `web.cpp:187-203` | detached, never ends; LIFO queue capped at 64; blocking HTTPS through the core (`psgopher_cover`, `psgopher_about`); decodes PNGs; disk cache with `.none` markers for 30 days |
| 4 | Share copy | `menu.cpp:870` | detached; the global `g_copy` outlives the `Menu` |
| 5 | SMB server search | `menu.cpp:946` | detached; globals `found` / `finding` |
| 6 | Emulation (in a game) | `interface.cpp:1228` | runs the quick menu loop and the stats OSD inside parallel-rdp's present path |
| 7 | Bench logger | `bench.c:214` | benchmark mode only |

Threads 2 to 5 keep running while a game runs.

## 6. The in-game overlay

`vendor/gopher64/parallel-rdp/interface.cpp`, PSGOPHER blocks (exported to
`ps5/patches/gopher64/ps5.patch`):

- `rdp_update_screen` (1228) presents, reports to `bench.c`, then `psgopher_quickmenu_check` (1041)
  -> `psgopher_quickmenu_wanted` (a touch pad tap seen by `ps5/sdl3/psgopher_sdl3.cpp:420-506`) ->
  `psgopher_quickmenu_run` (`ps5/host/quickmenu.cpp:357`).
- The quick menu makes its own ImGui context per opening and runs its own loop **on the emulation
  thread**; the game is paused because that thread is blocked. Each frame is flattened to vertices,
  indices and commands and handed to `psgopher_overlay_present` (1024), which runs `render_frame`
  again (scanout of the last frame plus the overlay triangles, HDR layer path included) and presents.
- The answer {resume, save slot, load slot, reset, quit} becomes Gopher64 callback flags (1045-1062)
  consumed by `ui/video.rs` `check_callback`.
- The stats OSD: `render_frame` calls `psgopher_osd_frame` (`quickmenu.cpp:494`) every frame when it
  is on. A persistent ImGui context builds triangles handed over by `psgopher_overlay_set`; the
  figures come from `bench.c` (`live_now`, history), written in `psgopher_bench_present` on the same
  thread.
- Textures (font atlases) go through `psgopher_overlay_texture` by small integer ids;
  `psgopher_overlay_release` at `rdp_close` also destroys the OSD context.

Correction to the plan made during the audit: pad polling does **not** stop while the quick menu is
up. `WSI::begin_frame` calls `poll_input`, then `SDL_PumpEvents`, on every menu present, so
`poll_pads` and its tap detection keep running (this is what makes FE-004 repeat).

## 7. Data

| Data | Where it lives | Access |
| --- | --- | --- |
| Library | `library.tsv` (cache, v3), `played.tsv` (by file name) | `library.cpp` |
| Probes (save states) | `data/states/<saveName>-<hash>.state<slot>` | four separate implementations: `probesOf`, the panel's file lookup, the RACK filter's set, the quick menu's `stat` |
| Gopher64 settings | `config.json` | the Rust bridge (`psgopher.rs`); each get constructs and drops a `Config` |
| Title settings | `psgopher.ini` | `TitleSettings` instances in `main`, `Menu`, the quick menu, the OSD |
| Shares | `network.cfg` | `g_shares` and `Menu::shares` |
| Cheats | the core's database plus `config/cheats.json` | `psgopher_cheats_of` / `psgopher_cheat_set` |
| Covers, descriptions | libretro thumbnails (raw.githubusercontent.com), Wikipedia REST; `/data/psgopher/cache/covers` | `web.cpp` through the core's `https_get` |

## 8. Assets

`ps5/art/board.png` and `logo.png` (stb; kept decoded for the life of the process in `image.cpp`),
four embedded faces (JetBrains Mono regular / semibold / extrabold, Bricolage Grotesque) from
generated headers. The design reference is `ps5/art/live-board.html`.

## 9. Tests (host, ASan + UBSan)

| Test | What it does | What it does not do |
| --- | --- | --- |
| `ps5/tests/test_menu.cpp` | drives `runMenu` offscreen on lavapipe with a scripted pad and stand-ins for the core; saves pictures | 1080p only, 15 short synthetic names, one cover; no cheat picker, OSD, 4K, empty or scanning library, many covers, long titles, share copy, network failure |
| `ps5/tests/test_quickmenu.cpp` | runs `psgopher_quickmenu_run` against a software rasterizer; checks the answers | the Stats row and `psgopher_osd_frame` |
| `ps5/tests/test_library.cpp` | scanning, settings and shares files | not read by the audit |

Lavapipe does not enforce descriptor pool sizes, so the host tests cannot show FE-001 without added
accounting. The pictures folder holds stale pictures of an earlier interface next to the current
ones (FE-038); the current set is listed in [COVERAGE.md](COVERAGE.md).

## 10. Structural observations

These are the patterns the defects come from; each links to where it is handled.

1. **One texture id, three holders, one decider.** A cover is known to the web entry, to
   `Menu::covers` and to `Machine::shown`; only the per-name frame stamp in `Menu::covers` decides
   eviction, and destruction happens inside the frame (FE-001, FE-002, FE-003, FE-033; IMP-01, IMP-02).
2. **A query with side effects.** `lookFor` is `const` and starts fetches, uploads and evictions
   (FE-003; IMP-01).
3. **Index-based selection against a list replaced asynchronously.** `refresh()` re-anchors only the
   strip selection by path; the rack selection and both pickers keep indices and cached lists
   (FE-006, FE-007, FE-012, FE-018).
4. **Result state written before a decision.** `host.play` writes `load_state` before `Menu::play`
   accepts (FE-008; IMP-08).
5. **Process-lifetime caches keyed on the wrong thing.** The Has probes set is keyed on the library
   version, the fingerprint cache on the path only (FE-010, FE-036).
6. **No single probe identity.** Four rules for which state files belong to a cartridge, and two
   slot ranges (FE-005, FE-009; IMP-06).
7. **Work every frame that depends on library size or text length.** The list copy, the RACK
   histogram and count, the description wrap, the truncation loops (FE-025, FE-027, FE-040, FE-051).
8. **Blocking the emulation thread for the quick menu.** Rumble, audio underrun counters, live
   statistics and play time each see the pause differently (FE-022, FE-058, FE-059; IMP-24).
9. **Shared tap state across players** (FE-004).
10. **Detached threads and static-duration objects with no shutdown protocol** (FE-035, FE-060; IMP-23).

## 11. Inventory

243 items. Kinds: entry 8, view 9, panel 40, picker/dialog 3, control 85, input 9, thread 6,
resource 13, model 14, overlay-api 8, state 34, feedback 10, test 4. Which of them were inspected is
in [COVERAGE.md](COVERAGE.md).

### Entry points

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| E-MAIN | Title main: menu<->game loop, play time | `ps5/host/main.cpp:207` | runMenu -> settings.load/applyHdr -> play -> addPlayed (>5 s) |
| E-AUTOSTART | autostart.txt / benchmark.txt / probe.txt modes | `ps5/host/main.cpp:254` | bypass the list; scan thread already started for autostart |
| E-RUNMENU | runMenu: display, ImGui, fonts, art, FrontendHost, loop, teardown | `ps5/host/menu.cpp:1180` | everything rebuilt per return from a game |
| E-MENU-FRAME | Vulkan frame: fence, acquire, render pass, present | `ps5/host/menu.cpp:1054` | host variant reads back a PPM |
| E-FRONTEND-FRAME | Frontend::frame (refresh, views, board, header, notice) | `ps5/host/frontend.cpp:455` | |
| E-QM-CHECK | psgopher_quickmenu_check after each present | `vendor/gopher64/parallel-rdp/interface.cpp:1041` | emulation thread |
| E-QM-RUN | psgopher_quickmenu_run loop | `ps5/host/quickmenu.cpp:357` | own ImGui context per opening |
| E-OSD-FRAME | psgopher_osd_frame (per rendered frame) | `ps5/host/quickmenu.cpp:494` | called from render_frame, interface.cpp:1204 |

### Views

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| V-HOME | HOME: board, panel, strip | `ps5/host/frontend.cpp:722` | picture 1-home, 2-home-moved |
| V-RACK | RACK: shelves of spines, index, sort/show, panel | `ps5/host/frontend.cpp:977` | picture 3-rack, 4-rack-sorted-filtered |
| V-BENCH | BENCH: callouts as settings, This console | `ps5/host/frontend.cpp:1253` | picture 5..7-bench |
| V-SHARES | Network shares list (ImGui window over the board) | `ps5/host/menu.cpp:888` | picture 8-shares |
| V-SHARE-EDIT | Share form (new/edit) | `ps5/host/menu.cpp:922` | picture 9, 12 |
| V-QM-MAIN | Quick menu main page | `ps5/host/quickmenu.cpp:188` | picture quickmenu/1 |
| V-QM-TAKE | Quick menu: take a probe (slots 1-5) | `ps5/host/quickmenu.cpp:198` | picture quickmenu/2 |
| V-QM-LOAD | Quick menu: load a probe | `ps5/host/quickmenu.cpp:198` | picture quickmenu/3 |
| V-OSD | Stats overlay (OSD) over the game | `ps5/host/quickmenu.cpp:441` | no host picture exists |

### Panels

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| P-HEADER | Header: logo, name, status line | `ps5/host/frontend.cpp:518` | |
| P-VIEW-TABS | HOME/RACK/BENCH pill | `ps5/host/frontend.cpp:540` | design has a LIVE tab too |
| P-CLOCK | Clock (day HH:MM) | `ps5/host/frontend.cpp:559` | localtime each frame |
| P-STAGE-LIGHT | Stage light (28 ellipses in the game colour) | `ps5/host/frontend.cpp:486` | |
| P-BOARD | Mainboard: mesh, buses, meters, lights, expansion, crystal | `ps5/host/board.cpp:540` | vertices transformed in place |
| P-BOARD-CARTRIDGE | Cartridge sliding into the slot with label/cover | `ps5/host/board.cpp:631` | uses Machine::shown (kept Look) |
| P-PANEL-HOME | Game panel in HOME | `ps5/host/frontend.cpp:574` | height grows with content |
| P-PANEL-RACK | Game panel in RACK (large label on top) | `ps5/host/frontend.cpp:584` | same code, rack_panel=true |
| P-PANEL-TITLE | Panel eyebrow + wrapped title + played line | `ps5/host/frontend.cpp:592` | wrap max 3 lines |
| P-PANEL-ABOUT | Wikipedia description (3 lines) + credit | `ps5/host/frontend.cpp:608` | untrusted text |
| P-PANEL-FACTS | Facts grid 2x3 with region flag | `ps5/host/frontend.cpp:622` | |
| P-PANEL-PROBES | Probes list with fingerprints (max 3) | `ps5/host/frontend.cpp:660` | opendir when selection changes; fingerprint 1176 rects each |
| P-PANEL-ACTIONS | Action pills (Power on / Probes / Cheats or Seat it) | `ps5/host/frontend.cpp:696` | labels only, not selectable |
| P-STRIP | Cartridge strip (sel-7..sel+8) | `ps5/host/frontend.cpp:780` | eased scroll, names truncated |
| P-SLOT-LINE | Dashed line from strip to slot | `ps5/host/frontend.cpp:760` | |
| P-LABEL | Cartridge label: procedural art or cover | `ps5/host/board.cpp:284` | clip() allocates per polygon |
| P-FLAG | Region flags drawn with primitives | `ps5/host/frontend.cpp:201` | 12 regions; Asia has none |
| P-RACK-SHELVES | Shelves, spines, selection glow, rotated code | `ps5/host/frontend.cpp:1091` | 52 columns, clip rect |
| P-RACK-LETTERS | Letter markers at first cartridge of a letter | `ps5/host/frontend.cpp:1153` | A-Z sort only |
| P-RACK-INDEX | Letter index histogram A-Z | `ps5/host/frontend.cpp:1174` | no column for '#' |
| P-RACK-BAR | Sort chips, show chip, cartridge count | `ps5/host/frontend.cpp:1194` | |
| P-CALLOUT-CRYSTAL | Callout Crystal (overclock) | `ps5/host/frontend.cpp:1278` | |
| P-CALLOUT-VIDEO | Callout Video stage (upscale, SSAA) | `ps5/host/frontend.cpp:1280` | anchor rdp |
| P-CALLOUT-AV | Callout AV out (CRT, widescreen, integer) | `ps5/host/frontend.cpp:1283` | |
| P-CALLOUT-TV | Callout TV link (refresh, HDR, nits) | `ps5/host/frontend.cpp:1287` | same anchor as AV out |
| P-CALLOUT-EXPANSION | Callout Expansion slot | `ps5/host/frontend.cpp:1301` | |
| P-CALLOUT-PORTS | Callout Ports (4 ports + VRU) | `ps5/host/frontend.cpp:1304` | |
| P-CALLOUT-SLOT | Callout Cartridge slot (rewind, USB link) | `ps5/host/frontend.cpp:1315` | |
| P-BENCH-CONSOLE | This console card (4 actions) | `ps5/host/frontend.cpp:1428` | bottom at about y=1076 of 1080 |
| P-HINTS-HOME | Hints bar HOME | `ps5/host/frontend.cpp:815` | |
| P-HINTS-RACK | Hints bar RACK | `ps5/host/frontend.cpp:1219` | |
| P-HINTS-BENCH | Hints bar BENCH | `ps5/host/frontend.cpp:1446` | |
| P-HINTS-PROBES | Hints bar probe picker | `ps5/host/frontend.cpp:855` | Square also closes |
| P-HINTS-CHEATS | Hints bar cheat picker | `ps5/host/frontend.cpp:971` | Triangle also closes |
| P-SHARES-WINDOW | ImGui window + style for shares pages | `ps5/host/menu.cpp:1036` | 980x760 stage units, style() at 601 |
| P-QM-PANEL | Quick menu card: eyebrow, title, rows | `ps5/host/quickmenu.cpp:247` | |
| P-QM-LIVE-STRIP | Quick menu live tiles (fps, CPU, present, audio) | `ps5/host/quickmenu.cpp:271` | |
| P-QM-HINTS | Quick menu key legend | `ps5/host/quickmenu.cpp:296` | |
| P-QM-DIM | Game dimming gradient | `ps5/host/quickmenu.cpp:177` | alpha over game; HDR layer path |
| P-OSD-GRAPH | OSD frame-time bars + target line | `ps5/host/quickmenu.cpp:477` | 180 bars |

### Pickers and dialogs

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| D-PROBES | Probe picker (power on from a probe) | `ps5/host/frontend.cpp:819` | picture 13-home-probes |
| D-CHEATS | Cheat picker (11 rows, note) | `ps5/host/frontend.cpp:903` | no host picture; readCheats at 859 |
| D-KEYBOARD | On-screen keyboard | `ps5/host/menu.cpp:674` | pictures 10, 11 |

### Controls

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| C-GLOBAL-L1 | L1: previous view | `ps5/host/frontend.cpp:472` | blocked only while probe picker is open |
| C-GLOBAL-R1 | R1: next view | `ps5/host/frontend.cpp:470` | |
| C-HOME-LEFT | HOME Left (repeat): previous cartridge | `ps5/host/frontend.cpp:738` | |
| C-HOME-RIGHT | HOME Right (repeat): next cartridge | `ps5/host/frontend.cpp:736` | |
| C-HOME-CROSS | HOME Cross: power on | `ps5/host/frontend.cpp:742` | silently ignored while a copy runs |
| C-HOME-SQUARE | HOME Square: open probe picker / notice | `ps5/host/frontend.cpp:744` | |
| C-HOME-TRIANGLE | HOME Triangle: open cheat picker / notice | `ps5/host/frontend.cpp:752` | |
| C-PROBES-UPDOWN | Probe picker Up/Down (repeat, wraps) | `ps5/host/frontend.cpp:823` | |
| C-PROBES-CROSS | Probe picker Cross: play from slot | `ps5/host/frontend.cpp:831` | no busy check |
| C-PROBES-CIRCLE | Probe picker Circle: close | `ps5/host/frontend.cpp:827` | |
| C-PROBES-SQUARE | Probe picker Square: close (undocumented) | `ps5/host/frontend.cpp:827` | |
| C-CHEATS-UPDOWN | Cheat picker Up/Down (repeat, wraps) | `ps5/host/frontend.cpp:907` | |
| C-CHEATS-CROSS | Cheat picker Cross: on/off (saved at once) | `ps5/host/frontend.cpp:920` | |
| C-CHEATS-LEFTRIGHT | Cheat picker Left/Right: option | `ps5/host/frontend.cpp:926` | only when on |
| C-CHEATS-CIRCLE | Cheat picker Circle: close | `ps5/host/frontend.cpp:911` | |
| C-CHEATS-TRIANGLE | Cheat picker Triangle: close (undocumented) | `ps5/host/frontend.cpp:911` | |
| C-RACK-LEFTRIGHT | RACK Left/Right (repeat, skips hidden) | `ps5/host/frontend.cpp:1024` | |
| C-RACK-UPDOWN | RACK Up/Down (repeat): shelf | `ps5/host/frontend.cpp:1028` | direction from held Down |
| C-RACK-L2R2 | RACK L2/R2: letter jump (A-Z sort only) | `ps5/host/frontend.cpp:1044` | |
| C-RACK-SQUARE | RACK Square: cycle sort | `ps5/host/frontend.cpp:1070` | panel pill says Probes |
| C-RACK-OPTIONS | RACK Options: cycle show filter (7) | `ps5/host/frontend.cpp:1078` | |
| C-RACK-CROSS | RACK Cross: power on | `ps5/host/frontend.cpp:1081` | acts on rack_sel_ even if filtered out |
| C-RACK-TRIANGLE | RACK Triangle: seat it (to HOME) | `ps5/host/frontend.cpp:1083` | |
| C-BENCH-UPDOWN | BENCH Up/Down (repeat, wraps over 22 items) | `ps5/host/frontend.cpp:1344` | |
| C-BENCH-LEFTRIGHT | BENCH Left/Right: change value (repeat, wraps) | `ps5/host/frontend.cpp:1355` | |
| C-BENCH-CROSS | BENCH Cross: next option / run action | `ps5/host/frontend.cpp:1365` | |
| C-BENCH-CIRCLE | BENCH Circle: to HOME | `ps5/host/frontend.cpp:1348` | |
| C-BENCH-OVERCLOCK | Row emulation.overclock | `ps5/host/frontend.cpp:1279` | |
| C-BENCH-UPSCALE | Row video.upscale 1/2/4/8 | `ps5/host/frontend.cpp:1281` | |
| C-BENCH-SSAA | Row video.ssaa | `ps5/host/frontend.cpp:1282` | |
| C-BENCH-CRT | Row video.crt | `ps5/host/frontend.cpp:1284` | |
| C-BENCH-WIDESCREEN | Row video.widescreen | `ps5/host/frontend.cpp:1285` | |
| C-BENCH-INTEGER | Row video.integer_scaling | `ps5/host/frontend.cpp:1286` | |
| C-BENCH-REFRESH | Row display 59.94/119.88/VRR (psgopher.ini) | `ps5/host/frontend.cpp:1288` | from next start |
| C-BENCH-HDR | Row SDR/HDR10 | `ps5/host/frontend.cpp:1291` | |
| C-BENCH-HDR-WHITE | Stepper HDR white nits 80..400 step 10 | `ps5/host/frontend.cpp:1294` | saves ini each step |
| C-BENCH-HDR-PEAK | Stepper HDR peak nits 0..4000 step 100 | `ps5/host/frontend.cpp:1296` | |
| C-BENCH-EXPANSION | Row Jumper Pak / Expansion Pak | `ps5/host/frontend.cpp:1302` | |
| C-BENCH-PORTS | Rows input.controller_enabled.0..3 | `ps5/host/frontend.cpp:1305` | four rows |
| C-BENCH-VRU | Row input.emulate_vru | `ps5/host/frontend.cpp:1312` | |
| C-BENCH-REWIND | Row emulation.rewind | `ps5/host/frontend.cpp:1316` | |
| C-BENCH-USB | Row emulation.usb | `ps5/host/frontend.cpp:1316` | |
| C-BENCH-SHARES | Action Network shares | `ps5/host/frontend.cpp:1325` | opens V-SHARES in the same frame |
| C-BENCH-SANDBOX | Action Leave the sandbox (elevation-off.txt) | `ps5/host/frontend.cpp:1327` | menu.cpp:1268 |
| C-BENCH-RESCAN | Action Search for cartridges | `ps5/host/frontend.cpp:1329` | ignored while a scan runs |
| C-BENCH-CLOSE | Action Close PSGopher (no confirmation) | `ps5/host/frontend.cpp:1330` | |
| C-SHARES-NAV | Shares: ImGui nav (D-pad / left stick) | `ps5/host/menu.cpp:574` | |
| C-SHARES-ADD | Shares: Add a share | `ps5/host/menu.cpp:899` | |
| C-SHARES-ROW | Shares: edit a share row | `ps5/host/menu.cpp:905` | |
| C-SHARES-CIRCLE | Shares: Circle back to board | `ps5/host/menu.cpp:890` | |
| C-EDIT-KIND | Form: Kind SMB/NFS (Cross, Left/Right) | `ps5/host/menu.cpp:934` | |
| C-EDIT-HOST | Form: Server (keyboard) | `ps5/host/menu.cpp:941` | |
| C-EDIT-FIND | Form: Find SMB servers (thread) | `ps5/host/menu.cpp:943` | |
| C-EDIT-FOUND-USE | Form: use a found server | `ps5/host/menu.cpp:959` | |
| C-EDIT-PATH | Form: Share/export and folder | `ps5/host/menu.cpp:965` | |
| C-EDIT-USER | Form: User | `ps5/host/menu.cpp:968` | |
| C-EDIT-PASSWORD | Form: Password (masked) | `ps5/host/menu.cpp:969` | |
| C-EDIT-NAME | Form: Name | `ps5/host/menu.cpp:971` | |
| C-EDIT-SAVE | Form: Add/Save and mount (blocking mount) | `ps5/host/menu.cpp:973` | mountShare on the UI thread |
| C-EDIT-REMOVE | Form: Remove this share (no confirmation) | `ps5/host/menu.cpp:1001` | |
| C-EDIT-CIRCLE | Form: Circle back without saving | `ps5/host/menu.cpp:926` | |
| C-KB-NAV | Keyboard: nav between keys | `ps5/host/menu.cpp:724` | initial focus on q |
| C-KB-KEY | Keyboard: Cross types a key | `ps5/host/menu.cpp:735` | no length limit |
| C-KB-SHIFT | Keyboard: Shift button | `ps5/host/menu.cpp:740` | |
| C-KB-SPACE | Keyboard: Space button | `ps5/host/menu.cpp:743` | |
| C-KB-DELETE | Keyboard: Delete button | `ps5/host/menu.cpp:746` | byte-wise pop_back |
| C-KB-DONE | Keyboard: Done button | `ps5/host/menu.cpp:749` | |
| C-KB-SQUARE | Keyboard: Square deletes | `ps5/host/menu.cpp:697` | no repeat |
| C-KB-OPTIONS | Keyboard: Options is done | `ps5/host/menu.cpp:703` | |
| C-KB-CIRCLE | Keyboard: Circle cancels | `ps5/host/menu.cpp:699` | |
| C-QM-TAP-OPEN | Touch pad tap opens the quick menu | `ps5/host/quickmenu.cpp:352` | tap <500 ms, no other button |
| C-QM-TAP-RESUME | Tap again resumes (any page) | `ps5/host/quickmenu.cpp:402` | |
| C-QM-UPDOWN | Quick menu Up/Down (pressed only, wraps) | `ps5/host/quickmenu.cpp:212` | |
| C-QM-CROSS | Quick menu Cross: choose (disabled rows ignored) | `ps5/host/quickmenu.cpp:223` | |
| C-QM-CIRCLE | Quick menu Circle: back / resume | `ps5/host/quickmenu.cpp:216` | |
| C-QM-RESUME | Item Resume | `ps5/host/quickmenu.cpp:190` | |
| C-QM-TAKE | Item Take a memory probe | `ps5/host/quickmenu.cpp:191` | |
| C-QM-LOAD | Item Load a probe (disabled when none) | `ps5/host/quickmenu.cpp:194` | |
| C-QM-STATS | Item Stats overlay on/off (writes psgopher.ini) | `ps5/host/quickmenu.cpp:237` | not covered by the host test |
| C-QM-RESET | Item Press reset (no confirmation) | `ps5/host/quickmenu.cpp:196` | |
| C-QM-EJECT | Item Eject cartridge (no confirmation) | `ps5/host/quickmenu.cpp:197` | Up from Resume lands here |
| C-QM-SLOT-SAVE | Take page: Probe 1-5 (replace) | `ps5/host/quickmenu.cpp:200` | |
| C-QM-SLOT-LOAD | Load page: Probe 1-5 (kept only) | `ps5/host/quickmenu.cpp:200` | cursor starts on newest |
| C-QM-BACK | Sub-page Back row | `ps5/host/quickmenu.cpp:206` | |
| C-GAME-HOTKEYS | Gopher64 hotkeys (touch pad held + Options/L1/R1/L2) | `ps5/sdl3/psgopher_sdl3.cpp:420` | share the save slot with the quick menu |

### Input

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| I-PADINPUT | PadInput edges + repeat (0.38 s, 0.09 s), float clock | `ps5/host/frontend.cpp:351` | filled at menu.cpp:1344-1357 |
| I-BUTTONS | Buttons (pad layer edges) for ImGui screens | `ps5/host/menu.cpp:553` | Buttons::repeat appears unused |
| I-IMGUI-NAV | feedImGui: D-pad, Cross, left stick | `ps5/host/menu.cpp:574` | |
| I-RELEASE-GATE-MENU | Menu input starts when all buttons are up | `ps5/host/menu.cpp:1333` | |
| I-RELEASE-GATE-QM | Quick menu release gate | `ps5/host/quickmenu.cpp:391` | |
| I-HOLD-UNTIL-RELEASED | Game sees pads at rest after the menu | `ps5/sdl3/psgopher_sdl3.cpp:448` | |
| I-L2R2-DIGITAL | L2/R2 past half as buttons | `ps5/host/menu.cpp:1347` | |
| I-PAD-POLL | pad_poll / pad_fill (disconnect, shell intercept) | `ps5/host/platform.c:343` | last sample kept when disconnected |
| I-TAP-DETECT | watch_tap / take_tap / menu_pad | `ps5/sdl3/psgopher_sdl3.cpp:424` | any player taps, menu reads player 1 |

### Threads

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| T-LIBRARY-SCAN | Library scan thread | `ps5/host/library.cpp:429` | detached; runs during games |
| T-COPY | Share ROM copy thread | `ps5/host/menu.cpp:870` | detached; global g_copy; no cancel/progress |
| T-WEB | Cover/description fetch thread | `ps5/host/web.cpp:187` | detached forever; queue cap 64 |
| T-FIND-SERVERS | SMB server search thread | `ps5/host/menu.cpp:946` | detached; about 2 s |
| T-EMU-OVERLAY | Emulation thread running quick menu and OSD | `vendor/gopher64/parallel-rdp/interface.cpp:1228` | blocks emulation while the menu is up |
| T-BENCH-LOGGER | Bench logger thread (benchmark mode) | `ps5/host/bench.c:214` | |

### Resources

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| R-TEX-BOARD | board.png texture (4000x2500 + mips) | `ps5/host/menu.cpp:1246` | re-uploaded each runMenu |
| R-TEX-LOGO | logo.png texture | `ps5/host/menu.cpp:1247` | |
| R-TEX-COVERS | Cover texture LRU (kCovers 48) | `ps5/host/menu.cpp:1297` | one upload per frame; eviction waits device idle |
| R-DESCRIPTOR-POOL | ImGui Vulkan descriptor pool (16 sampled images) | `ps5/host/menu.cpp:1228` | smaller than kCovers |
| R-FONTS | Font faces and atlases (menu, quick menu, OSD) | `ps5/host/menu.cpp:1215` | quickmenu.cpp:386, 513 |
| R-DISPLAY | Vulkan display objects (open/close) | `ps5/host/menu.cpp:402` | closeDisplay at 494; unchecked results in makeTargets/uploadTexture |
| R-IMAGE-CACHE | loadImage cache (decoded art kept) | `ps5/host/image.cpp:33` | 40 MB for the board |
| R-FINGERPRINT-CACHE | Probe fingerprint static cache | `ps5/host/frontend.cpp:326` | never invalidated |
| R-WEB-DISK-CACHE | cache/covers: .png, .none, about-*.txt | `ps5/host/web.cpp:103` | trusted on read |
| R-WEB-ENTRIES | Web entries map (states, decoded pictures, texts) | `ps5/host/web.cpp:32` | never pruned |
| R-OVERLAY-TEXTURES | Overlay textures by id in the interface | `vendor/gopher64/parallel-rdp/interface.cpp:921` | ids from a static counter, limit 4096 |
| R-OSD-CONTEXT | Persistent OSD ImGui context | `ps5/host/quickmenu.cpp:435` | destroyed in psgopher_osd_release |
| R-PLAY-CACHE | cache/play local copy of a share ROM | `ps5/host/library.cpp:658` | whole file in memory, cap 256 MiB |

### Models

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| M-LIBRARY | Library state (games, version, scanning, status) | `ps5/host/library.cpp:251` | games() copies under mutex |
| M-LIBRARY-TSV | library.tsv cache load/save | `ps5/host/library.cpp:373` | line buffer 2048 |
| M-PLAYED | played.tsv (by file name) | `ps5/host/library.cpp:588` | |
| M-PROBES | Probes: probesOf, panel file lookup, rack set, quick menu stat | `ps5/host/library.cpp:565` | four implementations |
| M-FRONTEND-STATE | Frontend state (selection, sort, filter, pickers) | `ps5/host/frontend.cpp:379` | refresh() and order() |
| M-MACHINE | board::Machine (seat, boot wave, loads) | `ps5/host/board.cpp:476` | keeps a Look with a texture id |
| M-SETTINGS-G64 | Gopher64 settings bridge + Menu::cache | `ps5/host/menu.cpp:825` | psgopher.rs:84-117 |
| M-SETTINGS-TITLE | TitleSettings (psgopher.ini) | `ps5/host/title_settings.cpp:22` | instances in main, Menu, quick menu, OSD |
| M-SHARES | Shares config, mount, g_shares | `ps5/host/shares.cpp:100` | menu.cpp:880, 1149 |
| M-CHEATS | Cheats bridge and parsing | `ps5/host/frontend.cpp:859` | psgopher.rs:352-421 |
| M-MENURESULT | MenuResult (play, rom, name, load_state) | `ps5/host/menu.cpp:851` | load_state set before play() accepts |
| M-ROM-FACTS | ROM header facts (readFacts, psgopher_rom_facts) | `ps5/host/library.cpp:167` | |
| M-WEB-FETCH | https_get, psgopher_cover, psgopher_about | `vendor/gopher64/src/psgopher.rs:600` | untrusted network input |
| M-LIVE-STATS | bench.c live figures and history | `ps5/host/bench.c:57` | |

### Overlay API

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| O-TEXTURE | psgopher_overlay_texture / _free | `vendor/gopher64/parallel-rdp/interface.cpp:938` | |
| O-PRESENT | psgopher_overlay_present | `vendor/gopher64/parallel-rdp/interface.cpp:1024` | re-runs scanout each menu frame |
| O-SET | psgopher_overlay_set | `vendor/gopher64/parallel-rdp/interface.cpp:1011` | borrowed pointers |
| O-DRAW | psgopher_overlay_draw | `vendor/gopher64/parallel-rdp/interface.cpp:963` | |
| O-SIZE | psgopher_overlay_size | `vendor/gopher64/parallel-rdp/interface.cpp:959` | |
| O-RELEASE | psgopher_overlay_release / psgopher_osd_release | `vendor/gopher64/parallel-rdp/interface.cpp:929` | quickmenu.cpp:553 |
| O-HDR | HDR10 layer path and packing pass | `vendor/gopher64/parallel-rdp/interface.cpp:815` | ps5/shaders/psgopher_hdr.frag |
| O-ANSWER | Quick menu answer to Gopher64 callbacks | `vendor/gopher64/parallel-rdp/interface.cpp:1045` | writes save_state_slot |

### States

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| S-EMPTY-LIBRARY | No cartridges message | `ps5/host/frontend.cpp:783` | HOME only; RACK shows nothing |
| S-SCANNING-EMPTY | Looking for cartridges (empty + scanning) | `ps5/host/frontend.cpp:785` | |
| S-SCANNING-BACKGROUND | Scan running with a list shown | `ps5/host/frontend.cpp:1329` | only the BENCH action hint says searching; Library::status unused |
| S-NO-SELECTION | No selected game (no panel, no line) | `ps5/host/frontend.cpp:433` | |
| S-COVER-PENDING | Cover pending: procedural label | `ps5/host/web.cpp:231` | |
| S-COVER-NONE | No cover (.none 30 days) | `ps5/host/web.cpp:122` | |
| S-COVER-UPLOAD-FAILED | Texture upload failed (ImTextureID_Invalid cached) | `ps5/host/menu.cpp:1321` | |
| S-ABOUT-NONE | No description / pending: section omitted | `ps5/host/frontend.cpp:612` | |
| S-OFFLINE | Network down: retry after 60/300 s | `ps5/host/web.cpp:126` | no user feedback |
| S-NO-FACTS | Game without header facts (zip/7z): dashes | `ps5/host/frontend.cpp:630` | |
| S-NO-PROBES | No probes text / notice | `ps5/host/frontend.cpp:681` | notice at 748 |
| S-NO-CHEATS | No cheats notices (two wordings) | `ps5/host/frontend.cpp:755` | |
| S-CHEAT-REJECTED | Gopher64 did not take that cheat | `ps5/host/frontend.cpp:918` | UI state already toggled |
| S-FILTER-EMPTY | Rack filter shows no cartridge | `ps5/host/frontend.cpp:1022` | selection stays on a hidden one |
| S-COPYING | Copying from the share (notice 600 s) | `ps5/host/menu.cpp:869` | any later notice replaces it |
| S-COPY-FAILED | Copy failed notice | `ps5/host/menu.cpp:1021` | |
| S-GAME-ERROR | The game ended with an error notice | `ps5/host/main.cpp:185` | with log path appended |
| S-SETTING-REJECTED | Gopher64 did not take `<key>` | `ps5/host/menu.cpp:836` | |
| S-SHARES-EMPTY | Shares list with no share | `ps5/host/menu.cpp:899` | |
| S-SHARE-NOT-MOUNTED | Share row Not mounted | `ps5/host/menu.cpp:908` | |
| S-SHARE-INVALID | A share needs its server... | `ps5/host/menu.cpp:975` | |
| S-SHARE-MOUNT-FAILED | Mount failed notice (saved anyway) | `ps5/host/menu.cpp:994` | |
| S-SHARES-WRITE-FAILED | The shares could not be written | `ps5/host/menu.cpp:885` | |
| S-FIND-LOOKING | Find servers: Looking... | `ps5/host/menu.cpp:943` | |
| S-FIND-NONE | None answered on port 445 | `ps5/host/menu.cpp:956` | |
| S-NO-ART | board.png / logo.png missing or undecodable | `ps5/host/frontend.cpp:523` | board.cpp:544 |
| S-DISPLAY-FAILED | Display/ImGui backend failed: title closes | `ps5/host/menu.cpp:1200` | no message to the user |
| S-ACQUIRE-FAILED | vkAcquireNextImageKHR failure loop | `ps5/host/menu.cpp:1059` | no swapchain recreation |
| S-SANDBOX | Sandbox states (on / not left / off next start) | `ps5/host/frontend.cpp:1326` | |
| S-QM-NO-NAME | Quick menu without a game name | `ps5/host/quickmenu.cpp:260` | |
| S-QM-NO-PROBES | Load a probe disabled (0 kept) | `ps5/host/quickmenu.cpp:194` | |
| S-QM-SLOT-EMPTY | Empty slot row | `ps5/host/quickmenu.cpp:203` | |
| S-OSD-NO-CPU | OSD/tiles without thread CPU clock | `ps5/host/quickmenu.cpp:468` | |
| S-HDR-UNAVAILABLE | HDR asked but not available (log only) | `vendor/gopher64/parallel-rdp/interface.cpp:824` | |

### Feedback

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| F-NOTICE | Notice pill with timeout | `ps5/host/frontend.cpp:507` | menu.cpp:807; single slot, unbounded width |
| F-SELECTION | Selection indicators (glow, light row, amber frame, nav cursor) | `ps5/host/frontend.cpp:794` | |
| F-SEAT-ANIM | Cartridge swap + boot wave | `ps5/host/board.cpp:481` | |
| F-VIEW-TRANSITION | View pose blend (0.7 s) | `ps5/host/frontend.cpp:475` | content switches at once, board eases |
| F-BOARD-SETTINGS | Board shows expansion/overclock/upscale/CRT | `ps5/host/frontend.cpp:480` | |
| F-HEADER-STATUS | Header counts per view | `ps5/host/frontend.cpp:528` | |
| F-BENCH-ACTION-HINTS | This console hints (share count, sandbox, searching) | `ps5/host/frontend.cpp:1323` | |
| F-RACK-MARKS | Rack marks: played light, unplayed sheen, dimmed hidden | `ps5/host/frontend.cpp:1138` | |
| F-G64-MESSAGES | Gopher64 on-screen messages after menu actions | `vendor/gopher64/src/ui/video.rs:255` | slot switch message |
| F-BOOT-LOG | Boot log lines (say) | `ps5/host/platform.c:131` | |

### Tests

| Id | Name | File | Notes |
| --- | --- | --- | --- |
| X-TEST-MENU | test_menu (scripted pad, pictures) | `ps5/tests/test_menu.cpp:195` | 1080p, 15 ROMs, one cover |
| X-TEST-QUICKMENU | test_quickmenu (software rasterizer) | `ps5/tests/test_quickmenu.cpp:297` | no OSD, no Stats row |
| X-TEST-LIBRARY | test_library | `ps5/tests/test_library.cpp:1` | |
| X-PICTURES | Host pictures folder | `build/host-tests/menu-pictures` | contains stale pictures of an earlier interface |
