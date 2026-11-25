# Front end audit: master plan

Audit of the PSGopher front end, 2026-11-25. Read-only: no source, test or build file was changed.
This folder is the whole output.

| Document | What it holds |
| --- | --- |
| [MASTER_PLAN.md](MASTER_PLAN.md) | This file: the plan, who did what, what is done and what is open |
| [ARCHITECTURE.md](ARCHITECTURE.md) | How the front end is built, as the audit found it; the inventory of 243 items |
| [BUGS.md](BUGS.md) | 64 defects (FE-001 to FE-064), by severity |
| [IMPROVEMENTS.md](IMPROVEMENTS.md) | 24 proposals (IMP-01 to IMP-24) |
| [COVERAGE.md](COVERAGE.md) | What was inspected, what was not, what only a console can show |
| [ROADMAP.md](ROADMAP.md) | The order of the fixes, who holds which file, how each step is verified |

Each fact is kept in one of these and linked from the others.

## 1. Result in brief

- **Coverage:** 236 of 243 inventory items inspected, 97.1% ([COVERAGE.md](COVERAGE.md)).
- **Defects:** 64, of which 60 confirmed and 4 suspected ([BUGS.md](BUGS.md#summary)).

  | Severity | Confirmed | Suspected | Entries |
  | --- | --- | --- | --- |
  | P0 | 0 | 0 | |
  | P1 | 1 | 0 | [FE-001](BUGS.md#fe-001) |
  | P2 | 4 | 0 | [FE-002](BUGS.md#fe-002) to [FE-005](BUGS.md#fe-005) |
  | P3 | 27 | 1 | [FE-006](BUGS.md#fe-006) to [FE-033](BUGS.md#fe-033) (suspected: FE-033) |
  | P4 | 28 | 3 | [FE-034](BUGS.md#fe-034) to [FE-064](BUGS.md#fe-064) (suspected: FE-062, FE-063, FE-064) |

- **Evidence:** 30 entries reproduced or measured on the PC, 34 from reading the code only, none
  verified on a console.
- **The one P1:** the menu's descriptor pool holds 16 images against a cover cache of 48. By the
  RADV source, covers past about the thirteenth never appear, and in a host run with the pool limit
  emulated a later font atlas growth aborted the title (open RACK with 14 or more covered
  cartridges, then BENCH). Unverified on a console.
- **First five steps of the roadmap:** FE-001 with FE-031, FE-003, FE-002, FE-004, FE-005
  ([ROADMAP.md](ROADMAP.md)), after three test additions that make them checkable.

## 2. The question and the scope

Find what is wrong, fragile or slow in the front end, say how sure each finding is, and put the
repairs in an order.

In scope: `ps5/host/` (`frontend`, `board`, `menu`, `quickmenu`, `web`, `image`, `library`,
`title_settings`, `shares`, `main`, `bench.c`, `platform.c` for the pad), `ps5/sdl3/psgopher_sdl3.cpp`
for the tap and the hotkeys, the PSGOPHER blocks of `vendor/gopher64/parallel-rdp/interface.cpp`,
`vendor/gopher64/src/psgopher.rs`, the host tests and their pictures, the design reference
`ps5/art/live-board.html` and the README's description of the controls.

Out of scope: Gopher64's emulation, RADV, the build and release tools.

The front end is immediate mode. The plan translated the usual UI audit questions accordingly:
work done every frame; cached state and what invalidates it; what the `std::function` host
callbacks capture; threads touching shared state; objects that outlive a frame, a screen or a menu
visit; texture lifetimes.

## 3. Method

1. **Plan** (the planner): read-only reconnaissance, the architecture summary, an inventory of 243
   items (entry points, views, panels, pickers, controls, input, threads, resources, models, overlay
   API, states, feedback, tests), 14 named risks, and an assignment of inventory ids to six
   dimensions. Many ids were given to several dimensions on purpose.
2. **Investigate** (one investigator per dimension): read the assigned code, look at the pictures,
   and where a claim could be run, run it in a scratch folder outside the repository against copies
   of the test sources, the prebuilt ImGui objects and lavapipe. Each reported findings,
   improvements and what it did not inspect.
3. **Verify** (one independent verifier per dimension): re-read the cited code for every finding,
   re-run or rebuild the reproductions, and give a verdict (confirmed, suspected, refuted) and its
   own severity.
4. **Merge** (the planner): one entry per defect across dimensions, the verifier's severity, the
   documents in this folder.

Rules everyone worked under: nothing under the repository is modified; the shared test runner and
build tools are not run; a stylistic preference is not a defect; static findings are kept apart
from runtime-verified ones; anything only a console shows is marked so; cartridges are named by id.

## 4. Agents

> Scratch folders are written as `SCRATCH/...`: the machine-local folder the audit's harnesses ran
> in; none of them is in the repository.

| Role | Dimension | Verifier ran |
| --- | --- | --- |
| Planner and editor of this folder | all | n/a |
| Investigator | Architecture | yes |
| Investigator | Visual | yes |
| Investigator | Input | yes |
| Investigator | Performance | yes |
| Investigator | Robustness | yes |
| Investigator | Overlay (quick menu, OSD) | yes |
| Independent verifier, one per dimension (six) | each of the above | n/a |

No dimension came back without results.

### What each dimension was given

| Dimension | Focus | Inventory kinds assigned |
| --- | --- | --- |
| Architecture | Ownership and lifetimes, state against asynchronous list replacement, the host callbacks, duplicated rules, threads at exit | entries, the board views and pickers, all threads, resources and models, the tests, the controls that start a game or leave the menu |
| Visual | The pictures against the code and the design reference: text fit, layout budgets, states with and without content, hint bars | all views, panels, pickers, states and feedback, the fonts and textures, the BENCH rows that change the board |
| Input | Every control and both input models: edges, repeats, wraps, focus, what happens on a press in every state | all controls, input items, hint bars, the pickers, the quick menu's controls |
| Performance | Work per frame against library size and text length, uploads and waits, allocations, the web thread's traffic, the cost of returning to the menu | entries, threads, resources, the board's panels, the overlay calls, the settings and web models |
| Robustness | Untrusted input, failure paths, unchecked results, negative caching, races, files | threads, resources, models, all states, the share form, the keyboard |
| Overlay | The quick menu and OSD on the emulation thread: the texture protocol, the answer to the core, the tap, the pause, the live figures | the quick menu views and controls, the overlay API, the tap and release gates, the live statistics |

The exact id lists are those of the plan; which ids ended up inspected is in
[COVERAGE.md](COVERAGE.md).

## 5. The plan's risks and where they landed

| # | Risk named before the investigation | Outcome |
| --- | --- | --- |
| 1 | Cover textures: a request per spine, one blocking upload per frame, an LRU that needs unseen entries, a 16-image pool, an id cached in `Machine::shown` | Confirmed: FE-001 (P1), FE-002, FE-003; FE-033 suspected |
| 2 | Pickers and index-based selection against asynchronous list replacement; the cheat picker surviving L1/R1 | Confirmed: FE-006, FE-007, FE-012, FE-018. Narrower than feared for wrong writes: the core rejects a cheat name the target game lacks |
| 3 | Share copy lifetime: global job, `load_state` written early, no busy check in the probe picker, no cancel, a replaceable notice | Confirmed by reading: FE-008, FE-035; the notice and the missing indicator are IMP-14. Not executed: the host has no share |
| 4 | State that outlives the per-menu `Frontend` against state rebuilt on every return | Confirmed: FE-002, FE-010, FE-036 on one side; FE-053 and IMP-03 on the other |
| 5 | Blocking the emulation thread for the quick menu | Confirmed by reading: FE-022, FE-058, FE-059. The speed limiter does not burst. The premise that events are not pumped was wrong (see [ARCHITECTURE.md](ARCHITECTURE.md), section 6) and that correction explains FE-004 |
| 6 | One save slot variable shared by the quick menu, the hotkeys and `--load-state`; two slot ranges; two identity rules | Confirmed: FE-005 (P2), FE-009 |
| 7 | The overlay's resource protocol across contexts and games | FE-032 confirmed. No finding on borrowed vertex pointers or on the OSD context's textures at release; the HDR alpha path was read and nothing found. Open: see section 9 |
| 8 | Network negative caching | Confirmed: FE-026, FE-055 |
| 9 | Untrusted pictures and text reaching the UI thread unchecked | Confirmed: FE-030, FE-031, FE-040. Description text with newlines or other scripts was traced by nobody: open |
| 10 | Input timing that depends on the platform | FE-024, FE-021, FE-054 confirmed by reading or measurement; the same-frame hand-over to ImGui shows as FE-023 and FE-045 |
| 11 | The scan thread against share management | Confirmed: FE-011, FE-029. Unmounting during a scan is memory-safe (reference-counted mounts) |
| 12 | Settings writers | Confirmed: FE-028, FE-056 |
| 13 | Layout without a vertical budget; unbounded notices and picker titles | Confirmed by arithmetic: FE-015, FE-016; FE-062 suspected |
| 14 | Test evidence that can mislead | Confirmed: FE-038, and FE-013 passed a green run; IMP-13 |

Found without having been named as a risk: FE-004 (two controllers), FE-013 (blank spines), FE-019
and FE-020 (a wrap onto an unconfirmed exit), FE-023 (lost ImGui focus), FE-025 and FE-027
(per-frame cost growing with the library).

## 6. Tasks

### Completed

- [x] Reconnaissance, architecture summary, inventory of 243 items, 14 risks, assignments.
- [x] Six investigations (architecture, visual, input, performance, robustness, overlay): 86
      findings, 31 improvement proposals, a not-inspected list each.
- [x] Six independent verifications: 80 findings confirmed, 4 suspected, 2 refuted; 42 severities
      lowered and 1 raised (the descriptor pool, from P2 to P1 after the verifier's run ended in an
      abort).
- [x] Merge into 64 defect entries and 24 proposals; coverage computed; roadmap sequenced by
      severity and by file.
- [x] The six documents of this folder.

### Outstanding

- [ ] **No fix has been made.** Everything in [ROADMAP.md](ROADMAP.md) is still to do.
- [ ] **No console check.** Every item in [COVERAGE.md](COVERAGE.md), section 4.
- [ ] The scratch harnesses that produced the reproductions are outside the repository and are not
      kept; the scenarios have to be written into `ps5/tests` (roadmap phase 0 and IMP-13).
- [ ] Seven inventory items inspected by nobody, and the gaps of [COVERAGE.md](COVERAGE.md),
      section 3.
- [ ] Two decisions that are the owner's: what RACK seats and how its panel is worded (IMP-11), and
      who may open and drive the quick menu with several controllers (FE-049).
- [ ] `docs/README.md` does not yet link this folder, and `docs/dev/open-work.md` does not yet carry
      the console-unverified items (the audit could write only here).
- [ ] The model families and the scratch root in section 4 are to be removed before this folder is pushed.

## 7. Dependencies between the defects

Fix order follows from these; [ROADMAP.md](ROADMAP.md) applies them.

| This | Depends on, or is made worse by | Why |
| --- | --- | --- |
| FE-001 | FE-003 | RACK's spine requests fill the 16-image pool in one visit; without them only the strip and the panel ask |
| FE-001 | FE-031 | The null-set handling and the unchecked Vulkan results are in the same function |
| FE-002 | FE-003, IMP-20 | Forgetting covers at teardown means uploading them again on each return; that is only cheap once covers are requested where drawn, and it decides where the decoded pixels live |
| FE-026 | FE-003 | The offline re-queue runs once per requested item; RACK multiplies it by the spines |
| FE-033 | FE-003, IMP-02 | The usual trigger is RACK's uploads during the un-seat; the real cure is deferred frees |
| FE-047 | FE-006, FE-007 | A rejected toggle is reachable in practice only through a stale picker |
| FE-008 | IMP-08 | The clean fix is a result decided in one place |
| FE-010, FE-036 | FE-009, IMP-06 | One probe API replaces the stale set and the stale fingerprint cache |
| FE-005 | FE-009 | Slot ranges and identity are one design question |
| FE-049 | FE-004 | Both need the tap state per player |
| FE-058, FE-059 | FE-022, IMP-24 | One pair of pause hooks serves all three |
| FE-019, FE-020 | IMP-16 | One confirmation pattern |
| FE-016, FE-040, FE-051, FE-062 | each other | One fit-to-width helper on UTF-8 boundaries |
| FE-015 | FE-016 | A height budget needs titles that are measured |
| FE-027 | FE-025 | Measure the rack's own cost after the list copy is gone |
| FE-056 | FE-028 | Saving on release changes where a failed save is reported |
| FE-018, FE-042 | each other | The empty-filter and empty-rack texts are one branch |
| FE-023, FE-045 | IMP-15 | Both come from two consumers of one D-pad edge |
| Every fix | IMP-13, FE-038 | A check that fails first; a pictures folder that shows only the current run |

## 8. Verification status

| Dimension | Findings | Confirmed | Suspected | Refuted | Runtime-verified by the verifier |
| --- | --- | --- | --- | --- | --- |
| Architecture | 16 | 15 | 1 | 0 | 5 |
| Visual | 13 | 11 | 1 | 1 | 4 |
| Input | 17 | 17 | 0 | 0 | 9 |
| Performance | 12 | 12 | 0 | 0 | 8 |
| Robustness | 16 | 14 | 1 | 1 | 5 |
| Overlay | 12 | 11 | 1 | 0 | 2 |
| **All** | **86** | **80** | **4** | **2** | **33** |

- "Runtime-verified" counts confirmed or suspected findings for which the verifier recorded a run,
  a rebuilt harness, or a current host picture as evidence. Both refutations were checked at
  runtime too (a picture; a scan of the compiled-in cheat database). After merging, 30 of the 64
  entries carry host-runtime evidence: 29 that a verifier ran, and FE-038, whose evidence is the
  listing of the pictures folder.
- The verifier ran for every dimension, so no finding is suspected for lack of verification. The
  four suspected entries are suspected on their merits: narrow conditions (FE-033), no realistic
  input shown (FE-062), a path the console's window system makes practically unreachable (FE-063), a
  window of about a frame (FE-064).
- One finding's host reproduction depends on an emulated limit: FE-001's pool accounting. The limit
  itself is read from the RADV source.
- Refuted: the BENCH card's bottom margin and callout lines (a layout choice), and a panic in the
  cheat text cut (unreachable with the compiled-in database). Parts of six other findings were
  refuted; all are listed at the [end of BUGS.md](BUGS.md#refuted).
- Console: nothing verified.

## 9. Next investigation tasks

In order of value.

1. **A console session for the P1 and the P2s.** FE-001 (14 or more covered cartridges, RACK, then
   BENCH; read the boot log), FE-004 (two controllers, hold the touch pad), FE-005 (Take Probe 3,
   then touch pad + L1, look at the state files), then FE-022 and FE-021. These five answers decide
   most of the roadmap's first two phases.
2. **Run the share paths.** A stand-in `ps5vfs` for the host test that can mount, stall and copy, so
   FE-008, FE-011, FE-029 and FE-035 are executed instead of read, and Find servers, Save and
   Remove are exercised at all.
3. **Look at what nobody looked at.** An OSD picture at 1080p and 4K and a visual review of it; the
   board at 3840x2160; the scanning, empty, copying, cover-failed and no-art states; the seven
   uninspected items.
4. **Untrusted description text on screen.** Embedded newlines, very long words, scripts the four
   embedded faces do not cover, byte limits that split a character at the bridge (`copy_out` cuts
   by bytes).
5. **The font atlas against the pool after FE-001's fix.** How many atlas textures the menu creates
   at 1080p and at 4K, and when it grows, so the pool's headroom is a number and not a guess.
6. **FE-033 with validation.** A host build with the Vulkan validation layers and many covers, to
   show or rule out a draw with a freed set.
7. **`R-PLAY-CACHE`.** The local copy of a share ROM: the whole file in memory up to 256 MiB, what
   happens above the cap, on a short read, on a full disk, and when the cache is cleaned.
8. **The overlay's remaining protocol questions.** The lifetime of the vertex pointers handed to
   `psgopher_overlay_set`, and whether the OSD context's textures are freed before the interface
   releases its own at game end.
9. **A thread sanitizer pass** over the web, scan, copy and find-servers threads with the host
   stand-ins, including exit while they run.
10. **The fetch code against a local stand-in server**: redirects, cut bodies, chunked encodings,
    slow answers (FE-055).
11. **The same-frame hand-over of Cross** from BENCH's "Network shares" into the ImGui page (the
    inventory notes that the page opens in the same frame): whether the press can also activate the
    page's first row.
12. **Read `test_library.cpp`** and list what it really covers, before phase 5 leans on it.
