# Front end improvements

Part of the front end audit of 2026-11-25. Index: [MASTER_PLAN.md](MASTER_PLAN.md). Defects are in
[BUGS.md](BUGS.md) (ids FE-...); this file holds the proposals that go beyond a single fix. When
each one is done is in [ROADMAP.md](ROADMAP.md).

The six investigators made 31 proposals; merged, they are the 24 below plus two notes at the end.
Each names what it is justified by. The two whose only argument is tidiness are marked
**not justified yet** and are not scheduled: this audit recommends no rewrite on taste alone.

Complexity: low = one file, under a day; medium = several files or a protocol change.

## Recommended order

| Order | Id | Proposal | Justified by | Complexity |
| --- | --- | --- | --- | --- |
| 1 | IMP-13 | Host tests for the states and limits the current run never reaches | every defect that passed a green run | low to medium |
| 2 | IMP-01 | Separate the pure look from the cover request | FE-003 (measured), FE-001, FE-026 | low |
| 3 | IMP-02 | One owner for cover textures: a pool sized to the cache, deferred frees | FE-001, FE-031, FE-033; 1.5 ms average, 7.7 ms maximum per upload on lavapipe | medium |
| 4 | IMP-08 | `MenuResult` decided in one place | FE-008 | low |
| 5 | IMP-06 | One probe identity and one probe API | FE-009, FE-010, FE-036 | medium |
| 6 | IMP-16 | Confirmation for destructive actions | FE-019, FE-020 | low |
| 7 | IMP-24 | Stop rumble and pause audio accounting while the quick menu is up | FE-022, FE-058, FE-059 | low |
| 8 | IMP-09 | Keep the quick menu's fonts across openings; reuse overlay ids | FE-032 (measured: 2 ids per opening at 4K) | low |
| 9 | IMP-17 | Mount shares on a worker thread | FE-029, FE-011 | medium |
| 10 | IMP-03 | Keep the board's view state across games | every return from a game lands in HOME at A-Z / All | low |
| 11 | IMP-04 | Do not keep 40 MB decoded, do not re-upload the board each return | FE-053 (81 to 143 ms per return on lavapipe) | medium |
| 12 | IMP-05 | Build BENCH's model once | about 650 allocations per frame in BENCH | low |
| 13 | IMP-19 | Stop asking the web cache every frame for settled covers | mutex traffic per visible item per frame | low |
| 14 | IMP-20 | Release decoded cover pixels nobody takes | +106 MB peak for 300 covers on lavapipe | low |
| 15 | IMP-22 | No per-frame strings in `host.get` / `notice` / `about` | a few allocations per frame | low |
| 16 | IMP-14 | Show scanning and copy progress outside BENCH | S-SCANNING-BACKGROUND, S-COPYING have no indicator | low |
| 17 | IMP-18 | Quick menu and keyboard ergonomics | slow entry of long share paths | low |
| 18 | IMP-12 | Non-16:9 output and a 4K picture run | only 1080p is pictured | medium |
| 19 | IMP-23 | A shutdown protocol for worker threads | FE-035, FE-060 | low |
| 20 | IMP-10 | Update only the changed rectangle of a font atlas | 10 full atlas uploads in the first 600 OSD frames | medium |
| 21 | IMP-21 | Fingerprints and the stage light from cached textures | about 14k vertices per frame with 3 probes (arithmetic) | medium |
| 22 | IMP-11 | RACK: what the board seats, and how the panel words it | FE-014; a design decision | low |
| - | IMP-07 | Split `struct Menu` and `menu.cpp` | **not justified yet** | medium |
| - | IMP-15 | One input model for the board and the ImGui pages | **not justified yet** as a rewrite; its two defects have small fixes | medium |

---

## IMP-01 Separate the pure look from the cover request

- **Current:** `Frontend::lookFor` is `const` but calls `host_.cover`, which starts a web fetch and may upload a texture and evict another. It runs per spine in RACK, per strip card, for the panel, the slot line and the pickers; four times per frame for the selected game in HOME (`frontend.cpp:464`, `:577`, `:762`, `:793`).
- **Limitation:** Side effects hidden in a query. RACK fetches and uploads covers it never draws, and the number of calls per frame decides eviction (FE-003).
- **Proposal:** `board::lookOf` (hue, code, seed) everywhere a colour is enough. A separate `Frontend::coverFor(game)` called only where a label is drawn, once per game per frame.
- **Benefits:** Fewer fetches and uploads; the LRU's "unseen for 2 frames" rule works as intended; RACK no longer fills the descriptor pool (FE-001) or multiplies the offline churn (FE-026).
- **Regressions:** None expected: labels that show covers keep doing so, spines are unchanged.
- **Complexity:** low.
- **Dependencies:** `menu.cpp` `host.cover`; `board.h` `Look`.

