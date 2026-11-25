# Front end defects

Part of the front end audit of 2026-11-25. Index: [MASTER_PLAN.md](MASTER_PLAN.md). The order in
which to fix these is in [ROADMAP.md](ROADMAP.md); component ids are those of the inventory in
[ARCHITECTURE.md](ARCHITECTURE.md).

## How to read this register

- **One entry per defect.** The six investigators reported 86 findings; 84 survived verification
  and, with duplicates across dimensions merged, make the 64 entries below. The two refuted findings
  and the refuted parts of others are at the [end](#refuted).
- **Severity** is the independent verifier's, not the investigator's. Where two verifiers rated the
  same defect differently the higher rating is used and the other is noted. P0 = data loss or a
  crash in normal use; P1 = a crash or a broken feature for most users; P2 = a feature visibly wrong
  in ordinary use; P3 = wrong under a specific but reachable condition; P4 = minor or cosmetic.
- **Status**: *Confirmed* = the verifier found the defect in the code (and, where stated, reproduced
  it); *Suspected* = the mechanism exists but the conditions were not shown to be reachable.
- **Evidence type**: *static* = from reading the code; *host-runtime* = reproduced on the PC
  (lavapipe, stand-ins for the core) in a scratch build outside the repository;
  *console-unverified* = the visible effect depends on the console (RADV, VideoOut, the pad
  service, uptime) and no console was reachable. Nothing in this register was verified on a console.
- **Sources** name the dimension and the finding's index in that dimension's report.
- Paths written `SCRATCH/...` are the scratch folders that held the reproductions (the root is
  given in [MASTER_PLAN.md](MASTER_PLAN.md), section 4); they are not part of the repository and
  may no longer exist.
- Test cartridges are named by their key or their position, never by title.

## Summary

| Severity | Confirmed | Suspected | Total |
| --- | --- | --- | --- |
| P0 | 0 | 0 | 0 |
| P1 | 1 | 0 | 1 |
| P2 | 4 | 0 | 4 |
| P3 | 27 | 1 | 28 |
| P4 | 28 | 3 | 31 |
| **All** | **60** | **4** | **64** |

Evidence: 30 entries have host-runtime evidence (a reproduction, a measurement, or an observation
of the host test's output), 34 are static only.

| Id | Sev | Status | Title |
| --- | --- | --- | --- |
| [FE-001](#fe-001) | P1 | Confirmed | Descriptor pool of 16 against a cover cache of 48: covers vanish, then a font atlas growth aborts |
| [FE-002](#fe-002) | P2 | Confirmed | Covers never come back after the first game |
| [FE-003](#fe-003) | P2 | Confirmed | RACK uploads a cover for every visible spine; the LRU never enforces its limit |
| [FE-004](#fe-004) | P2 | Confirmed | With two controllers, holding the touch pad opens the quick menu |
| [FE-005](#fe-005) | P2 | Confirmed | A quick-menu Take/Load moves the hotkey save slot |
| [FE-006](#fe-006) | P3 | Confirmed | The cheat picker survives L1/R1 |
| [FE-007](#fe-007) | P3 | Confirmed | Pickers are not re-anchored when the list is replaced |
| [FE-008](#fe-008) | P3 | Confirmed | Probe picker Cross during a share copy rewrites `load_state` |
| [FE-009](#fe-009) | P3 | Confirmed | Probe identity differs between board, quick menu and core |
| [FE-010](#fe-010) | P3 | Confirmed | RACK "Has probes" filter is stale for the whole run |
| [FE-011](#fe-011) | P3 | Confirmed | Rescan requests are dropped while a scan runs |
| [FE-012](#fe-012) | P3 | Confirmed | RACK selection is index-based across a list refresh |
| [FE-013](#fe-013) | P3 | Confirmed | RACK spine codes are never drawn |
| [FE-014](#fe-014) | P3 | Confirmed | RACK panel shows HOME wording and a Square "Probes" pill |
| [FE-015](#fe-015) | P3 | Confirmed | The game panel has no vertical budget |
| [FE-016](#fe-016) | P3 | Confirmed | `wrap()` never breaks a word; picker titles are not limited |
| [FE-017](#fe-017) | P3 | Confirmed | The keyboard's text field cannot show a long value's end |
| [FE-018](#fe-018) | P3 | Confirmed | RACK: a filter that hides everything leaves a live hidden selection |
| [FE-019](#fe-019) | P3 | Confirmed | BENCH: Up from the first row lands on "Close PSGopher", one Cross closes |
| [FE-020](#fe-020) | P3 | Confirmed | Quick menu: Up from Resume lands on Eject; Reset and Eject need one Cross |
| [FE-021](#fe-021) | P3 | Confirmed | Held buttons stay held after a disconnect or shell intercept |
| [FE-022](#fe-022) | P3 | Confirmed | Rumble is left on during the quick menu and after Eject or Reset |
| [FE-023](#fe-023) | P3 | Confirmed | ImGui pages lose the cursor (empty Shares list, keyboard cancel) |
| [FE-024](#fe-024) | P3 | Confirmed | `PadInput` keeps time in float seconds since boot |
| [FE-025](#fe-025) | P3 | Confirmed | Every frame copies the whole game list |
| [FE-026](#fe-026) | P3 | Confirmed | Offline: every visible item is re-queued every frame |
| [FE-027](#fe-027) | P3 | Confirmed | RACK per-frame cost grows with the whole library |
| [FE-028](#fe-028) | P3 | Confirmed | Setting reads rewrite `config.json`; held steppers rewrite files 11 times a second |
| [FE-029](#fe-029) | P3 | Confirmed | Share mount and unmount block the UI thread |
| [FE-030](#fe-030) | P3 | Confirmed | A PNG's declared size is not limited before decode and upload |
| [FE-031](#fe-031) | P3 | Confirmed | `uploadTexture` ignores allocation, bind and map results |
| [FE-032](#fe-032) | P3 | Confirmed | Overlay texture ids are never reused and stop at 4096 |
| [FE-033](#fe-033) | P3 | Suspected | A cover evicted in the frame that still draws it (`Machine::shown`) |
| [FE-034](#fe-034) | P4 | Confirmed | The board keeps the last cartridge seated when nothing is selected |
| [FE-035](#fe-035) | P4 | Confirmed | Detached copy and scan threads can outlive their statics at exit |
| [FE-036](#fe-036) | P4 | Confirmed | The probe fingerprint cache is never invalidated |
| [FE-037](#fe-037) | P4 | Confirmed | "yesterday" for anything under 48 hours, and for future times |
| [FE-038](#fe-038) | P4 | Confirmed | The host pictures folder mixes stale and current pictures |
| [FE-039](#fe-039) | P4 | Confirmed | "1 cartridges" |
| [FE-040](#fe-040) | P4 | Confirmed | Name truncation cuts bytes and costs an allocation per removed byte |
| [FE-041](#fe-041) | P4 | Confirmed | Region A reserves flag space but draws no flag |
| [FE-042](#fe-042) | P4 | Confirmed | No letter-index column for digit titles; no empty-rack message |
| [FE-043](#fe-043) | P4 | Confirmed | Hint bars do not match the controls |
| [FE-044](#fe-044) | P4 | Confirmed | Quick menu title is clipped without an ellipsis |
| [FE-045](#fe-045) | P4 | Confirmed | Share form: Left/Right on Kind do not toggle |
| [FE-046](#fe-046) | P4 | Confirmed | RACK L2 cannot step back past a fully hidden letter |
| [FE-047](#fe-047) | P4 | Confirmed | A rejected cheat toggle stays shown as changed |
| [FE-048](#fe-048) | P4 | Confirmed | BENCH option rows wrap on Left/Right |
| [FE-049](#fe-049) | P4 | Confirmed | Any player's tap opens the quick menu, only player 1 drives it |
| [FE-050](#fe-050) | P4 | Confirmed | RACK Up with Down still held moves down |
| [FE-051](#fe-051) | P4 | Confirmed | The panel wraps the whole description every frame |
| [FE-052](#fe-052) | P4 | Confirmed | Procedural labels allocate about 220 times each per frame |
| [FE-053](#fe-053) | P4 | Confirmed | Every return to the menu re-uploads the board; 40 MB stays decoded |
| [FE-054](#fe-054) | P4 | Confirmed | Strip and rack scrolling ease by a fixed 1/60 s step |
| [FE-055](#fe-055) | P4 | Confirmed | Transient fetch problems cached as "none" for 30 days |
| [FE-056](#fe-056) | P4 | Confirmed | Settings write failures are invisible |
| [FE-057](#fe-057) | P4 | Confirmed | `library.tsv` and `played.tsv` parsing gaps |
| [FE-058](#fe-058) | P4 | Confirmed | The OSD's underrun count is cumulative and counts the pause |
| [FE-059](#fe-059) | P4 | Confirmed | The quick-menu pause is counted as one frame |
| [FE-060](#fe-060) | P4 | Confirmed | The benchmark logger reads the audio stream pointer unsynchronised |
| [FE-061](#fe-061) | P4 | Confirmed | The OSD re-reads `psgopher.ini` only if it was drawn in the last game |
| [FE-062](#fe-062) | P4 | Suspected | The notice pill's width is unbounded |
| [FE-063](#fe-063) | P4 | Suspected | Menu display failures: endless acquire loop, silent exit |
| [FE-064](#fe-064) | P4 | Suspected | States are written asynchronously; the quick menu can show a slot stale |

---

## P1

### FE-001

**Descriptor pool of 16 against a cover cache of 48: covers past about 13 never appear, and a later font atlas growth aborts the title**

- **Where:** `ps5/host/menu.cpp:1228` (pool size), `:318-321` (`uploadTexture` returns the set unchecked), `:1321-1325` (the failure is cached), `:798` (`kCovers = 48`)
- **Component:** R-DESCRIPTOR-POOL, R-TEX-COVERS, S-COVER-UPLOAD-FAILED, R-FONTS
- **Severity:** P1 · **Status:** Confirmed · **Confidence:** high for the mechanism, medium for the console effect
- **Evidence type:** static (ImGui backend and RADV sources) + host-runtime with RADV-style pool accounting emulated on lavapipe; console-unverified
- **Description:** The ImGui Vulkan backend's own pool is created with `IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE + 8` = 16 sampled images (`maxSets` 18, two of them sampler sets). The font atlas, `board.png` and `logo.png` take 3, which leaves about 13 for covers while the cache allows 48. When the pool is full, `vkAllocateDescriptorSets` fails, `ImGui_ImplVulkan_AddTexture` returns a null set (`CheckVkResultFn` is not set, so nothing reports it), `uploadTexture` still keeps the image and its memory in `d.textures` and returns id 0, and `host.cover` caches that id as the cover. The backend sets `ImGuiBackendFlags_RendererHasTextures`, so the dynamic font atlas takes its textures from the same pool: when the atlas grows after the pool is full, its texture gets no set and `ImDrawCmd::GetTexID` asserts.
- **Repro:** A library with 14 or more cartridges whose covers download. Open RACK (R1) and wait a few seconds (FE-003 makes RACK fill the pool at once), then R1 to BENCH. In HOME alone, scrolling Right through more than about 13 covered cartridges reaches the first effect.
- **Expected:** Every downloaded cover is shown; a failed upload is retried or reported; the font atlas can always grow.
- **Actual:** Host run with pool accounting: 16 live sets, then failed cover uploads cached as "no texture" (procedural labels for the rest of the menu visit, each failed image leaked until the menu closes), then on entering BENCH `SIGABRT` in `ImDrawCmd::GetTexID` ("ImDrawCmd is referring to ImTextureData that wasn't uploaded") from `ImGui_ImplVulkan_RenderDrawData`. `ps5/tools/build.sh` compiles without `NDEBUG`, so the same assert is live in the console build.
- **Root cause:** The pool size is fixed far below the cover cache plus the fixed textures and the atlas pages; a null set is neither detected nor kept out of the cache.
- **Evidence:** `menu.cpp:1228`; `imgui_impl_vulkan.cpp:1187-1200` (pool), `:1495-1527` (`AddTexture`), `:1420` (`RendererHasTextures`); RADV `radv_descriptor_set.c:381-412` returns `VK_ERROR_OUT_OF_POOL_MEMORY` and `:537` nulls the handle. Investigator run: 15 valid uploads, then 27 null sets for 40 covers (also at 3840x2160). Verifier run (`SCRATCH/verify-robust/pool`, script extended RACK -> BENCH): the abort above with a gdb backtrace. Lavapipe itself does not enforce the pool (300 uploads into this pool succeeded without the accounting wrapper), so the stock host test cannot show it.
- **Remediation:** Size the pool from `kCovers` plus the board, the logo and headroom for atlas pages, or give covers their own pool. In `uploadTexture` treat a null set as a failure: destroy the image, view and memory, return `ImTextureID_Invalid`. In `host.cover` do not cache an invalid id (or cache it with a retry time). Set `CheckVkResultFn` so pool errors reach the boot log. Do FE-003 in the same change set.
- **Sources:** performance #0 (P1, static), robustness #0 (raised from P2 to P1, runtime). Related: FE-003, FE-031, FE-033.

## P2

### FE-002

**Covers never come back after the first game**

- **Where:** `ps5/host/web.cpp:242-249` (`webCover`), `:252-260` (`webForget`); `ps5/host/menu.cpp:1317` (the only caller of `webForget`), `:494-505` (`closeDisplay`)
- **Component:** R-WEB-ENTRIES, R-TEX-COVERS
- **Severity:** P2 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime
- **Description:** `webCover` hands a decoded cover over once (`cover_taken = true`, the picture moved out) on the assumption that the caller keeps it, and answers Pending from then on. The caller is `Menu::covers`, rebuilt on every `runMenu`, whose textures `closeDisplay` destroys. Only LRU eviction calls `webForget`; nothing does at teardown, and `webStart` is guarded so the entries are never reset.
- **Repro:** A cartridge whose cover is downloaded or cached shows it on the strip and in the slot. Cross (power on), then quit the game.
- **Expected:** Covers shown before the game are shown again after it.
- **Actual:** That cartridge, and every cartridge whose cover was shown before, shows a procedural label until PSGopher restarts (or until an eviction happens to forget the name).
- **Root cause:** Ownership split between a process-lifetime cache (web entries) and a per-menu cache (`Menu::covers`), with a one-way hand-over flag.
- **Evidence:** Scratch harness (a copy of `test_menu` with every cover answering 200, `ImGui_ImplVulkan_AddTexture` counted, `runMenu` called twice): menu 1 made 42 uploads; afterwards `webCover` for the first test cartridge returned Pending; menu 2 made 2 uploads (board, logo) over 60+ frames. Pictures `p1-home.png` (covers) against `p2-home.png` (procedural labels) in `SCRATCH/verify-arch/pics/`. Reproduced by the investigator and again by the verifier.
- **Remediation:** At menu teardown call `webForget` for every name in `Menu::covers` (or add a `webForgetAll()` called before `closeDisplay`). Alternatively keep the decoded image in the web entry and let `webCover` copy it out (see IMP-20 for the memory side).
- **Sources:** architecture #0.

### FE-003

**RACK requests and uploads a cover for every visible spine although spines never draw one; the LRU inserts even when nothing can be evicted**

- **Where:** `ps5/host/frontend.cpp:1116` (`lookFor` per spine), `:442-450` (`lookFor` always calls `host_.cover`); `ps5/host/menu.cpp:1310-1325` (conditional eviction, unconditional insert)
- **Component:** V-RACK, P-RACK-SHELVES, R-TEX-COVERS, R-DESCRIPTOR-POOL, T-WEB
- **Severity:** P2 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime; the pool consequence is console-unverified (FE-001)
- **Description:** `rack()` calls `lookFor(cg)` for every spine in the rows in view (up to 7 rows of 52). The spine uses only `look.hue` and `look.code`. Each call still asks the web thread for the cover and lets the UI thread upload one per frame (staging buffer, mip chain by blits, `vkQueueWaitIdle`). Every visible spine is stamped as shown each frame, so the eviction rule ("unseen for 2 frames") never fires and the map grows past `kCovers`.
- **Repro:** A library of more than 48 cartridges with covers available. R1 to RACK and wait.
- **Expected:** At most `kCovers` cover textures; covers requested only where a label is drawn (the strip, the slot, the panel).
- **Actual:** One blocking upload per frame for hundreds of frames; the cache reaches the number of spines on screen and stays there back in HOME.
- **Root cause:** A `const` query (`lookFor`) with side effects, and an insert that does not depend on the eviction succeeding.
- **Evidence:** Three independent runs: 90 cartridges, `AddTexture` 10 -> 92 during 200 RACK frames (architecture); 300 cartridges, covers 8 -> 300, "uploads 300 (failed 0) avg 1484 us max 3625 us; frees 0", maxrss 319 MB against 213-214 MB without covers, RACK draw median 1900 us while uploading against 377 us in HOME (performance verifier); 40 cartridges, 42 uploads after one RACK visit where the HOME strip shows at most 16 (robustness).
- **Remediation:** Split `lookFor` into a pure look (hue, code, seed) and an explicit cover request used only where a label is drawn (IMP-01). In `host.cover`, when the cache is full and nothing is evictable, skip the upload instead of inserting.
- **Sources:** architecture #4, performance #1, robustness #1 (all P2). Related: FE-001 (this is what fills the pool in one RACK visit), FE-026, FE-033.

### FE-004

**With two or more controllers connected, merely holding the touch pad opens the quick menu and breaks every touch-pad hotkey**

- **Where:** `ps5/sdl3/psgopher_sdl3.cpp:422-444` (`watch_tap` state), `:458-462` (called per player)
- **Component:** I-TAP-DETECT, C-QM-TAP-OPEN, C-GAME-HOTKEYS
- **Severity:** P2 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime (the real `psgopher_sdl3.cpp` against scripted pad readings); console-unverified
- **Description:** `watch_tap` keeps one set of statics (`tap_down`, `tap_clean`, `tap_since_us`) for all players, and `poll_pads` feeds it each player's readings in turn. An idle second controller's reading (touch == false) is taken as "the touch pad was let go", so a touch pad that is still held on the first controller counts as a clean short tap; the next poll starts a new "press", so it repeats on every poll.
- **Repro:** Two controllers signed in, a game running. Player 1 holds the touch pad to start a hotkey (touch pad + L1 to save, + Options to return to the board). Player 2 does nothing.
- **Expected:** No quick menu while the touch pad is held; the hotkey works; a tap is a press and release of the same controller's touch pad within 0.5 s.
- **Actual:** The quick menu opens in the middle of the hotkey, and again after it closes, for as long as the touch pad is held alone.
- **Root cause:** Per-player input fed through shared state: `else if (tap_down) { tap_down = false; if (tap_clean && ts - tap_since_us < 500000) tap_seen = true; }` runs on player 2's reading.
- **Evidence:** `SCRATCH/overlay-lifecycle/tap_test` output, re-run by the verifier: "1 controller(s): ... quick menu taps seen: 0" and "2 controller(s): ... quick menu taps seen: 5" over 11 polls. The stub assumes an idle connected controller yields at least one sample per poll (the usual behaviour, not checked on a console).
- **Remediation:** Keep the tap state per player (arrays indexed by player); have `psgopher_sdl3_menu_pad` use the same per-player state. Add a two-controller case to `test_sdl3`.
- **Sources:** overlay #0. Related: FE-049.

### FE-005

**A quick-menu Take or Load moves Gopher64's hotkey save slot: touch pad + L1/R1 then saves over, or loads, that probe**

- **Where:** `vendor/gopher64/parallel-rdp/interface.cpp:1048`; `vendor/gopher64/src/ui/video.rs:255-266`
- **Component:** O-ANSWER, C-GAME-HOTKEYS, C-QM-SLOT-SAVE, F-G64-MESSAGES
- **Severity:** P2 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** static; console-unverified
- **Description:** The quick menu's answer writes `callback.save_state_slot`, the only slot Gopher64's state hotkeys use, and nothing restores it. `quickmenu.cpp:70` states the opposite ("Gopher64's own slot 0 stays its hotkey's"). On the PS5 no hotkey changes the slot, so it stays where the menu left it for the rest of the game. A game started from a board probe N (`--load-state N`) has its hotkeys on slot N from the start.
- **Repro:** In a game: tap, Take a memory probe, Probe 3. Later press touch pad + L1 (the hotkey save the README documents).
- **Expected:** The hotkey saves to its own slot (0); Probe 3 stays as taken.
- **Actual:** "Switching savestate slot to 3" is shown; from then on every hotkey save overwrites probe 3 and every hotkey load loads it.
- **Root cause:** `callback.save_state_slot = uint32_t(std::clamp(answer.slot, 0, 9));` is persistent shared state, not a one-shot slot for the menu's action. The hotkeys (`input.rs:351-364`) only raise save/load events and use the current slot (`savestates.rs:183`).
- **Evidence:** `interface.cpp:1046-1053`; `video.rs:255-267`; `input.rs:351-365`; `README.md:40-41`; `quickmenu.cpp:70`. The only other writers of the slot are keyboard Alt+digit (`interface.cpp:434-436`) and `rdp_init` (650).
- **Remediation:** After a quick-menu save or load has run, put the slot back to its earlier value (keep it and restore it in the next `check_callback` after the latch), or give the menu a one-shot slot field that `process_savestates` uses. Suppress the "Switching savestate slot" message for menu actions. The change is in `vendor/gopher64` (export the patch) and cannot be checked on the PC.
- **Sources:** overlay #2. Related: FE-009 (slot ranges).

## P3

### FE-006

**The cheat picker survives L1/R1 and reopens in HOME for whatever cartridge is then selected**

- **Where:** `ps5/host/frontend.cpp:467` (guard), `:731-733` (HOME reopens it), `:916-919` (`save()`), `:1083-1089` (RACK Triangle)
- **Component:** D-CHEATS, C-GLOBAL-L1, C-GLOBAL-R1, C-RACK-TRIANGLE
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime
- **Description:** The view switch is guarded by `probes_open_ < 0` only. With the cheat picker open, R1/L1 change the view while `cheats_open_` stays set and `cheats_` keeps the first cartridge's list. The picker is invisible in RACK and BENCH, so another cartridge can be chosen there. Back in HOME the picker is drawn for the new `selected()` with the old list, and Cross calls `cheat_set(new key, old cheat name)`.
- **Repro:** HOME, cartridge A, Triangle (picker opens). R1 to RACK. Right twice, Triangle (Seat it) returns to HOME with cartridge B. Cross.
- **Expected:** L1/R1 do nothing while any picker is open, or close it.
- **Actual:** The picker is open again, titled B, listing A's cheats; Cross sends A's cheat under B's key.
- **Root cause:** `if (probes_open_ < 0) {` does not test `cheats_open_`; `cheats_` is not tied to a game.
- **Evidence:** Two independent harnesses, both re-run by their verifiers. One recorded `cheat_set(game=13000000-00000000-C:45, name=Cheat of 1A000000-00000000-C:45)`; the other `CHEATS_OF game=00000000-00000000-C:45` then `CHEAT_SET game=00000000-00000000-C:4A name=Infinite Lives on=0`. Pictures `p2-home-picker-back.png`, `c3-home-after-seat.png`. Severity lowered from P2: the real `psgopher_cheat_set` (`psgopher.rs:399-401`) rejects a name the target game does not have, so a wrong write needs the same cheat name in both games (sibling or regional ROMs); otherwise the result is a wrong list and a rejected toggle (FE-047).
- **Remediation:** `if (probes_open_ < 0 && cheats_open_ < 0)`, or close both pickers in `setView()`. Bind the picker to a game path (FE-007).
- **Sources:** architecture #2, input #0.

### FE-007

**Pickers are not re-anchored when the list is replaced: the open list is applied to whatever `selected()` becomes**

- **Where:** `ps5/host/frontend.cpp:379-410` (`refresh()`), `:726-733` (HOME hands the current `selected()` to the picker), `:831-835` (probe picker Cross), `:916-919` (cheat `save()`)
- **Component:** D-CHEATS, D-PROBES, C-PROBES-CROSS, M-FRONTEND-STATE
- **Severity:** P3 (cheat picker; the probe picker case was rated P4) · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime (cheat picker); static (probe picker)
- **Description:** `refresh()` replaces `games_` and re-finds `strip_sel_` by path, falling back to index 0 when the path is gone. It never closes `cheats_open_` / `probes_open_` and never clears `cheats_` / `probes_`. With a picker open, HOME returns before the panel, the only place that recomputes `probes_`. The cheat picker then writes the old cartridge's cheat under the new cartridge's key; the probe picker starts the new cartridge with a slot number read for the old one.
- **Repro:** HOME, cartridge A, Triangle (or Square). While the picker is open the list changes without A: a scan ends after A's file was removed over FTP, or A's share or USB drive was removed. Cross.
- **Expected:** The picker closes, or stays bound to A by path.
- **Actual:** The picker stays open, titled with the new strip selection, listing A's cheats or probes; Cross acts on the new cartridge.
- **Root cause:** Index-based UI state is not re-anchored on list replacement; the pickers' cached vectors are keyed to no game.
- **Evidence:** Harness: select the second test cartridge, Triangle, delete its ROM, `library().rescan()`, Cross; recorded `cheat_set(game=1A000000-00000000-C:45, name=Cheat of 10000000-00000000-C:45)`; picture `p2-cheats-after-rescan.png`. Probe picker: code only. The trigger is narrower than first reported: a mere change of strip position does not change the game (selection is kept by path); the selected cartridge has to leave the list. As in FE-006 the core's name check limits wrong cheat writes.
- **Remediation:** Remember the picker's game path when it opens; in `refresh()` (or at the top of `frame()`) close both pickers and clear `probes_for_` when that path is gone or `selected()->path` differs.
- **Sources:** architecture #1 (P3), input #14 (P4), robustness #13 (P4).

### FE-008

**Probe picker Cross has no busy check: during a share copy it rewrites `load_state`, and the copied cartridge starts from that slot**

- **Where:** `ps5/host/menu.cpp:1256-1259` (`host.play`), `:852-855` (`Menu::play`), `:1018-1028` (copy completion); `ps5/host/frontend.cpp:831-835`
- **Component:** T-COPY, M-MENURESULT, D-PROBES, C-PROBES-CROSS, S-COPYING
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** static (the host test has no share: ps5vfs is stubbed)
- **Description:** `host.play` writes `result.load_state = slot` before `Menu::play` decides; `Menu::play` returns at once while a copy runs but leaves `load_state` changed. HOME and RACK Cross check `host_.busy()`; the probe picker does not. When the copy ends, `Menu::draw` starts the copied cartridge and `main.cpp:298` passes the changed `load_state`. The picker also closes as if it had worked.
- **Repro:** Cross on a network-share cartridge A ("Copying ... from the share..."). Before it finishes, select cartridge B with probes, Square, Cross on Probe 3.
- **Expected:** The picker refuses while a copy runs (and says so); a refused play leaves the result untouched.
- **Actual:** A starts with `--load-state 3`: its own slot-3 state if one exists, otherwise a failed load.
- **Root cause:** Shared result state written before validation; a missing `!(host_.busy && host_.busy())` guard.
- **Evidence:** Code as cited; compare `frontend.cpp:742` and `:1081`. Severity lowered from P2: it needs a copy in progress and a probe chosen for another cartridge, and the slot loaded is the first cartridge's own.
- **Remediation:** Pass the slot into `Menu::play` and assign `load_state` and `name` only on acceptance, carried with the copy job (IMP-08). Add the busy check and a notice to the probe picker, and a notice for HOME/RACK Cross while busy.
- **Sources:** architecture #6, input #4, robustness #5.

### FE-009

**Probe identity differs between the board, the quick menu and the core**

- **Where:** `ps5/host/library.cpp:555-576` (`saveName`, `probesOf`); `ps5/host/frontend.cpp:663-680` (panel), `:987-1001` (RACK filter); `ps5/host/quickmenu.cpp:127-135`; `vendor/gopher64/src/ui/storage.rs:190-213`, `:254-261`
- **Component:** M-PROBES, P-PANEL-PROBES, D-PROBES, C-PROBES-CROSS, V-QM-LOAD
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime (a unit run of `probesOf` against the project's `library.cpp`) + static
- **Description:** Gopher64 names states `<prefix>-<ROM hash>.state<slot>`, where the prefix is the header name stripped to `[a-zA-Z0-9_ -]`, or the 3-character game id when the header is not valid UTF-8. Four host rules disagree with it and with each other: `probesOf` matches any file starting with `saveName(header) + "-"` and a slot digit 0-9 (no hash); the panel picks a fingerprint file by its last digit; the RACK filter uses the text before the last `-`; the quick menu uses the exact stem with slots 1-5.
- **Repro:** (a) Two ROMs with the same header name (regional releases, revisions): take a probe in one, select the other in HOME: its panel lists the probe; Square, Cross starts it with `--load-state N` for a file that does not exist for that hash. (b) A ROM whose header is not UTF-8 (bytes >= 0x80): take a probe, return to HOME: the board shows none.
- **Expected:** A cartridge lists, and can start from, exactly the states the core will load for it.
- **Actual:** Sibling ROMs list each other's probes; non-UTF-8 headers hide theirs.
- **Root cause:** The hash Gopher64 computes is not stored in `Game`, so the host can only match by name, and it does not apply the core's game-id fallback.
- **Evidence:** Unit run (`SCRATCH/robustness/probes/t`, re-run by the verifier): a non-UTF-8 header gives `saveName='64' probes=0` although the core kept one as `<id>-...`; a second ROM with the same header name gives `probes=1 (the state is the other region's)`. Severity lowered from P2: probes are wrongly listed or missing, the core still loads only its own file, no save data is lost. Not a defect on its own: slot 0 missing from the quick menu is a documented choice (`quickmenu.cpp:70`); FE-005 is what breaks that separation.
- **Remediation:** Have the core expose the state stem (or the hash) for a ROM, as `psgopher_rom_facts` does for facts; store it in `library.tsv`; use one function for the panel, the picker, the filter and the quick menu (IMP-06).
- **Sources:** architecture #8, robustness #4, overlay #6.

### FE-010

**RACK "Has probes" filter is stale for the whole run of the title**

- **Where:** `ps5/host/frontend.cpp:987-1001`
- **Component:** V-RACK, C-RACK-OPTIONS, M-PROBES
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime
- **Description:** `with_probes` and `probes_version` are function statics that outlive every per-menu `Frontend`. They are rebuilt only when the library version changes, and taking a probe in a game does not change it.
- **Repro:** RACK, Options x3 (Has probes) to build the set. Power on a cartridge without probes, take a probe, quit. In RACK choose Has probes again.
- **Expected:** The filter reflects the states folder each time the list is shown.
- **Actual:** The cartridge just probed stays hidden and the cursor skips it, until a rescan.
- **Root cause:** The cache key (library version) is not the data's source (the states folder), and the statics span `Frontend` instances.
- **Evidence:** Harness `test_filter`: a state file for a second cartridge written between two menus; in menu 2 the filter showed "1 cartridges" and Right did not reach it. Control run with both state files present before menu 1: "2 cartridges". Reproduced by investigator and verifier.
- **Remediation:** Make the set a `Frontend` member rebuilt on construction and when the filter is selected (or key it on the states folder's mtime). Fold into the single probe API (IMP-06).
- **Sources:** architecture #3.

### FE-011

**Rescan requests are dropped while a scan runs**

- **Where:** `ps5/host/library.cpp:429-433`; callers `ps5/host/menu.cpp:994-997`, `:1003-1006`
- **Component:** T-LIBRARY-SCAN, M-LIBRARY, M-SHARES, C-EDIT-SAVE, C-EDIT-REMOVE, C-BENCH-RESCAN
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** static
- **Description:** `Library::rescan` returns at once when a scan is running, and a running scan uses the place list it read at its start. The share form calls it after a mount and after a removal and tells the user it is searching.
- **Repro:** Start with a large library or a slow share so the boot scan takes a while. BENCH, Network shares, add a share (or remove one) before the scan ends.
- **Expected:** A rescan asked for during a scan runs once the current one ends.
- **Actual:** The request is discarded: the new share's cartridges do not appear, a removed share's stay listed (Cross on one fails) until a manual "Search for cartridges" after the scan.
- **Root cause:** `if (state().scanning.exchange(true)) return;` with no pending flag.
- **Evidence:** Code as cited. Unmounting during a scan is memory-safe (the mounts are reference-counted, `ps5_vfs.cpp:80-83`, `:135-154`); only the list goes stale.
- **Remediation:** An atomic "again" flag set when a request arrives during a scan; the scan thread loops once more while it is set.
- **Sources:** architecture #7, robustness #9.

### FE-012

**RACK selection is index-based across a list refresh**

- **Where:** `ps5/host/frontend.cpp:383`, `:406-409`, `:430`
- **Component:** M-FRONTEND-STATE, V-RACK
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** static
- **Description:** `refresh()` re-anchors `strip_sel_` by path but `order()` only clamps `rack_sel_`. When a scan adds or removes a cartridge that sorts before the selection, `rack_sel_` points at a neighbour: the board re-seats another cartridge and Cross powers that one on. The Square handler does re-anchor by game index (`:1070-1076`), so the two paths disagree.
- **Repro:** RACK, select a cartridge near the end. A scan finishes having added a ROM that sorts earlier.
- **Expected:** The selection follows the same path, as the strip's does.
- **Actual:** The selection keeps its index.
- **Root cause:** `chosen` is taken from `selected()` (in RACK, the rack game) but only `strip_` is searched for it.
- **Evidence:** Code as cited.
- **Remediation:** Remember both selections' paths before replacing `games_` and re-find both after `order()`.
- **Sources:** architecture #9.

### FE-013

**RACK spine codes are never drawn**

- **Where:** `ps5/host/frontend.cpp:1098` (clip rect), `:1129` (`AddText` at (0,0)), `:1129-1134` (rotation afterwards)
- **Component:** P-RACK-SHELVES
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime (picture and a scratch vertex count)
- **Description:** The spine code is added with `AddText` at `ImVec2(0, 0)` while the shelf clip rect is active; the vertices are rotated into place only afterwards. ImGui culls text against the clip rect at creation, so nothing is emitted.
- **Repro:** Open RACK with any library (`3-rack.png`).
- **Expected:** Each spine shows its code rotated up the spine, as the design does (`ps5/art/live-board.html:708-709`).
- **Actual:** Plain coloured bars.
- **Root cause:** `ImFont::RenderText` skips every line above the clip rect (`imgui_draw.cpp:7371-7372`) and culls glyphs (`:7474`).
- **Evidence:** Scratch reproduction linked against the prebuilt ImGui objects (`SCRATCH/visual-verify/t.cpp`): the same `AddText` at (0,0) emits 0 vertices under a clip rect of (44,138)-(1312,900) and 16 with the full-screen clip. The passing host run did not notice. Severity lowered from P2: an identifier is missing on every spine, navigation and the panel still work.
- **Remediation:** Push a full-screen clip rect around the `AddText` and restore the shelf clip after the rotation, or build the text at its final position.
- **Sources:** visual #0.

### FE-014

**The RACK panel shows HOME-strip wording and a Square "Probes" pill**

- **Where:** `ps5/host/frontend.cpp:592`, `:701-702`, `:1070-1077`
- **Component:** P-PANEL-ACTIONS, P-PANEL-RACK, C-RACK-SQUARE
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime (pictures `3-rack.png`, `4-rack-sorted-filtered.png`)
- **Description:** In RACK the eyebrow reads "ON THE HOME STRIP" for every cartridge, and the pills show Square "Probes" while Square cycles the sort there and RACK has no probe picker.
- **Repro:** RACK, select any cartridge; press Square.
- **Expected:** The eyebrow follows the design (`live-board.html:563`: "ON THE HOME STRIP" only for cartridges on the strip, otherwise "IN THE RACK"); the pills match RACK's buttons.
- **Actual:** As described.
- **Root cause:** `panel()` is shared between HOME and RACK; only the third pill and the eyebrow vary on `rack_panel`, and the eyebrow is unconditional.
- **Evidence:** Code and pictures as cited.
- **Remediation:** In RACK drop or relabel the Probes pill; make the eyebrow conditional as in the design. See IMP-11 for the wider question of what RACK seats.
- **Sources:** visual #1.

### FE-015

**The game panel has no vertical budget: it runs under the HOME strip and can pass the screen bottom in RACK**

- **Where:** `ps5/host/frontend.cpp:583`, `:597-600` (title lines), `:674-683` (probes), `:776-777`
- **Component:** P-PANEL-HOME, P-PANEL-RACK, P-PANEL-TITLE, P-PANEL-PROBES
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** medium
- **Evidence type:** static (arithmetic from the code and the pictures; not rendered)
- **Description:** The panel's height is the sum of its parts with no limit: up to 3 title lines of 49 px, 3 description lines, up to 3 probes of 50 px. HOME draws the panel before the strip, so the strip is drawn on top of it.
- **Repro:** A ROM whose title needs 2 or 3 lines and that has 2 or 3 probes; HOME and RACK.
- **Expected:** The panel stays above the strip (HOME) and above the screen bottom (RACK).
- **Actual:** HOME: `2-home-moved.png` ends at about y=798 with 1 title line and 2 probes; a 2-line title and a third probe give about 897, over the strip cartridges that start at 854 (x 1411-1890). RACK: `3-rack.png` ends at about 905 with 1 line and no probes; a 3-line title with 3 probes gives about 1126 of 1080.
- **Root cause:** `cy` accumulates unbounded; nothing limits title lines or probes by view.
- **Evidence:** As above. Severity lowered from P2.
- **Remediation:** Give each view a height budget and reduce in a fixed order (description lines, probes, title lines). Add a long-title, three-probe scenario to the host pictures.
- **Sources:** visual #3.

### FE-016

**`wrap()` never breaks a word and appends the ellipsis unmeasured; picker titles have no width limit**

- **Where:** `ps5/host/frontend.cpp:296-319` (`wrap`), `:849` (probe picker title), `:949` (cheat picker title)
- **Component:** P-PANEL-TITLE, D-PROBES, D-CHEATS
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** static
- **Description:** `wrap` splits on spaces only, so a single word wider than the panel is one overlong line; on truncation the ellipsis is appended without measuring again. The pickers draw the title at 40 px with no wrap or truncation inside a 470 / 560-wide panel.
- **Repro:** A ROM whose title is longer than about 24 characters (common for file-derived titles): open either picker. Or a title that is one 30-character word: look at the panel.
- **Expected:** Titles stay inside their panels.
- **Actual:** From `13-home-probes.png`, 13 characters take about 245 px at 40 px, so a title longer than about 24 characters passes the picker's edge at 1856. The unbroken long word is the rarer case.
- **Root cause:** Word-level wrapping with no character break; no measure after the ellipsis; no limit on picker titles.
- **Evidence:** Code as cited.
- **Remediation:** One shared helper that fits text to a width on UTF-8 boundaries (also FE-040, FE-051, FE-062); use it for the picker titles.
- **Sources:** visual #4.

### FE-017

**The keyboard's text field cannot show the end of a long value**

- **Where:** `ps5/host/menu.cpp:716-718`
- **Component:** D-KEYBOARD
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** medium
- **Evidence type:** static (`11-keyboard-typed.png` shows a stray vertical scrollbar and the hint wrapping to two lines)
- **Description:** The typed text is drawn with `ImGui::Text("%s_")` in a fixed child 26 font sizes wide, with no horizontal scroll and no scroll-to-end. Beyond about 40 monospace characters the tail and the caret are clipped. Share paths and user names can be that long.
- **Repro:** Type 40 or more characters in the Server or path field.
- **Expected:** The caret and the last characters stay visible.
- **Actual:** The tail is out of view.
- **Root cause:** A fixed-size child without scrolling to the end.
- **Evidence:** Code as cited.
- **Remediation:** Show the tail (measure and draw the last characters that fit, with a leading ellipsis) or `SetScrollHereX(1)`.
- **Sources:** visual #11.

### FE-018

**RACK: when the filter hides every cartridge, the hidden selection stays live and Cross powers it on**

- **Where:** `ps5/host/frontend.cpp:1015-1023` (`step`, the visibility fix-up), `:1080-1082`
- **Component:** C-RACK-OPTIONS, C-RACK-CROSS, S-FILTER-EMPTY
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime
- **Description:** With nothing visible, `step()` returns its starting index, so `rack_sel_` keeps pointing at a hidden cartridge. `selected()` and line 1080 ignore visibility: the panel shows that cartridge in full and Cross, Triangle and Square act on it.
- **Repro:** R1 (RACK), Options x5 to the USB filter with no USB cartridges, Cross.
- **Expected:** "0 cartridges", no panel, Cross and Triangle do nothing (or a message).
- **Actual:** Chip USB, "0 cartridges", every spine dimmed, a full panel with "Power on"; Cross starts the cartridge (`RESULT play=1`).
- **Root cause:** Visibility is a draw-time property that the selection logic does not consult.
- **Evidence:** Harness scenario `rackfilter`, picture `rf1-usb-filter-empty.png`; re-run by the verifier. Severity lowered from P2: the panel shows the cartridge that Cross starts, so the user is not misled about which game runs.
- **Remediation:** Compute the visible count first; when it is zero make `selected()` return null in RACK and draw an empty-filter message (also FE-042).
- **Sources:** input #1.

### FE-019

**BENCH: Up from the first row wraps to "Close PSGopher", which closes the title on one Cross**

- **Where:** `ps5/host/frontend.cpp:1345-1347`, `:1370-1371`; `ps5/host/menu.cpp:1261-1264`
- **Component:** C-BENCH-UPDOWN, C-BENCH-CLOSE
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime
- **Description:** Up/Down wrap over all 22 items and the last is "Close PSGopher", which runs on a single press edge with no confirmation.
- **Repro:** BENCH, Up at the top row (a common "go to top" reflex), Cross.
- **Expected:** No wrap onto a destructive action, or a confirmation for Close.
- **Actual:** The title closes.
- **Root cause:** `bench_sel_ = (bench_sel_ + n - 1) % n;` plus an unconfirmed action.
- **Evidence:** Harness scenario `benchwrap`, picture `b2-up-wrap.png` ("Close PSGopher" highlighted after Down, Up, Up); re-run by the verifier. Severity lowered from P2: no game is running, the cost is a relaunch.
- **Remediation:** Clamp at both ends or stop the wrap at the action card; a two-press confirmation for Close (IMP-16).
- **Sources:** input #2.

### FE-020

**Quick menu: Up from Resume wraps to "Eject cartridge"; Reset and Eject act on one Cross**

- **Where:** `ps5/host/quickmenu.cpp:215-216`, `:243-244`
- **Component:** C-QM-UPDOWN, C-QM-RESET, C-QM-EJECT
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** static; the project's own test asserts the sequence (`ps5/tests/test_quickmenu.cpp:308-309`)
- **Description:** The menu opens on Resume. One Up selects the last row, "Eject cartridge", and Cross quits the game at once; "Press reset" likewise acts on a single press. Progress since the last in-game save is lost.
- **Repro:** In a game: tap the touch pad, Up, Cross.
- **Expected:** No wrap from the first row, or a confirmation step for Reset and Eject.
- **Actual:** The game ends after two presses.
- **Root cause:** `cursor = (cursor + n - 1) % n;` and `finish(it.action, it.slot)` for actions 3 and 4.
- **Evidence:** `test_quickmenu.cpp`: `a = run({ PAD_UP, PAD_CROSS }); CHECK(a.action == 4);`. This is designed and tested behaviour; it is listed because of what two presses cost.
- **Remediation:** Clamp the cursor, or ask for a second Cross on the same row ("Cross again to eject") (IMP-16). The test changes with it.
- **Sources:** input #13.

### FE-021

**Held buttons stay held after a controller disconnect or a shell intercept, and the menu's repeat keeps firing**

- **Where:** `ps5/host/platform.c:329-341` (`pad_fill`, line 332), `:355-366` (`pad_poll`, line 365)
- **Component:** I-PAD-POLL, I-PADINPUT
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** medium
- **Evidence type:** static; console-unverified
- **Description:** `pad_poll` stores a sample in `state->last` only when it is connected and not intercepted; `pad_fill` reports `state->last.buttons` whenever the handle is open, whatever `connected` says. Player 0's handle is never closed on disconnect. A direction held at the moment of a disconnect (battery, cable, range) or a PS-button overlay stays held: the strip and rack scroll to the end, and BENCH rows cycle, each step saving `config.json` or `psgopher.ini` (FE-028). The menu's release gate runs only once at start. The game path handles the intercept explicitly (`psgopher_sdl3.cpp:470-475`); the menu path does not.
- **Repro:** On a console: in BENCH on "HDR peak", hold Right and switch the controller off, or press PS while holding Right.
- **Expected:** A disconnected or intercepted pad reads as at rest.
- **Actual:** The last held buttons persist until the pad reports again.
- **Root cause:** `pad->held = state->handle >= 0 ? state->last.buttons : 0;` never consults `connected` or the intercept bit.
- **Evidence:** Code as cited. Whether the console sends a connected, non-intercepted, all-zero sample in these cases is unverified. Severity lowered from P2.
- **Remediation:** In `pad_poll`, reset `state->last` to rest when a sample is disconnected or intercepted or the read fails; or make `pad_fill` report rest when `!state->connected`.
- **Sources:** input #3, robustness #10.

### FE-022

**Rumble is left on while the quick menu is up, and after Eject or Reset it is never turned off**

- **Where:** `ps5/sdl3/psgopher_sdl3.cpp:712-719` (`rumble`), `:492-495` (timeout only when a deadline exists), `:586-614` (close); `ps5/host/quickmenu.cpp:357-427`
- **Component:** T-EMU-OVERLAY, E-QM-RUN, C-QM-EJECT, C-QM-RESET, I-HOLD-UNTIL-RELEASED
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** medium
- **Evidence type:** static; console-unverified
- **Description:** Gopher64 turns the motors on with `SDL_HAPTIC_INFINITY` and off only when the game writes the Rumble Pak. The SDL layer stores `rumble_until = 0` for an infinite effect, so `poll_pads` never stops it. The quick menu blocks the emulation thread, so the game cannot write "off". `SDL_CloseGamepad` / `SDL_CloseJoystick` only decrement counters.
- **Repro:** On a console, in a game with a Rumble Pak: tap the touch pad during a rumble effect and stay in the menu, or choose Eject cartridge or Press reset.
- **Expected:** Vibration stops while the game is paused, and in any case when the game is left.
- **Actual:** The controller keeps vibrating for the whole menu; after Eject it carries on into the board until a later game writes rumble off.
- **Root cause:** An infinite rumble has no deadline, and neither the menu nor teardown calls `pad_player_vibrate(player, 0, 0)`.
- **Evidence:** `input.rs:221-226`; the only vibrate callers are `psgopher_sdl3.cpp:494` and `:715`. The missing stop is certain from the code; that the controller keeps vibrating after a single `scePadSetVibration` is the stand-in's own assumption. Severity lowered from P2 (overlay).
- **Remediation:** Stop all motors when `psgopher_quickmenu_run` starts and when the last joystick is closed; optionally restore on Resume (IMP-24).
- **Sources:** input #5, overlay #3.

### FE-023

**ImGui pages lose the cursor: a D-pad press on the empty Shares list, or Circle on the keyboard, leaves no focused item and Cross does nothing**

- **Where:** `ps5/host/menu.cpp:897` (Shares page), `:699-701` (keyboard Circle returns before `Begin`)
- **Component:** C-SHARES-NAV, C-KB-CIRCLE, F-SELECTION, I-IMGUI-NAV
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime
- **Description:** On the Shares page with no share (the first-run state) any D-pad press with no target clears ImGui's focus: the amber frame disappears and Cross does nothing until another D-pad press brings it back, so every press toggles the cursor. Cancelling the keyboard with Circle hands focus to the root window with no item, instead of back to the field (Done does return it).
- **Repro:** BENCH, Network shares, Down, Cross. In the form: Server, Cross (keyboard), Circle.
- **Expected:** The cursor stays on "Add a share"; it returns to the field after a cancelled keyboard.
- **Actual:** NavId goes to 0 (trace: `navid=a45d8171` -> `navid=00000000`; after the keyboard Circle `win=##shares navid=00000000`); a Cross pressed in that state does not open the form.
- **Root cause:** ImGui navigation with nothing to move to drops the focused id (observed, the internal step was not traced); `Menu::go` sets `focusFirst` only on a screen change.
- **Evidence:** Harness scenarios `holdcross` and `form`, pictures `h3-keyboard-after-long-cross.png`, `f6-c1.png`; re-run by the verifier.
- **Remediation:** On these pages re-assert focus on the first row when no item is focused; when the keyboard closes by any route, focus the field it was opened for.
- **Sources:** input #7.

### FE-024

**`PadInput` keeps time in float seconds since boot: repeats slow down and then stall at long uptimes**

- **Where:** `ps5/host/frontend.h:45-47`; `ps5/host/menu.cpp:1352`; `ps5/host/frontend.cpp:351-362`
- **Component:** I-PADINPUT
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime (the real `PadInput::repeat`); whether the console's clock reaches such values is console-unverified
- **Description:** `menu.input.now = (float)now_seconds()` with `CLOCK_MONOTONIC`. Above about 2^21 s (24 days) a float step is 0.25 s, larger than the 0.09 s repeat interval; above 2^24 s it is 1 to 2 s, larger than the 0.38 s delay.
- **Repro:** Hold Right for 3 s at 60 Hz with the clock at several bases.
- **Expected:** About 28 events at any uptime.
- **Actual:** base 1e3 s: 28; base 3e6 (about 35 days): 12; base 1.7e7 (about 197 days): 2, the first repeat after 1.017 s.
- **Root cause:** `float now`, `held_since[32]`, `last_repeat[32]`.
- **Evidence:** Run output as above, re-run by the verifier. The console's monotonic clock may keep counting through rest mode. `Buttons` in `menu.cpp` already uses `double`.
- **Remediation:** Use `double`, or times relative to the menu's start.
- **Sources:** input #10.

### FE-025

**Every frame copies the whole game list under the scan mutex**

- **Where:** `ps5/host/frontend.cpp:379-384`; `ps5/host/library.cpp:490-496`
- **Component:** E-FRONTEND-FRAME, M-LIBRARY
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime (allocation counter, desktop CPU)
- **Description:** `refresh()` calls `library().games(&version)`, which returns the vector by value under the library mutex, and only then compares the version. Each `Game` has five `std::string` fields.
- **Repro:** 300, then 3000 ROMs; stay in HOME with nothing pressed.
- **Expected:** Per-frame cost independent of library size when nothing changed.
- **Actual:** HOME idle with 3000 games: 12132 allocations, 990 KB and 1666 us of front-end work per frame; with a version check before the copy and no other change: 131 allocations, 8 KB, 303 us.
- **Root cause:** The copy precedes the version check.
- **Evidence:** Verifier's variant build in `SCRATCH/verify-perf`. Severity lowered from P2: about 8% of a 60 Hz frame at 3000 games on a desktop CPU, about 0.1 ms at 300; not user-visible unless the frame budget is exceeded.
- **Remediation:** `unsigned Library::version() const`; copy only when it differs.
- **Sources:** performance #2.

### FE-026

**Offline: every visible cover and description is re-queued every frame for the whole 60 / 300 s rest**

- **Where:** `ps5/host/web.cpp:114-115`, `:136-138` (`doCover`), `:151-152`, `:179-181` (`doAbout`), `:206-215` (`ask`), `:238-241` (`webCover`)
- **Component:** T-WEB, S-OFFLINE, R-WEB-ENTRIES
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime
- **Description:** While `offline_until` (or `wiki_until`) is in the future, a job stats `<name>.png` and `<name>.none`, clears the "asked" flag and returns. The next frame's `webCover` asks again. In RACK the 64-job cap adds churn: `ask()` drops the oldest job and clears its flag too.
- **Repro:** Network down. Open the menu with a few hundred cartridges; HOME, then RACK.
- **Expected:** During the rest nothing is queued; items are asked again when it ends.
- **Actual:** 421 frames: 92543 asks and 48355 web-thread jobs for one real request (performance verifier); 26000 jobs in 35.9 s with 40 cartridges (robustness).
- **Root cause:** A retry is represented as "not asked" instead of "asked, wait until the rest ends".
- **Evidence:** Counters in scratch copies of `web.cpp`. The web thread's own CPU was 0.23 s over 23 s on the PC: the cost is mutex round trips and two `stat()` calls per job, wasteful rather than visibly slow. The per-second rate on a console is unverified. FE-003 multiplies it by the number of spines.
- **Remediation:** Keep the flag set during the rest and store a retry time per entry, or check the rest in `webCover` before `ask()`.
- **Sources:** performance #3, robustness #6.

### FE-027

**RACK per-frame cost grows with the whole library**

- **Where:** `ps5/host/frontend.cpp:1176-1180` (letter index), `:1213-1215` (count), `:1002-1014` (`visible`), `:161-163` (`letterOf`)
- **Component:** V-RACK, P-RACK-INDEX, P-RACK-BAR, C-RACK-OPTIONS
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime
- **Description:** Besides drawing the visible rows, `rack()` runs `visible(k)` over all games for the letter index (with `letterOf`, which builds a title string per game) and again for the count, every frame. Under "Has probes", each `visible()` builds `saveName(header)` and looks it up in a `std::set<std::string>`.
- **Repro:** 3000 games, RACK, then Options x3.
- **Expected:** Per-frame work proportional to the visible rows.
- **Actual:** 3000 games: "All" 16094 allocations, 1065 KB, 3186 us median draw; "Has probes" 29419 allocations, 1401 KB, 4746 us. About 12000 of the "All" figure is FE-025; the rest and the +13300 are this.
- **Root cause:** Histogram and count recomputed from the whole list every frame; per-call string building.
- **Evidence:** Verifier run in `SCRATCH/verify-perf`.
- **Remediation:** Compute each game's letter and save name once in `refresh()`; cache the histogram and the shown count keyed on (version, sort, filter).
- **Sources:** performance #6.

### FE-028

**Every first read of a Gopher64 setting rewrites `config.json` non-atomically; held Left/Right on BENCH rewrites `config.json` or `psgopher.ini` about 11 times a second**

- **Where:** `vendor/gopher64/src/psgopher.rs:84-95` (get), `:104-117` (set); `vendor/gopher64/src/ui/config.rs:124-152`; `ps5/host/frontend.cpp:1269-1273`, `:1355-1365`, `:926-935`
- **Component:** M-SETTINGS-G64, M-SETTINGS-TITLE, C-BENCH-HDR-WHITE, C-BENCH-HDR-PEAK, M-CHEATS
- **Severity:** P3 (performance verifier; the architecture verifier rated the non-atomic write alone P4) · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** static; console-unverified (the host stand-in bridge is a `std::map`, no file I/O was measured)
- **Description:** `psgopher_setting_get` builds a `Config` with `Config::new()`, which reads the file and sets `write_to_disk = true`; dropping it writes the whole file with `File::create` (truncate) and a serde write, no temporary file. `Menu::get` caches per key, but the cache belongs to the per-visit `Menu`: about 14 read-and-rewrite cycles per menu visit (4 on the first HOME frame, about 10 on the first BENCH frame). Every BENCH change is another cycle; `pad.repeat` fires every 0.09 s; the HDR steppers call `ts->save()` per step; cheat option Left/Right rewrites `cheats.json` per step.
- **Repro:** Open the list: `config.json`'s mtime changes with no setting changed. BENCH, hold Right on "HDR peak" from 0 to 4000: 40 writes of `psgopher.ini`.
- **Expected:** A read does not write. Repeated changes are written once. Writes go through a temporary file and rename, as `psgopher.ini` does.
- **Actual:** As described. A kill or power loss between truncate and write leaves an empty or partial `config.json`, which `Config::new` treats as unparsable: every Gopher64 setting falls back to its default.
- **Root cause:** `Config`'s Drop-writes-to-disk reused for reads; the front end saves on every repeat step.
- **Evidence:** Code as cited. Upstream Gopher64 has the same Drop write; the bridge adds the writes on read.
- **Remediation:** In `psgopher_setting_get` set `write_to_disk = false` (and in set when the value is rejected). Mark title settings and cheat options dirty and save on release or on leaving the view. The Rust part is in `vendor/gopher64` (export the patch).
- **Sources:** architecture #11 (P4), performance #7 (P3). Related: FE-056.

### FE-029

**Adding, saving or removing a share mounts or unmounts on the UI thread; the boot scan runs alongside an autostart game**

- **Where:** `ps5/host/menu.cpp:983-996`, `:1002`; `ps5/host/shares.cpp:333`; `ps5/host/main.cpp:279-287`
- **Component:** C-EDIT-SAVE, C-EDIT-REMOVE, E-AUTOSTART, T-LIBRARY-SCAN, S-SCANNING-BACKGROUND
- **Severity:** P3 · **Status:** Confirmed (the blocking mount); Suspected (scan and web threads competing with a running game) · **Confidence:** medium
- **Evidence type:** static; console-unverified
- **Description:** `mountShare` / `unmountShare` (`ps5vfs_mount_smb` / `_nfs`) run synchronously inside `drawShareEdit`, in the middle of building a frame. No frame is presented until they return, and presses made during the stall are lost (the pad layer reports only the last sample).
- **Repro:** BENCH, Network shares, add a share with an unreachable server, "Add and mount it".
- **Expected:** The interface keeps drawing (a "Mounting..." state) while a worker mounts.
- **Actual:** The menu freezes for as long as the connect takes (the timeout is unverified).
- **Root cause:** Blocking network I/O on the render thread.
- **Evidence:** Code as cited. The claim that the detached scan and web threads compete with a running game (autostart with an uncached library) is plausible but not shown: with a `library.tsv` present the scan reads only new files' 4 KB headers.
- **Remediation:** Mount on a worker with a pending row, as "Find SMB servers" already does (IMP-17). Consider delaying the boot rescan when `autostart.txt` is set.
- **Sources:** performance #10.

### FE-030

**A PNG's declared size is not limited: 547 KiB decodes to 576 MB (1.1 GB peak) before an unchecked upload**

- **Where:** `ps5/host/image.cpp:24-25`; `ps5/host/web.cpp:109-111`, `:119`; `vendor/gopher64/src/psgopher.rs:648`
- **Component:** R-WEB-DISK-CACHE, M-WEB-FETCH, R-TEX-COVERS
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime (decode); the console's out-of-memory behaviour is console-unverified
- **Description:** `decodeImage` calls `stbi_load` on any cached cover with no size check (`STBI_MAX_DIMENSIONS` defaults to 1<<24 per side) and then copies the pixels a second time. The core limits only the body to 16 MiB and checks the PNG signature and trailer. Files in `cache/covers` can also be placed over FTP. The UI thread would then build a full mip chain and a staging buffer the size of the image through FE-031's unchecked path.
- **Repro:** A 559,931-byte RGBA PNG declaring 12000x12000 placed at `cache/covers/<cartridge name>.png`; show that cartridge.
- **Expected:** A cover above a sane limit (for example 2048x2048, or a pixel budget) is refused before decoding and treated as no cover.
- **Actual:** Decoded to 12000x12000, 576,000,000 bytes, peak RSS 1101 MiB (investigator and verifier, against the project's `image.cpp`).
- **Root cause:** No `stbi_info` check and no dimension limit before `stbi_load`.
- **Evidence:** As above. Severity lowered from P2: the fetch uses TLS with web roots against one host, so a remote trigger needs a malicious file in the thumbnail repository; the local trigger is the user's own write.
- **Remediation:** `stbi_info` first, reject above a limit (or define `STBI_MAX_DIMENSIONS`); take ownership of the stb buffer instead of copying; refuse uploads above the device's `maxImageDimension2D`.
- **Sources:** robustness #2.

### FE-031

**`uploadTexture` ignores allocation, bind, map and command-buffer results; a failed map makes `memcpy` write through a null pointer**

- **Where:** `ps5/host/menu.cpp:235-317` (line 254-256 the map and copy), `:203-211` (`memoryType`), `:179-199` (`makeTargets`)
- **Component:** R-TEX-BOARD, R-TEX-LOGO, R-TEX-COVERS, R-DISPLAY
- **Severity:** P3 · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** static (lavapipe did not fail these allocations)
- **Description:** Only `vkCreateImage` is checked. `vkAllocateMemory`, `vkBind*Memory`, `vkCreateBuffer`, `vkMapMemory`, `vkAllocateCommandBuffers`, `vkQueueSubmit` and `vkCreateImageView` are not; `memoryType()` falls back to index 0. If host-visible memory cannot be allocated, `mapped` stays null and `memcpy(mapped, ...)` crashes. `makeTargets` ignores its results too.
- **Repro:** An allocation failure during any upload: the oversized PNG of FE-030, or memory pressure while the board (about 53 MB with mips plus 40 MB staging) is uploaded on a return from a game.
- **Expected:** A failed step frees what was made and returns `ImTextureID_Invalid`; the front end draws procedurally (S-NO-ART already handles an invalid board texture).
- **Actual:** Execution continues with null handles into a crash.
- **Root cause:** Return values discarded.
- **Evidence:** Code as cited.
- **Remediation:** Check each result, one failure path that cleans up, `memoryType` returning `UINT32_MAX` when nothing matches. Do it with FE-001's null-set handling: same function.
- **Sources:** robustness #3.

### FE-032

**Overlay texture ids come from a counter that is never reset and stops at 4096: after about 2047 quick-menu openings at 4K the title asserts**

- **Where:** `ps5/host/quickmenu.cpp:307-310`; `vendor/gopher64/parallel-rdp/interface.cpp:939`, `:948`
- **Component:** O-TEXTURE, R-OVERLAY-TEXTURES, R-FONTS, R-OSD-CONTEXT, E-QM-RUN
- **Severity:** P3 (overlay verifier; the robustness verifier rated the static reading P4) · **Status:** Confirmed · **Confidence:** high
- **Evidence type:** host-runtime (ASan, ImGui asserts on, a stub applying the interface's id rule); whether the assert aborts on the console is unverified
- **Description:** `updateTextures` gives each new texture `next_id++` from a function static that lasts for the process. Each quick-menu opening makes a new ImGui context and font atlas: 2 ids at 4K (the atlas grows once), 1 at 1080p; each game's OSD adds 1 or 2. The interface refuses ids above 4096 and grows its table to id + 1. On refusal the texture keeps id 0, another id is taken on every retry, and `present()` still calls `GetTexID()` on a command that uses it.
- **Repro:** Over one run of the title at 4K, open and close the quick menu 2048 times. Any other failure of `psgopher_overlay_texture` (image creation failing under memory pressure) reaches the same assert on the first frame, which is the more realistic trigger.
- **Expected:** The menu keeps working however often it is opened; an upload failure skips the frame.
- **Actual:** "opening 2047: max id 4096", then the assertion at `imgui.h:4094` on opening 2048. With asserts off the menu would be invisible but still take input.
- **Root cause:** Ids never reused; no handling of a refused upload.
- **Evidence:** `SCRATCH/overlay-lifecycle/qm_ids 2048` at 3840x2160, re-run by the verifier. Severity lowered from P2.
- **Remediation:** Reuse freed ids or reset the counter when no texture is live (after each menu close and each `psgopher_overlay_release`); do not take a new id on a failed retry; skip present when an upload failed. Keeping the quick menu's atlas across openings removes most of the churn (IMP-09).
- **Sources:** overlay #1 (P3, runtime), robustness #12 (P4, static).

### FE-033

**A cover can be evicted in the frame that still draws it: `Machine::shown` keeps a texture id that a later `lookFor` can free**

- **Where:** `ps5/host/board.cpp:481-500`, `:648`; `ps5/host/menu.cpp:325-340` (`freeTexture`), `:1310-1317`
- **Component:** M-MACHINE, F-SEAT-ANIM, R-TEX-COVERS
- **Severity:** P3 · **Status:** Suspected · **Confidence:** medium
- **Evidence type:** static; console-unverified (not reproduced: no validation layers on lavapipe, one cover in the host test)
- **Description:** `Machine::shown` is a copy of the previous cartridge's `Look`, cover id included, kept through the 0.16 s un-seat. `drawBoard` records it before `rack()` / `home()` run their `lookFor` calls. If the old cartridge is no longer looked up, its stamp ages, and an upload later in the same frame can pick it as the oldest and free it at once (`RemoveTexture`, view destroyed). The idle wait protects earlier frames, not the draw list being recorded.
- **Repro:** A full cache (48 or more), HOME on a cartridge whose spine is not in RACK's first rows, R1; during the un-seat (about 10 frames) RACK uploads spine covers.
- **Expected:** A texture is not freed while a `Look` holding it can still be drawn.
- **Actual:** Eviction checks only the per-name frame stamp.
- **Root cause:** Three holders of one id, eviction decided by one of them, destruction inside the frame.
- **Evidence:** Order in `Frontend::frame`: `machine_.step` (484), `drawBoard`, then the views. The conditions are narrow: a full cache, the old entry unstamped for more than 2 frames and the minimum, during about 10 frames with at most one eviction per frame.
- **Remediation:** Defer frees until the frame's fence has signalled (a short deletion queue) and stamp the seated cover as used each frame; or let `Machine` hold the game's name and resolve the texture at draw time (IMP-02). Removing the spine requests (FE-003) takes away the usual trigger.
- **Sources:** architecture #5.

## P4

### FE-034

**The board keeps the last cartridge seated when the library becomes empty or nothing is selected**

- **Where:** `ps5/host/board.cpp:481-501`, `:631-649`; `ps5/host/frontend.cpp:484`
- **Component:** M-MACHINE, S-NO-SELECTION
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static
- **Description:** `Machine::step` seats or swaps only when `wanted` is non-null. With no selection, `has_cartridge` stays true and `shown` is drawn for ever while the panel and the slot line disappear.
- **Repro:** Only share cartridges, one selected; remove the share. HOME shows "No cartridges yet" and a seated cartridge.
- **Expected:** The cartridge slides out when nothing is selected.
- **Actual:** It stays seated.
- **Root cause:** `const bool other = wanted && (...)`: no branch un-seats on null.
- **Evidence:** Code as cited. Lowered from P3: cosmetic; the stale cover id stays valid in practice (with an empty library nothing is looked up, so nothing is evicted).
- **Remediation:** When `wanted` is null and a cartridge is seated, set `seat_dir = -1` and clear `has_cartridge` when the seat reaches 0.
- **Sources:** architecture #10.

### FE-035

**Detached copy and scan threads can touch destroyed statics when Close PSGopher is chosen mid-work**

- **Where:** `ps5/host/menu.cpp:766-771` (`g_copy`), `:870-877`, `:1261-1264` (`host.close`); `ps5/host/library.cpp:257-261`, `:433-482`; `ps5/host/main.cpp:302`
- **Component:** T-COPY, T-LIBRARY-SCAN, C-BENCH-CLOSE
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** medium · **Evidence type:** static; console-unverified
- **Description:** `g_copy` (a namespace-scope object with a mutex and strings) and the library's `State` static are used by detached threads. Close does not check `busy()` or `scanning()`; `main` returns and static destructors run while those threads may be live. `web()` is deliberately leaked for this reason (`web.cpp:52-55`); these are not.
- **Repro:** Power on a share cartridge (the copy starts), go to BENCH, Close PSGopher before the copy ends.
- **Expected:** Close waits for or cancels the workers, or their shared state is leaked like `web()`.
- **Actual:** Undefined behaviour at exit inside a short window; the worst case is a crash report on close.
- **Root cause:** Detached threads plus static-duration objects with destructors, no shutdown protocol.
- **Evidence:** Code as cited. Lowered from P3.
- **Remediation:** Allocate `g_copy` and `State` with `new` and never free them, or refuse Close while busy with a notice (IMP-23).
- **Sources:** architecture #12.

### FE-036

**The probe fingerprint cache is never invalidated**

- **Where:** `ps5/host/frontend.cpp:326-337`
- **Component:** R-FINGERPRINT-CACHE, P-PANEL-PROBES
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static
- **Description:** `fingerprint()` keeps a function-static map from path to bytes, filled once, never refreshed or bounded. A probe taken again in the same slot rewrites the same path. A missing file is cached as a zero-filled (black) picture and never read again.
- **Repro:** HOME on a cartridge with Probe 1. Power on, take Probe 1 again, quit.
- **Expected:** The picture reflects the file's current contents.
- **Actual:** "Probe 1 today hh:mm" with the previous fingerprint until the title restarts.
- **Root cause:** A cache keyed on the path only.
- **Evidence:** Code as cited.
- **Remediation:** Key by path and mtime (the probe already carries its time), or make the cache a `Frontend` member cleared when `probes_for_` changes.
- **Sources:** architecture #13, robustness #15 (part 4).

### FE-037

**"yesterday" for anything under 48 hours old that is not today, and for future timestamps**

- **Where:** `ps5/host/frontend.cpp:116-130` (`agoText`); `ps5/host/quickmenu.cpp:89-101` (`when`)
- **Component:** P-PANEL-PROBES, V-QM-TAKE, V-QM-LOAD
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static
- **Description:** Both functions use `days = (now - t) / 86400; ... else if (days <= 1) "yesterday"`. A time two calendar days back but less than 48 h old reads "yesterday"; so does a future time (the clock set back). The two copies also differ in wording beyond that.
- **Repro:** Take a probe on day D at 23:00; look on day D+2 at 00:30.
- **Expected:** "yesterday" only for the previous calendar day; future times shown as dates.
- **Actual:** "yesterday 23:00".
- **Root cause:** Elapsed-time arithmetic instead of comparing calendar days.
- **Evidence:** Code as cited. The duplication itself is not counted as a defect.
- **Remediation:** One shared helper that compares `tm_year` / `tm_yday` with yesterday's local date.
- **Sources:** architecture #14, overlay #10, robustness #15 (part 3).

### FE-038

**The host pictures folder mixes current pictures with stale ones from an earlier interface**

- **Where:** `ps5/tests/run-host-tests.sh:49`; `ps5/tests/test_menu.cpp:199`; `build/host-tests/menu-pictures`
- **Component:** X-PICTURES
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** host-runtime (folder listing)
- **Description:** The folder holds `1-list`, `2-list-moved`, `3-usb`, `4-network`, `5-shares`, `6-share-form`, `7-keyboard`, `8-keyboard-typed`, `9-share-form-filled`, `10-settings`, `11-settings-changed`, `12-settings-about` and `12-settings-end`, which the current `test_menu` does not produce. The runner converts every `*.ppm` to PNG on each run, so the stale PNGs carry current times.
- **Repro:** List the folder after a run.
- **Expected:** Only the current run's pictures.
- **Actual:** Old and new side by side; a reviewer can take old screens for the current interface.
- **Root cause:** `test_menu` removes only its work folder.
- **Evidence:** The listing; stale PPMs dated before the current run. Test hygiene, not a product defect.
- **Remediation:** Clear the pictures folder at the start of the run.
- **Sources:** architecture #15.

### FE-039

**"1 cartridges"**

- **Where:** `ps5/host/frontend.cpp:1217`
- **Component:** P-RACK-BAR
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** host-runtime (picture `4-rack-sorted-filtered.png`)
- **Description / Actual:** `snprintf(count, sizeof(count), "%d cartridges", shown)` gives "1 cartridges".
- **Repro:** RACK, filter until one cartridge remains.
- **Expected:** "1 cartridge".
- **Root cause:** A fixed plural.
- **Remediation:** Singular for 1.
- **Sources:** visual #2.

### FE-040

**Name truncation cuts bytes (it can split a UTF-8 character) and costs an allocation and a measure per removed byte, every frame**

- **Where:** `ps5/host/frontend.cpp:649-650` (facts value), `:809-811` (strip name), `:958-960` (cheat label)
- **Component:** P-STRIP, P-PANEL-FACTS, D-CHEATS
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static (the split) + host-runtime (the allocation count)
- **Description:** `while (width > limit) name = name.substr(0, name.size() - 4) + "…";`. After the first pass each pass removes the previous 3-byte ellipsis and one byte, so the cut can land inside a 2 to 4 byte sequence (ImGui then shows U+FFFD before the ellipsis), and each removed byte costs a string copy and a full-width measure, for 15 strip items every frame.
- **Repro:** A long strip name with an accented character at the cut; or names of about 80 characters.
- **Expected:** A cut at a character boundary, found once.
- **Actual:** A possible stray glyph; HOME with 300 long-named games: 6795 allocations per frame against 4880 with short names, with no measurable time difference (554 us against 552 us).
- **Root cause:** Byte arithmetic on UTF-8 strings in a linear loop, recomputed for unchanged names.
- **Evidence:** Code and the measurement above. The visual part was lowered from P3: one wrong glyph, rare.
- **Remediation:** One fit-to-width helper on UTF-8 boundaries (shared with FE-016, FE-051, FE-062); cache the shortened title per game.
- **Sources:** visual #6, performance #9.

### FE-041

**Region A (Asia) reserves flag space but draws no flag**

- **Where:** `ps5/host/frontend.cpp:652-655`, `:201` (`flag`), `:187` (`regionOf`)
- **Component:** P-FLAG, P-PANEL-FACTS
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** medium · **Evidence type:** static
- **Description / Actual:** `regionOf` can return 'A'; `flag()` has no case for it; the facts value is still shifted 36 px, leaving a gap before "Asia".
- **Repro:** A cartridge whose id ends in A, or whose name carries an "(Asia" tag.
- **Expected:** A flag, or no shift.
- **Root cause:** The shift depends on `regionOf`, not on whether a flag was drawn.
- **Remediation:** Have `flag()` return whether it drew and shift only then.
- **Sources:** visual #7.

### FE-042

**The letter index has no column for titles starting with a digit; an empty rack shows no message**

- **Where:** `ps5/host/frontend.cpp:161-170` (`letterOf`), `:1167`, `:1173-1179`; compare `:782-784` (HOME's message)
- **Component:** P-RACK-INDEX, S-EMPTY-LIBRARY
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static
- **Description / Actual:** `counts[26]` for '#' is filled but the draw loop is `i < 26`: digit titles are counted and not indexed, and nothing is highlighted when the selected title starts with a digit. `rack()` has no branch for an empty list: empty shelves with no text.
- **Repro:** A library with a title starting with a digit; RACK. An empty library; RACK.
- **Expected:** A '#' column; a message when empty.
- **Root cause:** The loop bound; no `n == 0` branch.
- **Evidence:** Code as cited. Lowered from P3. Two parts of the original finding were refuted (see the end).
- **Remediation:** A 27th column; an empty-rack text (with FE-018's empty-filter text).
- **Sources:** visual #8.

### FE-043

**Hint bars do not match the controls**

- **Where:** `ps5/host/frontend.cpp:1219-1221` (RACK), `:1446-1447` (BENCH), `:1354-1358` (steppers), `:1044`; `ps5/host/menu.cpp:7-10` (header comment)
- **Component:** P-HINTS-RACK, P-HINTS-BENCH, P-HINTS-PROBES, P-HINTS-CHEATS
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** host-runtime (pictures) + static
- **Description / Actual:** RACK always shows "L2 · R2 LETTER" although L2/R2 act only in the A-Z sort (`4-rack-sorted-filtered.png` shows it under "Recently played"). RACK's bar omits "L1 · R1 VIEWS", which works there. BENCH says "◄ ► ✕ CHANGE" but Cross does nothing on the two HDR stepper rows. The `menu.cpp` header comment describes an earlier interface (tabs, left-stick movement on the board).
- **Repro:** Compare the bars with the handlers.
- **Expected:** Each bar lists what its view accepts in its current state.
- **Root cause:** Hand-written hint strings.
- **Evidence:** Code and pictures as cited. Not counted: the undocumented extra close buttons (Square on the probe picker, Triangle on the cheat picker), unused buttons without hints, and RACK having no back button.
- **Remediation:** Build hints per state; add L1·R1 to RACK; give Cross a meaning on steppers or say ◄ ► only; correct the comment.
- **Sources:** visual #9, input #15.

### FE-044

**The quick menu's title is clipped without an ellipsis**

- **Where:** `ps5/host/quickmenu.cpp:261-263`
- **Component:** P-QM-PANEL
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** medium · **Evidence type:** static
- **Description / Actual:** The title uses a clip rect of one line with no measuring. The width is 572 px; from `quickmenu/1-quickmenu.png` an upper-case character takes about 31 px, so header names of 19 or 20 characters (the header holds 20) are cut mid-glyph.
- **Repro:** A game with a 20-character header name; tap the touch pad.
- **Expected:** An ellipsis or a smaller size.
- **Root cause:** `PushClipRect` instead of measured truncation.
- **Evidence:** Code and picture as cited. The copied palette constants are not counted.
- **Remediation:** Measure and truncate (or shrink).
- **Sources:** visual #12.

### FE-045

**Share form: Left/Right on Kind do not toggle it, and drop the cursor**

- **Where:** `ps5/host/menu.cpp:934`, `:574-586` (`feedImGui`)
- **Component:** C-EDIT-KIND, I-IMGUI-NAV
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** host-runtime
- **Description:** The Kind row is meant to toggle on Cross or Left/Right. The same D-pad edge goes to ImGui navigation, whose move with no target clears the focus, so `IsItemFocused()` is false when the toggle is tested.
- **Repro:** BENCH, Network shares, Add a share (Kind focused), Right, Right.
- **Expected:** Right changes SMB to NFS and the cursor stays.
- **Actual:** Kind stays SMB; the first Right clears the focus, the second brings it back.
- **Root cause:** Two consumers of one edge (see IMP-15).
- **Evidence:** Harness scenario `form`: `navid=2bab2eb1` -> `00000000` -> `2bab2eb1`, no toggle; picture `f1b-kind-right.png`; re-run by the verifier. Lowered from P3: Cross on Kind still toggles.
- **Remediation:** Remember the Kind row's focus from the previous frame, or withhold Left/Right from ImGui while Kind is focused, or make Kind two selectables on one line.
- **Sources:** input #6. Related: FE-023.

### FE-046

**RACK L2 cannot step back past a letter whose cartridges are all hidden by the filter**

- **Where:** `ps5/host/frontend.cpp:1056-1065`
- **Component:** C-RACK-L2R2
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** host-runtime
- **Description:** Going back, L2 finds the previous letter's start and then walks forward to the first visible cartridge; if that letter is fully hidden the walk returns to the current letter.
- **Repro:** RACK, Options x2 ("Never played", which hides the only cartridge of one letter), land on the next letter, L2, L2.
- **Expected:** L2 goes to the previous letter that has a visible cartridge.
- **Actual:** The selection stays put on every press; R2 still moves on.
- **Root cause:** `while (k < n && !visible(k)) k++;` walks forward instead of continuing backwards.
- **Evidence:** Harness scenario `rackl2`, crops of the letter index; re-run by the verifier. Lowered from P3: only with a filter in the A-Z sort; Left still reaches the earlier cartridges.
- **Remediation:** Loop backwards letter by letter until one has a visible cartridge; stop at 0.
- **Sources:** input #8.

### FE-047

**A rejected cheat toggle stays shown as changed**

- **Where:** `ps5/host/frontend.cpp:916-925`
- **Component:** C-CHEATS-CROSS, C-CHEATS-LEFTRIGHT, S-CHEAT-REJECTED
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** host-runtime (with a stand-in that rejects)
- **Description:** Cross flips `c.on` (and may set `c.option`) before `cheat_set` answers; on a rejection only a notice is shown.
- **Repro:** With the core rejecting: open the picker, Cross on a cheat that is on.
- **Expected:** The row reverts.
- **Actual:** Notice "Gopher64 did not take that cheat", the row reads off and the header counts it as off.
- **Root cause:** The state is changed before the save and never restored.
- **Evidence:** Picture `c4.png`. Lowered from P3: the core rejects only a name or option that is not in that game's database, which in normal use cannot happen; the stale pickers of FE-006 and FE-007 are the way in.
- **Remediation:** Restore the row on failure, or re-read the cheats after every set.
- **Sources:** input #9.

### FE-048

**BENCH option rows wrap on Left/Right (1x straight to 8x upscale)**

- **Where:** `ps5/host/frontend.cpp:1365-1368`
- **Component:** C-BENCH-LEFTRIGHT, C-BENCH-UPSCALE
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** host-runtime
- **Description / Actual:** Left on the first option selects the last: Left on "1x original" gives 8x, the heaviest setting.
- **Repro:** BENCH, Down (Video stage), Left.
- **Expected:** Left/Right stop at the ends; Cross may cycle.
- **Root cause:** `r.set(r.options[(at + m - 1) % m].value)`.
- **Evidence:** Picture `b1-upscale-left.png`; re-run by the verifier. A deliberate wrap and close to a design choice; the chosen chip is visible and applies from the next game.
- **Remediation:** Clamp Left/Right; keep cycling for Cross.
- **Sources:** input #11.

### FE-049

**Any player's tap opens and closes the quick menu, but only player 1's pad drives it**

- **Where:** `ps5/sdl3/psgopher_sdl3.cpp:458-466`, `:508-513`; `ps5/host/platform.c:368`
- **Component:** C-QM-TAP-OPEN, C-QM-TAP-RESUME, I-TAP-DETECT, I-RELEASE-GATE-QM
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static; console-unverified
- **Description:** `poll_pads` watches taps from every connected player; `psgopher_sdl3_menu_pad` reads only player 0's pad and taps.
- **Repro:** Player 2 taps the touch pad in a game and tries the D-pad or Cross.
- **Expected:** Whoever opened the menu can use it, or only player 1 can open it.
- **Actual:** Player 2 can open and close it but not move or choose. If player 1's slot is empty, only a tap leaves the menu.
- **Root cause:** `pad_poll` ends with `(void)pad_player(0, pad)`.
- **Evidence:** Code as cited; `quickmenu.cpp:12` documents "player 1's". Lowered from P3: nothing is stuck.
- **Remediation:** Record which player tapped (with FE-004's per-player state) and read that pad, or merge all connected players' presses.
- **Sources:** input #12, overlay #8.

### FE-050

**RACK Up with Down still held moves down**

- **Where:** `ps5/host/frontend.cpp:1028-1030`
- **Component:** C-RACK-UPDOWN, I-PADINPUT
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static (the host list has one shelf)
- **Description / Actual:** The direction comes from `pad.held & PAD_DOWN`, not from which repeat fired; a fresh Up while Down is still held gives +1.
- **Repro:** RACK with more than 52 cartridges: hold Down, press Up before releasing.
- **Expected:** Up moves up.
- **Root cause:** `const int d = (pad.held & PAD_DOWN) ? 1 : -1`.
- **Remediation:** Take the direction from the repeat that fired.
- **Sources:** input #16.

### FE-051

**The panel word-wraps the whole description (up to 16 KB) every frame to show 3 lines**

- **Where:** `ps5/host/frontend.cpp:609-613`, `:296-319`; `ps5/host/web.cpp:263-275`
- **Component:** P-PANEL-ABOUT, P-PANEL-TITLE
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** host-runtime
- **Description:** The text is copied under the web mutex, copied again without its first line, and wrapped whole (a substring and a concatenation per word) before being cut to 3 lines.
- **Repro:** Select a game with a long description; stay on HOME.
- **Expected:** Only enough text for 3 lines is measured, or the lines are kept until the text changes.
- **Actual:** HOME, 300 games: 400 characters 4880 allocations and 552 us; 1500 characters 5182 and 521 us; 16000 characters 9143 and 793 us.
- **Root cause:** `wrap()` does not stop at `max_lines` and nothing is cached.
- **Evidence:** As above. Lowered from P3: a typical summary is a few hundred to about 2000 characters, where the extra cost is not measurable; 16 KB is the buffer's bound.
- **Remediation:** Stop `wrap()` after `max_lines + 1` lines; cache the lines per game and width.
- **Sources:** performance #4.

### FE-052

**Procedural cartridge labels cost about 220 heap allocations each per frame**

- **Where:** `ps5/host/board.cpp:185-212` (`clip`), `:219-227` (`fill`), `:239-245` (`circle`)
- **Component:** P-LABEL, P-STRIP, P-BOARD-CARTRIDGE
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** host-runtime
- **Description:** Each polygon of a label is clipped through vectors copied per pass, and every vertex goes through a `std::function` transform. HOME draws 16 labels per frame; the geometry is deterministic per game and rebuilt every frame.
- **Repro:** A library whose covers are not available; stay on HOME.
- **Expected:** Label geometry is cheap or cached.
- **Actual:** HOME, 300 games: 4880 allocations, 292 KB, 552 us with procedural labels against 1332, 106 KB, 377 us with covers.
- **Root cause:** Allocating clipper, no caching.
- **Evidence:** As above. Lowered from P3: under 0.2 ms per frame on a desktop CPU, no visible effect shown; part of the difference is the geometry itself.
- **Remediation:** Clip into fixed stack arrays, or bake each label once in label space and transform it per frame.
- **Sources:** performance #5.

### FE-053

**Every return to the menu rebuilds the display and uploads the 4000x2500 board with a full mip chain; the decoded 40 MB stays in RAM during games**

- **Where:** `ps5/host/menu.cpp:1199-1248` (line 1246), `:221-223`; `ps5/host/image.cpp:33-51`; `ps5/host/main.cpp:289-300`
- **Component:** R-TEX-BOARD, R-IMAGE-CACHE, R-DISPLAY, E-RUNMENU
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** host-runtime (lavapipe timings, a proxy only); console-unverified
- **Description:** `runMenu` runs each time a game ends: instance, device, swapchain, ImGui context, four faces, and `uploadTexture` of the board (40 MB staged, a 13-level blit chain, a queue wait, about 53 MB of device memory). `loadImage` keeps the decoded pixels in a static map for the process.
- **Repro:** Play a game, quit to the menu; repeat.
- **Expected:** A fast return; the decoded board not kept while a game runs.
- **Actual:** Lavapipe: board upload 81 to 143 ms per entry, decode 154 ms on the first entry, logo 3 to 5 ms.
- **Root cause:** The display is torn down between menu and game (an architectural choice: the game opens its own Vulkan); the image cache keeps source pixels after upload.
- **Evidence:** As above. Lowered from P3.
- **Remediation:** Release the board's pixels after upload; consider a pre-mipped or smaller asset (IMP-04).
- **Sources:** performance #8.

### FE-054

**Strip and rack scrolling ease by a fixed 1/60 s step**

- **Where:** `ps5/host/frontend.cpp:782`, `:1094`; compare `:475`
- **Component:** P-STRIP, P-RACK-SHELVES
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static; console-unverified
- **Description / Actual:** `strip_x_ += (target - strip_x_) * min(1, 1/60 * 9)` once per frame, while the view blend uses `dt`. At 119.88 Hz the easing converges about twice as fast. The menu presents FIFO, so its rate follows the display.
- **Repro:** Set TV link to 119.88 Hz, restart, scroll.
- **Expected:** The same easing at both rates.
- **Root cause:** A hard-coded frame step.
- **Remediation:** Pass `dt` and use `1 - exp(-k * dt)`.
- **Sources:** performance #11.

### FE-055

**Transient fetch problems are cached as "no cover / no description" for 30 days or retried for ever**

- **Where:** `vendor/gopher64/src/psgopher.rs:731`, `:743`, `:653`, `:663`, `:687`, `:773-775`; `ps5/host/web.cpp:109-111`, `:122-125`
- **Component:** M-WEB-FETCH, R-WEB-DISK-CACHE, S-COVER-NONE, S-ABOUT-NONE
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** medium · **Evidence type:** static (no live server was exercised)
- **Description:** (1) A 200 answer that fails to parse as JSON returns 404, which `web.cpp` records as `.none` for 30 days; a body cut by an unclean close is accepted and then fails to parse. (2) A 3xx is treated as "no answer": the shared 60 s rest pauses every other fetch and it is asked again for ever. (3) Chunked bodies are recognised by an exact substring. (4) A Content-Length that does not parse becomes 0. (5) Only per-read timeouts exist. (6) An existing `.png` that does not decode settles as None with no age and is never fetched again.
- **Repro:** For (6): truncate any `cache/covers/<name>.png`; that cartridge has no cover until the file is deleted by hand.
- **Expected:** Only a definite "none" is cached as none; the rest is retried with back-off; a corrupt cached file is deleted and refetched.
- **Actual:** As described.
- **Root cause:** One status (404) for absence and for failure; hand-rolled header parsing.
- **Evidence:** Code as cited (the investigator's file path was corrected by the verifier). Lowered from P3: points 1 and 3 to 5 depend on answers that the two hosts are unlikely to send, and covers are written through `.part` and rename.
- **Remediation:** A distinct "try later" status; settle or follow 3xx; parse headers line by line; an overall deadline; delete and refetch an undecodable cached file.
- **Sources:** robustness #7.

### FE-056

**Settings write failures are invisible**

- **Where:** `ps5/host/frontend.cpp:1269-1273`; `ps5/host/quickmenu.cpp:239-242`; `ps5/host/menu.cpp:1268-1279`; `ps5/host/library.cpp:285-293`, `:647-655`
- **Component:** M-SETTINGS-TITLE, C-BENCH-SANDBOX, S-SANDBOX, C-QM-STATS
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static
- **Description:** BENCH's `psgopher.ini` rows and the quick menu's stats toggle ignore `TitleSettings::save`'s result. The sandbox toggle changes its flag only on success but always shows a notice that describes the current state as though the action had happened. Library and played saves fail silently too.
- **Repro:** Make the config folder read-only or fill the disk, then change Display in BENCH or toggle "Leave the sandbox".
- **Expected:** A failed save says so.
- **Actual:** No feedback; the value reverts at the next start.
- **Root cause:** Discarded results.
- **Evidence:** Code as cited. Lowered from P3: writes under `/data` rarely fail.
- **Remediation:** Report failures through `tell()` with the reason.
- **Sources:** robustness #11.

### FE-057

**`library.tsv` and `played.tsv` parsing gaps**

- **Where:** `ps5/host/library.cpp:175-178`, `:279-280`, `:377`, `:599`
- **Component:** M-LIBRARY-TSV, M-PLAYED
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static
- **Description:** `save()` replaces only tabs in the raw ROM header: a header byte 0x0A or 0x0D splits the row and `load()` drops both halves, as it drops rows longer than the 2048-byte line buffer. `readPlayed` accepts "nan" and "inf" through `%lf` and seconds are unbounded.
- **Repro:** A ROM whose header holds 0x0A; a `played.tsv` line starting with "nan".
- **Expected:** Control characters sanitised; numbers validated.
- **Actual:** The cartridge is missing from the cached list until the scan ends; odd play times.
- **Root cause:** Minimal sanitising and a fixed line buffer.
- **Evidence:** Code as cited.
- **Remediation:** Replace all control characters; read lines with `getline`; reject non-finite or negative seconds.
- **Sources:** robustness #15 (parts 1 and 2; parts 3 and 4 are FE-037 and FE-036).

### FE-058

**The OSD's underrun count is cumulative for the whole game and counts the quick menu's pause**

- **Where:** `ps5/host/bench.c:113-116`; `ps5/host/quickmenu.cpp:475-476`; `ps5/sdl3/psgopher_sdl3.cpp:864-869`
- **Component:** M-LIVE-STATS, V-OSD
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static; console-unverified
- **Description:** `live_now.audio_underruns = audio.underrun_grains;` is the stream's running total while every other figure is per window. While the quick menu blocks the emulation thread the audio callback keeps counting underruns.
- **Repro:** Stats overlay on, play, tap, wait a second, Resume.
- **Expected:** Recent underruns; the line returns to its normal colour.
- **Actual:** "N underrun(s)" in amber for the rest of the game; start-up underruns alone are enough.
- **Root cause:** No delta; the stream is not paused with the game.
- **Evidence:** Code as cited. Lowered from P3: a diagnostic overlay; the total resets with each game's stream.
- **Remediation:** Report the per-second delta; pause the stream or stop counting while the menu is up (IMP-24).
- **Sources:** overlay #4.

### FE-059

**The quick-menu pause is counted as one frame; play time includes the pause**

- **Where:** `ps5/host/bench.c:79-121`; `vendor/gopher64/parallel-rdp/interface.cpp:1024-1038`, `:1244-1245`; `ps5/host/main.cpp:297-300`
- **Component:** M-LIVE-STATS, P-OSD-GRAPH, E-MAIN, T-EMU-OVERLAY
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static; console-unverified
- **Description:** Menu frames are presented without `psgopher_bench_present`, so the first game present after Resume treats the whole pause as one frame: one red sample in the graph, VI/s and speed at a few percent for one window, CPU ms inflated by the menu's own work on the same thread. `main.cpp` times `play()` wall-clock, so `played.tsv` includes the menu time.
- **Repro:** Stats overlay on; tap, wait 10 s, Resume; watch the next second.
- **Expected:** Figures that describe the game.
- **Actual:** As described, for one 1 s window.
- **Root cause:** No hook shifts the live counters over the pause.
- **Evidence:** Code as cited. Lowered from P3; counting menu time as play time is debatable.
- **Remediation:** A `psgopher_live_pause(begin, end)` called when the menu closes; a paused-time counter that `addPlayed` subtracts.
- **Sources:** overlay #5.

### FE-060

**The benchmark logger reads the audio stream pointer without synchronisation while the game destroys it**

- **Where:** `ps5/sdl3/psgopher_sdl3.cpp:874-883`, `:922-930`; `ps5/host/bench.c:214-239`, `:286`; `ps5/host/main.cpp:269-272`
- **Component:** T-BENCH-LOGGER
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** medium · **Evidence type:** static
- **Description:** The logger thread calls `psgopher_sdl3_audio_stats_read` once a second: it checks the plain global `the_stream`, then locks `the_stream->lock`. `SDL_DestroyAudioStream` on the emulation thread nulls and deletes it; the logger is joined only after `play()` returns.
- **Repro:** `benchmark.txt` with a time limit; the logger's tick falls as the game tears down audio.
- **Expected:** No access to a freed stream.
- **Actual:** A data race and a possible use-after-free within a microsecond-wide window, benchmark mode only.
- **Root cause:** No lock around the global pointer.
- **Remediation:** A global mutex taken in both, or stop the logger before audio teardown.
- **Sources:** overlay #9.

### FE-061

**The OSD re-reads `psgopher.ini` once per game only if it was drawn in the previous game**

- **Where:** `ps5/host/quickmenu.cpp:494-502`, `:553-561`
- **Component:** R-OSD-CONTEXT, M-SETTINGS-TITLE, C-QM-STATS
- **Severity:** P4 · **Status:** Confirmed · **Confidence:** high · **Evidence type:** static
- **Description:** `g_osd_wanted` is set back to -1 only in `psgopher_osd_release`, which returns early when no OSD context was created.
- **Repro:** Play with stats off. Edit `psgopher.ini` over FTP to stats on. Start another game.
- **Expected:** The OSD is shown.
- **Actual:** No OSD until the quick menu's Stats row is used.
- **Root cause:** `if (!g_osd.context) return;` precedes the reset.
- **Evidence:** Code as cited. It shows only when the file is edited outside the title between games.
- **Remediation:** Reset `g_osd_wanted = -1` before the early return.
- **Sources:** overlay #11.

### FE-062

**The notice pill's width is unbounded**

- **Where:** `ps5/host/frontend.cpp:507-511`; `ps5/host/main.cpp:287-291`
- **Component:** F-NOTICE
- **Severity:** P4 · **Status:** Suspected · **Confidence:** medium · **Evidence type:** static
- **Description:** The pill is the measured text plus 40, centred, with no maximum and no wrap. Game-error notices carry the core's text plus the log path; copy notices carry a file name. About 190 characters of 16 px mono fit across 1920.
- **Repro:** Return from a game with a long error notice.
- **Expected:** The notice fits.
- **Actual:** The width is unbounded in the code; no realistic message was shown to exceed the screen.
- **Root cause:** No clamp.
- **Evidence:** Code as cited. Lowered from P3. The claim that notices are hidden behind the share window was mostly refuted (see the end).
- **Remediation:** Clamp and ellipsise to about 1200 px with the shared fit-to-width helper.
- **Sources:** visual #5.

### FE-063

**Menu display failures: a failed acquire loops for ever; a failed display start closes the title with only a log line**

- **Where:** `ps5/host/menu.cpp:1059-1064`, `:1141`, `:1200-1203`, `:1234-1239`; `ps5/host/main.cpp:292-294`
- **Component:** E-MENU-FRAME, S-ACQUIRE-FAILED, S-DISPLAY-FAILED, E-RUNMENU, E-MAIN
- **Severity:** P4 · **Status:** Suspected · **Confidence:** medium · **Evidence type:** static; console-unverified
- **Description:** `frame()` logs and returns on any acquire error, nothing recreates the swapchain, the present result is ignored; because `menu.draw` is never reached the menu can never finish. If `openDisplay` or the ImGui backend fails, `runMenu` returns an empty result, which `main` treats as "Close PSGopher".
- **Repro:** Console only (for example a display mode change while the list is up).
- **Expected:** Recreate on out-of-date; give up after repeated failures with an error; a display start failure distinguishable from a user's Close.
- **Actual:** By the code, an endless loop with a log line per frame, or a silent exit.
- **Root cause:** No recovery path.
- **Evidence:** The console's window system returns out-of-date only for a retired chain and waits without a timeout (`wsi_common_videoout.c:1320-1340`); nothing retires the menu's chain while it runs, so the loop is practically unreachable. The silent exit is a diagnostics gap. Lowered from P3.
- **Remediation:** Count consecutive failures and leave with an explicit error flag that `main` logs.
- **Sources:** robustness #8.

### FE-064

**States are written asynchronously: right after Take, the quick menu can show the slot empty or old, and Load can read a partly written file**

- **Where:** `ps5/host/quickmenu.cpp:127-135`, `:364`; `vendor/gopher64/src/savestates.rs:182-221`; `vendor/gopher64/src/device/vi.rs:188-214`
- **Component:** C-QM-SLOT-LOAD, S-QM-SLOT-EMPTY, O-ANSWER
- **Severity:** P4 · **Status:** Suspected · **Confidence:** medium · **Evidence type:** static; console-unverified
- **Description:** The menu's answer only sets flags; the save runs at the next frame and the file is compressed and written in a spawned task with a truncating write. The menu stats the slot files once per opening.
- **Repro:** Tap, Take, Probe 2; tap again at once and open Load, or choose Probe 2 there right away.
- **Expected:** The menu shows the probe just taken, and loading it gives that state.
- **Actual:** By the code, a short window in which the slot shows stale or a Load reads a truncated file ("Failed to load savestate").
- **Root cause:** No completion signal between the writer and the menu; a non-atomic write.
- **Evidence:** The window is tiny: reopening before the save frame needs a full new tap within about a frame, and a Load during the write needs navigation before a few-MB write finishes. Not demonstrable on the PC.
- **Remediation:** Write to a temporary name and rename in the core; optionally a "saving..." state.
- **Sources:** overlay #7.

---

## Refuted

Findings the verifiers rejected, and parts of confirmed findings that did not stand.

| Finding | Why it was refuted |
| --- | --- |
| visual #10: BENCH "This console" card ends about 4 px above the bottom edge; callout lines cross | The card ends at 1076 inside the 1080 stage; in `5-bench.png` nothing is clipped or hidden and every card is readable. The crossing lines and the shared anchor of the AV and TV callouts are layout choices. |
| robustness #14: `psgopher_cheats_of` can panic across the FFI boundary when its cut falls inside a multi-byte character | The slice would panic, but the cheat database is compiled in: no game's text exceeds 38,415 bytes against the 256 KiB buffer, and no game's text has a non-ASCII character. Latent only; it would matter if the embedded database changed. |
| visual #8 (parts): the selected spine's letter marker drawn at the unlifted position; filtered spines kept as 13% ghosts | Both match the design reference (`live-board.html:701`, `:731`). The rest of the finding is FE-042. |
| visual #5 (part): notices raised from the share form are hidden under the share window | In `12-share-form-filled.png` the window starts at about y=160; the notice text sits at about 138 to 157 and stays visible. The rest is FE-062. |
| input #14 (part): a change of strip position while the probe picker is open changes the game | `refresh()` keeps the selection by path; only the selected cartridge leaving the list triggers it (FE-007). |
| input #15, visual #9 (parts): undocumented extra close buttons, unused buttons without hints, no back button in RACK | Not defects. The rest is FE-043. |
| overlay #6 (part a): slot 0 missing from the quick menu | A documented choice (`quickmenu.cpp:70`). The rest is FE-009; FE-005 is what breaks the separation. |
| performance #10 (part): the scan and web threads compete with a running game | Plausible but not shown; kept as suspected inside FE-029. |
