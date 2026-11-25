# Front end remediation roadmap

Part of the front end audit of 2026-11-25. Index: [MASTER_PLAN.md](MASTER_PLAN.md). FE ids are in
[BUGS.md](BUGS.md), IMP ids in [IMPROVEMENTS.md](IMPROVEMENTS.md). This file gives only the order,
the files each step holds, and how each step is verified.

**Progress.** Fixed after the audit: FE-001 (confirmed on the console first: SIGABRT in `GetTexID`
in the boot log of 2026-11-25), FE-031, FE-003, FE-002 and FE-004 (with its two-controller host
test). Phase 0's many-covers test is still to be written. Next: FE-005.

## Rules of the sequence

1. **Severity first.** There is no P0. The one P1 (FE-001) and the cover chain it belongs to come
   first, the in-game P2s alongside it, then the cheap fixes with a visible effect, then the rest,
   then the improvements.
2. **One writer per file.** A step lists the files it holds. Two steps that share a file never run
   at the same time; the [file table](#who-holds-which-file) shows the order per file. Steps in
   different lanes of the same phase may run in parallel.
3. **Red first.** Where a host test can show a defect, the test is added before the fix and must
   fail on the current tree.
4. **Three kinds of verification**, named per step:
   - *host test*: a check in `ps5/tests` that fails before and passes after;
   - *picture*: a host-test picture looked at (and compared with `ps5/art/live-board.html` where the
     design shows it);
   - *console*: only a PS5 shows it. No console was reachable during the audit, so every such step
     ends with a note to the owner saying exactly what to try. By the project's rule, a behaviour
     change that the PC cannot check goes behind a default-off setting or is sent as a test build
     with that note; which of the two is the owner's call per step.
5. **Changes in `vendor/gopher64`** are followed by `ps5/tools/fetch-gopher64.sh --export` and
   `--check`; they are grouped so the patch is exported once per phase.
6. A fix that showed no benefit is taken out again.

## Phase 0: make the fixes checkable

Lane T (tests only). Nothing else waits on more than these three.

| Step | Does | Files | Verified by |
| --- | --- | --- | --- |
| 0.1 | Clear the pictures folder at the start of a run (FE-038) | `ps5/tests/run-host-tests.sh`, `ps5/tests/test_menu.cpp` | host test: the folder holds only the current run's names |
| 0.2 | Cover scenario for `test_menu`: a few hundred ROMs that all get a cover, a wrapper that counts live descriptor sets and refuses allocation at the pool size, a second `runMenu` in the same process (IMP-13) | `ps5/tests/test_menu.cpp` (or a new test beside it) | host test: fails on the current tree with null sets, more than 48 covers, an abort on RACK -> BENCH, and no covers in the second menu |
| 0.3 | Two-controller touch-pad case (IMP-13) | `ps5/tests/test_sdl3*` | host test: fails on the current tree (5 taps seen) |

## Phase 1: the P1 and the cover chain

Lane A (`menu.cpp`, `web.cpp`, `image.cpp`) and lane B (`frontend.cpp`), strictly in this order
because steps 1.2 and 1.5 cross both.

| Step | Does | Files | Verified by |
| --- | --- | --- | --- |
| 1.1 | **FE-001** with **FE-031**: size the descriptor pool for `kCovers` plus the fixed textures and atlas pages; treat a null set and every failed Vulkan call in `uploadTexture` as a failed upload that frees what it made; do not cache an invalid id; set `CheckVkResultFn` | `ps5/host/menu.cpp` | host test 0.2 (no null set drawn, no abort); console: a library with more than 13 covers, open RACK, then BENCH; the boot log shows no VkResult lines |
| 1.2 | **FE-003** through IMP-01: `lookOf` for spines and other colour-only callers, a cover request only where a label is drawn; no insert when the cache is full and nothing is evictable | `ps5/host/frontend.cpp`, then `ps5/host/menu.cpp` | host test 0.2 (uploads only for drawn covers, at most 48 entries); picture `3-rack` unchanged apart from 3.3 |
| 1.3 | **FE-002**: forget the handed-over covers at menu teardown | `ps5/host/menu.cpp`, `ps5/host/web.cpp` | host test 0.2 (covers uploaded again in the second menu); picture of the second menu |
| 1.4 | **FE-030**: refuse a PNG above a size limit before decoding; no second copy | `ps5/host/image.cpp` | host test: the 12000x12000 file is refused with a small peak memory |
| 1.5 | **FE-033** (suspected): deferred texture frees and a pinned seated cover (the low part of IMP-02) | `ps5/host/menu.cpp`, `ps5/host/board.cpp` | host test: no id freed while a draw command recorded this frame uses it (a counter in the test's wrapper); console-unverified otherwise |

## Phase 2: the P2s in a game

Lane C (`ps5/sdl3`, `platform.c`) and lane D (`vendor/gopher64`). May run in parallel with phase 1.

| Step | Does | Files | Verified by |
| --- | --- | --- | --- |
| 2.1 | **FE-004**: tap state per player; with it decide **FE-049** (the opener's pad drives the menu, or only player 1 opens it) | `ps5/sdl3/psgopher_sdl3.cpp` | host test 0.3; console: two controllers, touch pad + L1 on the first |
| 2.2 | **FE-022**: stop the motors when the quick menu opens and when the last joystick closes | `ps5/sdl3/psgopher_sdl3.cpp`, `ps5/host/quickmenu.cpp` | console only: a Rumble Pak game, tap during a rumble; Eject during a rumble |
| 2.3 | **FE-021**: a disconnected or intercepted pad reads as at rest | `ps5/host/platform.c` | host test if the pad service can be stubbed with scripted samples; console: hold Right in BENCH and switch the controller off, or press PS |
| 2.4 | **FE-005**: restore the hotkey slot after a quick-menu Take or Load; no "Switching savestate slot" message for menu actions | `vendor/gopher64/parallel-rdp/interface.cpp`, `vendor/gopher64/src/ui/video.rs`, then export the patch | console only: Take Probe 3, then touch pad + L1, then check which state file changed; `fetch-gopher64.sh --check` |

## Phase 3: cheap fixes with a visible effect on the board

Lane B (`frontend.cpp`), one writer, in this order. Starts when phase 1 has released `frontend.cpp`
and `menu.cpp`.

| Step | Does | Files | Verified by |
| --- | --- | --- | --- |
| 3.1 | **FE-006**, **FE-007**, **FE-047**: L1/R1 blocked (or pickers closed) while any picker is open; pickers bound to a game path and closed when it leaves; a rejected toggle reverts | `ps5/host/frontend.cpp` | host test: the two picker scenarios check that no `cheat_set` carries another cartridge's key; picture of the cheat picker (none exists today) |
| 3.2 | **FE-008** through IMP-08: one `Menu::accept`; the busy check and a notice in the probe picker | `ps5/host/frontend.cpp`, `ps5/host/menu.cpp` | host test with a stand-in copy that stays busy (the result's `load_state` is unchanged); console: a real share copy |
| 3.3 | **FE-013**: spine codes | `ps5/host/frontend.cpp` | picture `3-rack` against the design; host test: the spine text emits vertices |
| 3.4 | **FE-018**, **FE-042**, **FE-039**, **FE-046**: no live selection under an empty filter, empty-rack and empty-filter texts, a '#' column, the singular, L2 past hidden letters | `ps5/host/frontend.cpp` | host test: the filter-empty scenario returns no play; the L2 scenario; pictures |
| 3.5 | **FE-012**: the rack selection re-anchored by path | `ps5/host/frontend.cpp` | host test: add an earlier-sorting ROM, rescan, the selected path is unchanged |
| 3.6 | **FE-025**, **FE-027**: a version check before the list copy; letters, save names, histogram and count cached | `ps5/host/library.h`, `ps5/host/library.cpp`, `ps5/host/frontend.cpp` | host test: allocations per idle frame below a bound that does not grow from 300 to 3000 games |
| 3.7 | **FE-019**, **FE-048**, **FE-050**, **FE-043**, **FE-014**: no wrap onto Close and a confirmation (IMP-16), clamped option rows, Up means up, hints built per state, the RACK panel's wording (after the IMP-11 decision) | `ps5/host/frontend.cpp` | host test: Down, Up, Up, Cross in BENCH does not close; pictures of the hint bars and the RACK panel |
| 3.8 | **FE-024**: `PadInput` times in `double` | `ps5/host/frontend.h`, `ps5/host/frontend.cpp`, `ps5/host/menu.cpp` | host test: 28 repeat events in 3 s at a clock base of 1.7e7 s |
| 3.9 | **FE-054**, **FE-034**, **FE-041**: easing by `dt`, the cartridge un-seats when nothing is selected, no gap for a region without a flag | `ps5/host/frontend.cpp`, `ps5/host/board.cpp` | host test for the easing at two frame times; picture of the empty library; console for the 119.88 Hz feel |

## Phase 4: the ImGui pages and the share form

Lane A (`menu.cpp`), after 3.2 and 3.8.

| Step | Does | Files | Verified by |
| --- | --- | --- | --- |
| 4.1 | **FE-023**, **FE-045**, **FE-017**: focus re-asserted on the pages and after the keyboard closes, Kind toggles on Left/Right, the field shows a long value's tail | `ps5/host/menu.cpp` | host test: the focused id after each scripted press; pictures of the form and of a 40-character value |
| 4.2 | **FE-056**: failed saves are reported | `ps5/host/menu.cpp`, `ps5/host/frontend.cpp`, `ps5/host/quickmenu.cpp` | host test with the settings file pointed at a read-only folder |
| 4.3 | **FE-029** through IMP-17: mount and unmount on a worker with a "Mounting..." row | `ps5/host/menu.cpp`, `ps5/host/shares.cpp` | host test with a stand-in mount that sleeps (frames keep coming); console: an unreachable server |
| 4.4 | **FE-035**: Close refused while busy, or the shared statics leaked as `web()` is | `ps5/host/menu.cpp`, `ps5/host/library.cpp` | host test under ASan: Close during a stand-in copy |

## Phase 5: library, web cache and the core bridge

Lane E (`library.cpp`, `web.cpp`) and lane D (`vendor/gopher64`, one export at the end). `web.cpp`
after 1.3, `library.cpp` after 3.6 and 4.4, the vendor tree after 2.4, `frontend.cpp` after phase 3.

| Step | Does | Files | Verified by |
| --- | --- | --- | --- |
| 5.1 | **FE-011**: a rescan asked for during a scan runs after it | `ps5/host/library.cpp` | host test (`test_library`): two requests, two scans |
| 5.2 | **FE-026**: no re-queue during the rest | `ps5/host/web.cpp` | host test: the web job count over 400 offline frames stays near the number of visible items |
| 5.3 | **FE-055**: "try later" kept apart from "none"; a corrupt cached file refetched | `vendor/gopher64/src/psgopher.rs`, `ps5/host/web.cpp` | host test for the cache side (a truncated `.png`); the fetch side against a local stand-in server if one is added, otherwise unverified |
| 5.4 | **FE-028**: a read does not write `config.json`; title settings and cheat options saved on release or on leaving the view | `vendor/gopher64/src/psgopher.rs`, `ps5/host/frontend.cpp` | host test: the number of `TitleSettings::save` calls over a held stepper; console: `config.json`'s mtime after opening the list |
| 5.5 | **FE-009**, **FE-010**, **FE-036** through IMP-06: the core's state stem per ROM, one probe function for the panel, the picker, the filter and the quick menu | `vendor/gopher64/src/psgopher.rs`, `ps5/host/library.cpp`, `ps5/host/frontend.cpp`, `ps5/host/quickmenu.cpp` | host test (`test_library`): sibling headers and a non-UTF-8 header; the Has probes filter across two menus; console: a probe taken in one of two regional copies |
| 5.6 | **FE-057**: control characters and numbers sanitised | `ps5/host/library.cpp` | host test (`test_library`) |

## Phase 6: the quick menu and the stats overlay

Lane F (`quickmenu.cpp`, `bench.c`), after 2.2, 4.2 and 5.5 have released `quickmenu.cpp`.

| Step | Does | Files | Verified by |
| --- | --- | --- | --- |
| 6.1 | **FE-032** with IMP-09: overlay ids reused, the atlas kept across openings, a failed upload skips the frame | `ps5/host/quickmenu.cpp` (and `interface.cpp` only if the interface hands out ids) | host test: 3000 openings at 4K with bounded ids; a stub that fails one upload |
| 6.2 | **FE-020** through IMP-16: no wrap onto Eject, a second Cross for Reset and Eject | `ps5/host/quickmenu.cpp`, `ps5/tests/test_quickmenu.cpp` | host test (the existing Up, Cross assertion changes); picture |
| 6.3 | **FE-061**, **FE-044**, **FE-037**: the ini re-read each game, a measured title, one "yesterday" helper for the board and the quick menu | `ps5/host/quickmenu.cpp`, `ps5/host/frontend.cpp` | host test for the helper at fixed times; picture with a 20-character header name |
| 6.4 | **FE-058**, **FE-059** through IMP-24: underruns as a per-second delta, the live counters shifted over the pause, paused time taken out of play time | `ps5/host/bench.c`, `ps5/host/quickmenu.cpp`, `ps5/host/main.cpp` | host test for the counters with a scripted pause; an OSD picture (none exists today); console for the figures after Resume |
| 6.5 | **FE-060**: the logger and the stream teardown synchronised | `ps5/sdl3/psgopher_sdl3.cpp`, `ps5/host/bench.c` | host test under a thread sanitizer if the stand-ins allow, otherwise by reading |
| 6.6 | **FE-064** (suspected): states written to a temporary name and renamed | `vendor/gopher64/src/savestates.rs`, then export | console only |

## Phase 7: text and layout

Lane B (`frontend.cpp`), after phase 6.3.

| Step | Does | Files | Verified by |
| --- | --- | --- | --- |
| 7.1 | One fit-to-width helper on UTF-8 boundaries, used for **FE-016**, **FE-040**, **FE-062**; `wrap()` stops at its line limit and the description lines are cached (**FE-051**, with IMP-22) | `ps5/host/frontend.cpp` | host test for the helper (accented names, an unbroken word); pictures with long titles; the allocation bound of 3.6 |
| 7.2 | **FE-015**: a height budget per view | `ps5/host/frontend.cpp` | pictures: a 3-line title with 3 probes in HOME and RACK, at 1080p and 4K (IMP-12) |
| 7.3 | **FE-052**: labels clipped without heap allocation or baked once | `ps5/host/board.cpp` | host test: the allocation bound; pictures unchanged |

## Phase 8: improvements and what is left

In the order of [IMPROVEMENTS.md](IMPROVEMENTS.md); each takes the file lane free at the time.

| Step | Does | Files | Verified by |
| --- | --- | --- | --- |
| 8.1 | IMP-03: view state kept across games | `ps5/host/frontend.h`, `ps5/host/frontend.cpp`, `ps5/host/menu.cpp` | host test: the second `runMenu` opens in the view, sort and filter of the first |
| 8.2 | IMP-04 with **FE-053**: decoded board released after upload; the asset question measured | `ps5/host/menu.cpp`, `ps5/host/image.cpp` | host test: resident memory after the upload; console: the time from quitting a game to the list |
| 8.3 | IMP-05, IMP-19, IMP-20, IMP-22: BENCH's model built once, settled covers not asked again, unused decoded covers released, no per-frame strings | `ps5/host/frontend.cpp`, `ps5/host/menu.cpp`, `ps5/host/web.cpp` | host test: the allocation bound per view; peak memory in the cover scenario |
| 8.4 | IMP-14, IMP-18: scanning and copy status in the header; repeats in the quick menu and the keyboard | `ps5/host/frontend.cpp`, `ps5/host/menu.cpp`, `ps5/host/quickmenu.cpp` | pictures of the scanning and copying states; host test for the repeats |
| 8.5 | IMP-12: a 4K picture run | `ps5/tests/test_menu.cpp` | pictures at 3840x2160 |
| 8.6 | IMP-23: a stop flag and a join for the worker threads | `ps5/host/library.cpp`, `ps5/host/web.cpp`, `ps5/host/menu.cpp`, `ps5/host/main.cpp` | host tests exit cleanly under ASan |
| 8.7 | **FE-063** (suspected): leave the menu with an error after repeated acquire failures | `ps5/host/menu.cpp`, `ps5/host/main.cpp` | host test with a stand-in acquire that fails; console-unverified |
| 8.8 | IMP-10, IMP-21: partial atlas updates; fingerprints and the stage light from cached textures. Only if a console shows a frame-time problem | `vendor/gopher64/parallel-rdp/interface.cpp`, `ps5/host/quickmenu.cpp`, `ps5/host/frontend.cpp` | host test: upload and vertex counts; console for the effect |

Not scheduled: IMP-07 and IMP-15 beyond the parts named in their entries; the rest of IMP-02
(uploads recorded into the frame's command buffer) until a console shows upload stalls after 1.2.

## Who holds which file

The order in which steps take each file. A step waits for the one before it in the same row.

| File | Steps, in order |
| --- | --- |
| `ps5/host/menu.cpp` | 1.1 -> 1.2 -> 1.3 -> 1.5 -> 3.2 -> 3.8 -> 4.1 -> 4.2 -> 4.3 -> 4.4 -> 8.1 -> 8.2 -> 8.3 -> 8.4 -> 8.6 -> 8.7 |
| `ps5/host/frontend.cpp`, `frontend.h` | 1.2 -> 3.1 -> 3.2 -> 3.3 -> 3.4 -> 3.5 -> 3.6 -> 3.7 -> 3.8 -> 3.9 -> 4.2 -> 5.4 -> 5.5 -> 6.3 -> 7.1 -> 7.2 -> 8.1 -> 8.3 -> 8.4 -> 8.8 |
| `ps5/host/board.cpp` | 1.5 -> 3.9 -> 7.3 |
| `ps5/host/web.cpp` | 1.3 -> 5.2 -> 5.3 -> 8.3 -> 8.6 |
| `ps5/host/image.cpp` | 1.4 -> 8.2 |
| `ps5/host/library.cpp`, `library.h` | 3.6 -> 4.4 -> 5.1 -> 5.5 -> 5.6 -> 8.6 |
| `ps5/host/shares.cpp` | 4.3 |
| `ps5/host/quickmenu.cpp` | 2.2 -> 4.2 -> 5.5 -> 6.1 -> 6.2 -> 6.3 -> 6.4 -> 8.4 -> 8.8 |
| `ps5/host/bench.c` | 6.4 -> 6.5 |
| `ps5/host/platform.c` | 2.3 |
| `ps5/host/main.cpp` | 6.4 -> 8.6 -> 8.7 |
| `ps5/sdl3/psgopher_sdl3.cpp` | 2.1 -> 2.2 -> 6.5 |
| `vendor/gopher64` (one export per phase) | 2.4 -> 5.3 -> 5.4 -> 5.5 -> 6.6 -> 8.8 |
| `ps5/tests/*` | 0.1 -> 0.2 -> 0.3, then each step adds its own check with its fix |

## What to tell the owner after each phase

Each phase ends with the list of what only a console can show. Collected here so nothing is
reported as tested that was not:

- Phase 1: covers past the thirteenth; RACK then BENCH without a crash; the boot log's VkResult lines.
- Phase 2: every step (two controllers, rumble, disconnect and PS button, the hotkey slot).
- Phase 3: a real share copy with the probe picker (3.2); the scroll feel at 119.88 Hz (3.9).
- Phase 4: the mount of an unreachable server (4.3).
- Phase 5: `config.json` untouched by opening the list (5.4); probes of regional copies (5.5).
- Phase 6: the OSD's figures after Resume (6.4); state files after Take (6.6).
- Phase 8: the time from a game back to the list (8.2).