## IMP-02 One owner for cover textures: a pool sized to the cache, deferred frees

- **Current:** Covers live in `Menu::covers` (an LRU of 48 by frame stamp) and are uploaded into ImGui's backend pool of 16 images, shared with the font atlas and the art. `Machine::shown` keeps a raw texture id across frames. Each upload waits for the queue (`menu.cpp:306-307`); each eviction waits for the device in the middle of building a frame (`:331`).
- **Limitation:** The pool and cache sizes disagree (FE-001); a failed upload is cached (FE-001, FE-031); eviction can free an id another holder still draws (FE-033); each upload or eviction stalls the CPU until the GPU drains, one per frame while browsing.
- **Proposal:** A small cover-cache unit that owns the textures: a dedicated descriptor pool sized `kCovers`, a refusal to insert when full and nothing is evictable, frees deferred until that frame's fence has signalled, the seated cover pinned (or `Machine` holding the game's name and resolving the texture at draw time), uploads recorded into the frame's command buffer instead of waiting for the queue.
- **Benefits:** No possible use of a freed descriptor; bounded GPU memory; no frame-time spikes from covers.
- **Regressions:** Layout barriers and the timing of ImGui's descriptor removal need care; the seat animation shows the cover looked up at draw time.
- **Complexity:** medium (the pool size and the null-set handling alone are low and are FE-001's fix; the deferred frees and in-frame uploads are the medium part).
- **Dependencies:** IMP-01 first; `menu.cpp` `host.cover`, `uploadTexture`, `freeTexture`; `board.cpp` `Machine::step`.

## IMP-03 Keep the board's view state across games

- **Current:** `Frontend` and `Menu` are rebuilt on each `runMenu`: the view, `sort_`, `show_`, `rack_sel_`, `bench_sel_` and the strip scroll are lost.
- **Limitation:** Returning from a game always lands in HOME at A-Z / All with BENCH's cursor at the top.
- **Proposal:** A small static record (view, sort, filter, the selected paths, the BENCH cursor) restored by the constructor and validated against the new list.
- **Benefits:** Continuity.
- **Regressions:** Saved paths must be re-validated; the pickers must not be restored.
- **Complexity:** low.
- **Dependencies:** `frontend.h`, `menu.cpp` `runMenu`. Do FE-007 and FE-012 first (the same re-anchoring by path).

## IMP-04 Do not keep 40 MB decoded, do not re-upload the board on every return

- **Current:** `runMenu` uploads `board.png` (mips, about 53 MB of device memory plus a 40 MB staging buffer) every time the menu opens, through the unchecked `uploadTexture` path; `loadImage` keeps the decoded copy for the life of the process.
- **Limitation:** A slower return from a game (81 to 143 ms on lavapipe; unverified on console), a large transient allocation each time, 40 MB resident during games (FE-053).
- **Proposal:** Release the decoded pixels after the upload. Then consider a pre-mipped compressed file or a smaller board for 1080p. Keeping the display between games is only possible if parallel-rdp allows it and is not proposed here.
- **Benefits:** Less memory during games; with the asset change, a faster return.
- **Regressions:** With the pixels released, each return decodes again (154 ms on lavapipe) unless the asset changes too: measure before choosing.
- **Complexity:** low for the release, medium for the asset.
- **Dependencies:** `menu.cpp`, `image.cpp`; FE-031 first.

## IMP-05 Build BENCH's model once

- **Current:** `Frontend::bench` constructs 7 callouts, about 19 rows with `std::function` getters and setters, option vectors and 4 actions every frame (`frontend.cpp:1253-1342`); `Row::get` runs twice per selected row.
- **Limitation:** About 650 allocations per frame of BENCH's own; the model cannot be tested apart from drawing.
- **Proposal:** A member built in the constructor; only the dynamic hints (share count, sandbox, scanning) are refreshed.
- **Benefits:** Less per-frame work; BENCH's rows become testable.
- **Regressions:** None expected.
- **Complexity:** low.
- **Dependencies:** None.

## IMP-06 One probe identity and one probe API

- **Current:** Four rules for which state files belong to a cartridge (`probesOf`, the panel's file lookup, the RACK filter's set, the quick menu's `stat`), two slot ranges, and directory reads on the UI thread when the selection changes (`frontend.cpp:663-680`).
- **Limitation:** FE-009, FE-010, FE-036; a directory scan on a slow disk stalls frames while browsing.
- **Proposal:** The core exposes the state stem (or the hash) for a ROM, stored in `library.tsv`. One Library function returns a cartridge's probes (slot, time, file) for the panel, the picker, the filter and the quick menu; its result is cached per cartridge and keyed on the folder's mtime.
- **Benefits:** One rule; the filter and fingerprints stay fresh; no per-selection directory scan in the draw call.
- **Regressions:** `library.tsv` changes format (a version bump and a rescan); existing probes of non-UTF-8 headers and of sibling ROMs change which cartridge lists them, which is the point.
- **Complexity:** medium.
- **Dependencies:** `vendor/gopher64/src/psgopher.rs` (export the patch), `library.cpp`, `frontend.cpp`, `quickmenu.cpp`.

## IMP-07 Split `struct Menu` and `menu.cpp` (not justified yet)

- **Current:** `menu.cpp` mixes Vulkan display plumbing, texture upload, ImGui style, pad translation, the share screens, the keyboard, the copy thread, the settings cache and the `FrontendHost` glue.
- **Limitation:** Ownership questions span unrelated code. Dead members: `Buttons::repeat` is unused, `FrontendHost::display_at_start` is set and never read, `Library::status()` is never shown, `board::View::Live` is never used, `stage_w` is computed and discarded.
- **Proposal:** Do not split now. Remove the dead members (or wire `Library::status` into the header, IMP-14). IMP-02 already moves the cover cache into its own unit; revisit a wider split only if that boundary proves useful.
- **Benefits:** Smaller surface without a mechanical refactor.
- **Regressions:** None for the dead-member removal.
- **Complexity:** low for the removal; medium for a split.
- **Dependencies:** None.

## IMP-08 `MenuResult` decided in one place

- **Current:** `host.play` writes `load_state`, `Menu::play` writes `name` / `rom` / `play`, the copy completion in `Menu::draw` writes `play` / `rom`, `host.close` writes `play = false`.
- **Limitation:** Partial writes before acceptance (FE-008); the name is captured at copy start and the ROM at copy end.
- **Proposal:** A single `Menu::accept(game, slot, rom)` called when the menu actually closes for play. The copy job carries its own game, slot and name.
- **Benefits:** No cross-talk between a pending copy and later input.
- **Regressions:** None.
- **Complexity:** low.
- **Dependencies:** `menu.cpp`.

## IMP-09 Keep the quick menu's fonts across openings; reuse overlay ids

- **Current:** Each opening creates a context and a font atlas and rasterises glyphs at up to 116 px (4K), using 1 or 2 overlay ids from a counter that is never reset.
- **Limitation:** FE-032; repeated work and a slower first frame.
- **Proposal:** One font atlas for the quick menu, shared through `ImGui::CreateContext(shared_atlas)` and alive until `psgopher_overlay_release`, as the OSD's context already is. Reuse freed ids or reset the counter when no texture is live.
- **Benefits:** Bounded id use; a faster opening.
- **Regressions:** The atlas must be released with the interface's textures at game end.
- **Complexity:** low.
- **Dependencies:** `quickmenu.cpp`; `interface.cpp` only if the interface hands out the ids.

## IMP-10 Update only the changed rectangle of a font atlas

- **Current:** `updateTextures` re-creates the whole atlas image on every update request (10 full uploads in the first 600 OSD frames in the scratch run), on the emulation thread during play.
- **Limitation:** Each is a synchronous staging copy of a large atlas at 4K: possible hitches in the OSD's first seconds (not measured on a console).
- **Proposal:** A `psgopher_overlay_texture_update(id, x, y, w, h, pixels)` entry point, or pre-bake the OSD's glyph set (digits and the fixed labels).
- **Benefits:** Fewer GPU allocations on the emulation thread.
- **Regressions:** Image layouts and barriers in the interface need care. Not checkable on the PC beyond counting uploads.
- **Complexity:** medium.
- **Dependencies:** The overlay API in `interface.cpp` (`ps5.patch`). After IMP-09.

## IMP-11 RACK: what the board seats, and how the panel words it

- **Current:** In RACK the board seats the rack selection (`selected()` changes meaning per view) and the panel says "ON THE HOME STRIP". Leaving RACK with L1/R1 swaps the board back to the strip's cartridge. Each RACK cursor move plays the swap animation.
- **Limitation:** Two selections behind one "seated" picture (FE-014).
- **Proposal:** Decide against `ps5/art/live-board.html`: either the board stays on the strip's cartridge in RACK (seat only on Triangle), or the RACK panel is worded as the rack's selection.
- **Benefits:** One meaning per selection.
- **Regressions:** A visible design change: the owner's decision.
- **Complexity:** low.
- **Dependencies:** `frontend.cpp` `frame` / `selected`.

## IMP-12 Non-16:9 output and a 4K picture run

- **Current:** `u_ = h / 1080`; `stage_w` is computed and discarded; the ImGui style scales by height; only 1080p is pictured.
- **Limitation:** The centred notice, the bars and the panels use 1920 literals, so a non-16:9 output would be misplaced; 4K scaling is untested for the board (the quick menu was run at 4K in a scratch build only).
- **Proposal:** Render 3840x2160 in the host test first (cheap, finds scale bugs). Use `stage_w` for anchors only if a non-16:9 mode can actually be output.
- **Benefits:** Scale defects found before a console shows them.
- **Regressions:** Layout churn if the anchors change; a longer host run.
- **Complexity:** low for the 4K run, medium for the anchors.
- **Dependencies:** `test_menu` (`PSGOPHER_MENU_SIZE`).

## IMP-13 Host tests for the states and limits the current run never reaches

- **Current:** `test_menu`: 15 short names, one cover, 1080p, 2-frame presses, one played game checked. `test_quickmenu`: six openings at 1080p, no OSD, no Stats row. `test_sdl3`: no multi-player tap. Lavapipe does not enforce descriptor pool sizes. The pictures folder is never cleared.
- **Limitation:** The blank spines, the pool exhaustion, both picker defects, the filter-empty Cross, the L2 stall, the Kind toggle, the lost ImGui focus, the id exhaustion and the two-controller tap all passed a green run.
- **Proposal:** Scenarios as separate `runMenu` passes with checks, most of them already written as scratch harnesses during the audit:
  - *covers:* a few hundred ROMs that all get a cover; a wrapper that counts live descriptor sets and fails allocation at the pool size; checks: `covers.size() <= kCovers`, uploads only for drawn covers, no null set drawn, covers present in a second `runMenu`.
  - *pickers and selection:* the cheat picker over L1/R1 and over a rescan (check the `cheat_set` key), the probe picker, the filter that hides everything (check the result), L2/R2, Up/Down in RACK, BENCH wrap and steppers.
  - *ImGui pages:* the Kind toggle, focus after a D-pad press on the empty list and after a cancelled keyboard (check the focused id), a 40-character value.
  - *states:* empty library, scanning, offline (check the web job count), a long title with three probes, an oversized PNG in the cache folder, non-UTF-8 and sibling headers for the probe list.
  - *quick menu:* an OSD picture at 1080p and 4K, 3000 openings with bounded ids, a failed upload, the Stats row, a second tap, Cross on a disabled row.
  - *pad layer:* two controllers with the touch pad held; the repeat at a large clock base.
  - *performance:* allocations per idle frame below a bound that does not depend on library size.
  - *hygiene:* clear the pictures folder at the start (FE-038).
- **Benefits:** Each fix in the roadmap gets a check that fails before it and passes after it.
- **Regressions:** A longer host run (each menu scenario took 20 to 60 s on lavapipe).
- **Complexity:** low per scenario, medium in total.
- **Dependencies:** `ps5/tests/test_menu.cpp`, `test_quickmenu.cpp`, `test_sdl3`, `run-host-tests.sh`. The scratch sources were under `SCRATCH/` (`arch/`, `input-exec/`, `perf/`, `robustness/`, `overlay-lifecycle/`) and may need to be written again.

## IMP-14 Show scanning and copy progress outside BENCH

- **Current:** A scan with a list shown has no indicator outside BENCH's action hint; a copy is a notice that any later notice replaces; `Library::status()` exists and is never shown.
- **Limitation:** No feedback for the two long-running jobs.
- **Proposal:** A status text in the header line while scanning or copying.
- **Benefits:** Clarity; the copy state no longer depends on the single notice slot.
- **Regressions:** None.
- **Complexity:** low.
- **Dependencies:** `Library::scanning`, `host.busy`.

## IMP-15 One input model for the board and the ImGui pages (not justified yet)

- **Current:** Two edge detectors (`Buttons` with double times in `menu.cpp`, `PadInput` with float times) plus ImGui's own navigation fed from the raw sample; repeat timings differ (0.4 / 0.1 s against 0.38 / 0.09 s).
- **Limitation:** Same-frame hand-overs depend on ImGui internals (FE-023, FE-045); the float clock (FE-024).
- **Proposal:** Not a rewrite. Fix FE-024 (double), FE-023 and FE-045 where they are, remove `Buttons::repeat` if it stays unused. Revisit a single per-frame structure only if further hand-over defects appear.
- **Benefits:** The three defects go without changing how the pages feel.
- **Regressions:** A single model would change ImGui page navigation slightly.
- **Complexity:** low for the fixes; medium for a single model.
- **Dependencies:** None.

## IMP-16 Confirmation for destructive actions

- **Current:** "Remove this share", "Close PSGopher", the quick menu's Reset and Eject act on one Cross; Circle discards a changed share draft silently.
- **Limitation:** An accidental press loses progress or data (FE-019, FE-020).
- **Proposal:** A two-press pattern: the row's text changes to "Cross again to ..." for 3 s. Stop the cursor wrapping onto those rows.
- **Benefits:** Safer controller-only use.
- **Regressions:** One more press for rare actions; `test_quickmenu`'s Up, Cross -> quit assertion changes.
- **Complexity:** low.
- **Dependencies:** `frontend.cpp`, `quickmenu.cpp`, `menu.cpp`.

## IMP-17 Mount shares on a worker thread

- **Current:** Save and Remove call `mountShare` / `unmountShare` on the UI thread (`menu.cpp:984-988`, `:1002`).
- **Limitation:** A server that does not answer freezes the interface until the connect times out (FE-029); a rescan asked for during a scan is dropped (FE-011).
- **Proposal:** Mount on a worker with a "Mounting..." row, as "Find SMB servers" already does; queue the rescan.
- **Benefits:** A responsive interface on a bad network.
- **Regressions:** More state in the share form; the timeout behaviour can only be seen on a console.
- **Complexity:** medium.
- **Dependencies:** `menu.cpp`, `shares.cpp`, `library.cpp`.

## IMP-18 Quick menu and keyboard ergonomics

- **Current:** Quick menu Up/Down act on press edges only; the keyboard's Square deletes one byte per press with no repeat and no cursor movement.
- **Limitation:** Slow editing of long share paths and passwords; a byte-wise delete can split a character.
- **Proposal:** Repeat for quick menu Up/Down and the keyboard's Square; delete a whole character; optionally R2 for Space and L2 for Delete.
- **Benefits:** Faster entry with a controller.
- **Regressions:** None expected.
- **Complexity:** low.
- **Dependencies:** None.

## IMP-19 Stop asking the web cache every frame for settled covers

- **Current:** `host.cover` returns early only for entries in `Menu::covers`. For "none" and pending entries every visible item goes, every frame, through a `std::function` call, a map lookup by name, the web mutex and `entries[name]`.
- **Limitation:** Per-item overhead and mutex traffic with the web thread each frame; measurable but small (RACK with 300 "none" covers: 532 us median draw in total).
- **Proposal:** Cache "none" in `Menu::covers`; compute the selected game's look once per frame.
- **Benefits:** Fewer lock round trips.
- **Regressions:** A "none" that turns into a cover (a marker expiring after 30 days) is noticed only at the next menu visit.
- **Complexity:** low.
- **Dependencies:** IMP-01.

## IMP-20 Release decoded cover pixels nobody takes

- **Current:** Web entries keep the decoded pixels until `webCover` hands them over; a cartridge scrolled past before its upload turn keeps its picture; after an eviction `webForget` forces a decode from disk again.
- **Limitation:** RAM held for pictures nobody draws (lavapipe: +106 MB peak for 300 covers of 256x256, textures included).
- **Proposal:** Keep only the state "png on disk" and decode at upload time on the web thread with a small ready queue, or drop a picture not asked for N frames.
- **Benefits:** RAM bounded whatever part of the library was browsed. It also settles how FE-002's fix gets its pixels back.
- **Regressions:** A cover appears a moment later after an eviction.
- **Complexity:** low.
- **Dependencies:** FE-002.

## IMP-21 Fingerprints and the stage light from cached textures or coarser meshes

- **Current:** Each probe fingerprint is 1176 filled rectangles (4704 vertices), up to 3 per panel; the stage light is 28 ellipses of 64 segments; the rack glow 10 circles of 48. Measured per frame: HOME 14k vertices with covers or 18.5k with procedural labels, RACK 25k, BENCH 13.7k. Fingerprints were not in the measured frames; 3 probes add about 14k by arithmetic.
- **Limitation:** A moderate draw-list volume rebuilt each frame; not a measured bottleneck on the host (0.4 to 0.6 ms of front-end work per frame).
- **Proposal:** A 56x21 texture per fingerprint drawn as one quad; the stage light as one radial-gradient mesh.
- **Benefits:** About 14k fewer vertices per frame with 3 probes.
- **Regressions:** One more texture per probe, competing for the descriptor pool.
- **Complexity:** medium.
- **Dependencies:** FE-001 first. Low priority until a console shows a frame-time problem.

## IMP-22 No per-frame strings in `host.get` / `notice` / `about`

- **Current:** `host.get` builds a `std::string` per call (two of the four per-frame keys exceed the small-string buffer); the cache is a linearly searched vector; `host_.notice()` and `host_.about()` return copies each frame, the latter under the web mutex.
- **Limitation:** Small constant per-frame allocations.
- **Proposal:** `std::string_view` keys resolved to indices once; `notice` / `about` by reference or with a version counter.
- **Benefits:** Removes the remaining constant per-frame allocations.
- **Regressions:** None.
- **Complexity:** low.
- **Dependencies:** None. Do it with FE-051 (the description cache).

## IMP-23 A shutdown protocol for worker threads

- **Current:** The scan, web, copy and find-servers threads are detached. `main` returns while they may run; the web singleton is leaked on purpose for this reason.
- **Limitation:** FE-035, FE-060; host tests can race on static state at exit.
- **Proposal:** A stop flag checked by the scan and copy loops and by the web thread's wait, and a join with a short timeout before `main` returns; or leak the remaining shared statics as `web()` does and refuse Close while busy.
- **Benefits:** A deterministic exit; safer host tests.
- **Regressions:** Close may take a moment longer while a share read finishes.
- **Complexity:** low.
- **Dependencies:** `library.cpp`, `web.cpp`, `menu.cpp`.

## IMP-24 Stop rumble and pause audio accounting while the quick menu is up

- **Current:** While the menu blocks the emulation thread the audio callback renders silence (counted as underruns) and the motors keep their last state.
- **Limitation:** FE-022, FE-058, FE-059.
- **Proposal:** Hooks called by `psgopher_quickmenu_run` on open and close: stop the motors, stop counting underruns (or pause the stream), shift the live counters over the pause.
- **Benefits:** A clean pause; the OSD stays meaningful.
- **Regressions:** The audio queue refills differently on resume. Stopping the motors and the counter changes are safe; pausing the stream cannot be checked on the PC and, by the project's rule, belongs behind a default-off setting until a console has shown it.
- **Complexity:** low.
- **Dependencies:** `psgopher_sdl3.cpp`, `quickmenu.cpp`, `bench.c`.

---

## Notes

**No change recommended: the OSD's per-frame cost.** Measured with the `test_quickmenu` stand-ins:
`psgopher_osd_frame` takes 22 to 25 us per call at 1080p and 4K, with no heap allocation per call in
steady state and one 256 KB atlas upload on the first call. The index conversion loop and the two
window-size queries per frame cost a few microseconds at most. The GPU cost of the overlay draw was
not measured and console timing is unverified.

**Fixes that are not proposals.** Changes that only repair one defect (the pool size, the L1/R1
guard, the version check before the list copy, the clip rect around the spine text, and so on) are
described under their FE entry in [BUGS.md](BUGS.md) and are not repeated here.
