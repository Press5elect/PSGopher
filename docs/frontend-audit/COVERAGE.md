# Front end audit coverage

Part of the front end audit of 2026-11-25. Index: [MASTER_PLAN.md](MASTER_PLAN.md). The ids are those
of the inventory in [ARCHITECTURE.md](ARCHITECTURE.md).

## The figure

**236 of 243 inventory items inspected: 97.1%.**

An item counts as inspected when at least one investigator reported having looked at it. That says
nothing about depth: section 3 lists the items that count as inspected but that one or more
dimensions set aside, and section 4 lists what no amount of reading on a PC can settle.

## 1. Not inspected by anyone (7)

| Id | What | Why it matters |
| --- | --- | --- |
| P-SLOT-LINE | The dashed line from the strip to the slot (`frontend.cpp:760`) | Drawing only; low risk |
| S-SCANNING-EMPTY | "Looking for cartridges" (empty and scanning) (`frontend.cpp:785`) | The first thing a new user sees; never pictured |
| S-NO-FACTS | A game without header facts (zip / 7z): dashes (`frontend.cpp:630`) | Never pictured |
| F-HEADER-STATUS | The header's counts per view (`frontend.cpp:528`) | Text correctness only |
| F-BENCH-ACTION-HINTS | "This console" hints: share count, sandbox, searching (`frontend.cpp:1323`) | The only scan indicator there is (IMP-14) |
| F-RACK-MARKS | Rack marks: played light, unplayed sheen, dimmed hidden (`frontend.cpp:1138`) | The dimming was checked against the design in passing (refuted part of visual #8); the marks themselves were not |
| X-TEST-LIBRARY | `ps5/tests/test_library.cpp` | The test source was not read; what it covers is taken from the plan |

## 2. Inspected, by kind

| Kind | Inspected | Total | Not inspected |
| --- | --- | --- | --- |
| Entry points (E-) | 8 | 8 | |
| Views (V-) | 9 | 9 | |
| Panels (P-) | 39 | 40 | P-SLOT-LINE |
| Pickers and dialogs (D-) | 3 | 3 | |
| Controls (C-) | 85 | 85 | |
| Input (I-) | 9 | 9 | |
| Threads (T-) | 6 | 6 | |
| Resources (R-) | 13 | 13 | |
| Models (M-) | 14 | 14 | |
| Overlay API (O-) | 8 | 8 | |
| States (S-) | 32 | 34 | S-SCANNING-EMPTY, S-NO-FACTS |
| Feedback (F-) | 7 | 10 | F-HEADER-STATUS, F-BENCH-ACTION-HINTS, F-RACK-MARKS |
| Tests (X-) | 3 | 4 | X-TEST-LIBRARY |
| **All** | **236** | **243** | **7** |

The inspected ids are every id in the inventory tables except the seven above.

## 3. Inspected, with gaps: what each dimension set aside

Each dimension had an assignment (see [MASTER_PLAN.md](MASTER_PLAN.md)); these are the parts each
reported as not inspected. An item here was looked at by another dimension unless the last column
says otherwise.

| Dimension | Declared not inspected | Covered elsewhere? |
| --- | --- | --- |
| Architecture | T-BENCH-LOGGER; D-KEYBOARD internals (`menu.cpp:674-750`); X-TEST-QUICKMENU source; X-TEST-LIBRARY source; R-IMAGE-CACHE; M-LIVE-STATS | Yes, except X-TEST-LIBRARY: overlay (logger, live stats), visual and input (keyboard), performance (image cache) |
| Visual | V-QM-TAKE, V-QM-LOAD, V-OSD, P-OSD-GRAPH, P-QM-DIM, R-TEX-COVERS, R-TEX-BOARD, R-FONTS, M-MACHINE, S-COVER-UPLOAD-FAILED, S-NO-ART, S-COPYING; a 3840x2160 render; how description text with newlines or non-Latin scripts is drawn | The code was read by other dimensions. **Visually, nobody looked at:** the OSD (no picture exists), the two quick-menu sub-pages beyond the existing pictures, the cover-failed, no-art and copying states, 4K on the board, and untrusted description text on screen |
| Input | V-OSD; C-EDIT-FIND, C-EDIT-FOUND-USE, C-EDIT-SAVE, C-EDIT-REMOVE read but not executed (the host stand-in has no shares or SMB); C-QM-*, I-RELEASE-GATE-QM, I-TAP-DETECT not re-run in a scratch quick-menu harness | Overlay ran the tap logic. **Never executed:** Find servers, use a found server, Save and Remove with a real mount |
| Performance | V-QM-MAIN and P-QM-LIVE-STRIP (cost inferred from the OSD measurement); R-PLAY-CACHE; M-WEB-FETCH (the TLS handshake per request, redirects); P-BOARD line by line | Robustness read M-WEB-FETCH statically. **R-PLAY-CACHE** (the whole ROM in memory, 256 MiB cap) has no finding and no measurement from any dimension |
| Robustness | S-SCANNING-EMPTY, S-SCANNING-BACKGROUND, S-NO-FACTS, S-FILTER-EMPTY, S-QM-NO-NAME, S-QM-SLOT-EMPTY, S-HDR-UNAVAILABLE, P-PANEL-ABOUT (embedded newlines), P-PANEL-FACTS, F-BOOT-LOG, X-TEST-LIBRARY | Input covered S-FILTER-EMPTY (FE-018); visual covered the facts panel. Two of these are in section 1. **Embedded newlines and arbitrary scripts in descriptions** were named as a risk in the plan and traced by nobody |
| Overlay | O-HDR and S-HDR-UNAVAILABLE statically only; interlaced output while paused statically only | No. See section 4 |

Entries that rest on reading the code alone, run by no one (34 of the 64): FE-005,
FE-008, FE-011, FE-012, FE-015 to FE-017, FE-020 to FE-022, FE-028, FE-029, FE-031, FE-033 to
FE-037, FE-041, FE-042, FE-044, FE-049, FE-050, FE-054 to FE-064. The evidence type of every entry
is in [BUGS.md](BUGS.md).

## 4. What only a console can show

Nothing in this audit was verified on a PS5. The host runs used lavapipe and stand-ins for the core,
the pad service, the file system and the network. These questions stay open until a console answers
them:

**Graphics (RADV, VideoOut)**

- Whether the descriptor pool limit bites as the RADV source says, and whether the assert in
  `ImDrawCmd::GetTexID` aborts the title (FE-001). Lavapipe enforces no pool size; the host
  reproduction emulated the accounting.
- Whether a freed descriptor set is ever drawn (FE-033): no validation layers on the host.
- Upload and eviction stalls per cover and the time to re-upload the board (FE-003, FE-053): the
  host figures are lavapipe's.
- Out-of-memory behaviour for an oversized picture and failed allocations (FE-030, FE-031).
- Acquire failures and display start failures (FE-063).
- The menu's pacing at 59.94 against 119.88 Hz and the scroll easing (FE-054).
- The HDR10 layer path: how the overlay's alpha is composited over the game. Reviewed statically,
  no double alpha found (the overlay layer starts at 0 and is blended to premultiplied colour, and
  the packing shader composites `m.rgb + p * (1 - m.a)`); not observed.
- Interlaced output while the quick menu repeats the scanout of one field: no drift expected by
  reading; not observed.
- GPU cost of the overlay draw and of the OSD.

**Pad service**

- What a disconnected or shell-intercepted pad reports, and so whether held buttons persist (FE-021).
- Whether an idle second controller yields a sample on every poll (FE-004's reproduction assumes it).
- Whether vibration holds until changed after one call (FE-022).
- Whether the monotonic clock keeps counting through rest mode (FE-024).

**Emulation thread and audio**

- Rumble, audio underrun counts, the live figures and the speed limiter across a quick-menu pause
  (FE-022, FE-058, FE-059). By reading, the limiter does not burst after the pause.
- The window in which a state file is half written (FE-064).
- The hotkey slot after a quick-menu Take (FE-005): certain from the code, never observed.

**Storage and network**

- Share copy, SMB and NFS mount timing and timeouts, the search for servers (FE-008, FE-029).
- Whether the scan and web threads disturb a running game (the suspected part of FE-029).
- Real answers from the cover and description hosts: redirects, cut bodies, rate limits (FE-055).
- `config.json` and `psgopher.ini` write timing and failures (FE-028, FE-056).

**Exit**

- Whether Close during a copy or scan crashes (FE-035).

## 5. Limits of the host evidence

- `test_menu` runs at 1920x1080 with 15 short synthetic names and one cover. The cheat picker, the
  OSD, an empty or scanning library, a copy, and every failure state are never rendered by the
  shipped tests. The reproductions in [BUGS.md](BUGS.md) came from scratch harnesses outside the
  repository, which are not kept.
- A passing run proved little: the blank rack spines (FE-013) were in a picture of a green run.
- The pictures folder mixes the current set with stale pictures of an earlier interface (FE-038).
  The current set is `1-home`, `2-home-moved`, `3-rack`, `4-rack-sorted-filtered`, `5-bench`,
  `6-bench-changed`, `7-bench-console`, `8-shares`, `9-share-form`, `10-keyboard`,
  `11-keyboard-typed`, `12-share-form-filled`, `13-home-probes` and `quickmenu/1-quickmenu`,
  `2-take`, `3-load`. Anything else in the folder is old.
- Performance figures are from a desktop CPU at -O2; they rank costs, they do not predict console
  frame times.
- One premise of the plan was wrong and was corrected by the overlay investigator: pad polling does
  not stop while the quick menu is up (see [ARCHITECTURE.md](ARCHITECTURE.md), section 6).
