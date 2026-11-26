/*
 * PSGopher - the title's own settings, /data/psgopher/config/psgopher.ini (Gopher64's are its
 * config.json): the display's refresh and HDR10. The game list's Settings edit them; the file stays
 * one that can be edited over FTP.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <array>
#include <string>

namespace psgopher {

struct TitleSettings {
	std::string display{ "60" }; // 60, 120 or vrr: from the title's start (the display opens once)
	bool hdr{ false };           // HDR10 when the console's output is HDR: from the next game
	int hdrWhite{ 203 };         // nits, 80..400
	int hdrPeak{ 1000 };         // nits, 0 (off) or up to 4000
	bool stats{ false };         // the stats overlay in a game
	int audioBuffer{ 4 };        // the audio Gopher64 keeps queued, in VIs (2 is its own; 4, 6, 8)
	std::string shaderCompile{ "1" }; // parallel-rdp's specialised shading pipelines: "1" one background
	                                  // thread (Gopher64's), "4" four, "now" compiled when first needed
	bool driverThread{ false };  // RADV records and submits on a thread of its own (Crux2's switch)
	bool gpuTiming{ false };     // parallel-rdp's GPU timestamps, each stage's time in the boot log
	bool slowCapture{ true };    // a slow moment in a game (below 95% for 3 s) measured into the boot log
	bool wave32{ false };        // RADV runs compute shaders 32 wide (RADV_PERFTEST=cswave32; its default is 64)
	bool smallTypes{ false };    // parallel-rdp's 8/16-bit arithmetic (it turns it off on RADV by default)
	int rdpLayout{ 0 };          // the upscaled RDRAM's layout, an experiment for 8x (PSGOPHER_RDP_LAYOUT):
	                             // 0 parallel-rdp's, 1 padded stride between the samples' copies, 2 per pixel
	/* the controller's hotkeys (Gopher64's: its Hotkey Activator held, the touch pad by default, and
	 * a button): on or off, and each action's N64 button (an input profile index, -1 none); the
	 * actions are kHotkeyActions' (psgopher.rs's HOTKEY_ACTIONS, in its order) */
	bool hotkeys{ true };
	std::array<int, 15> hotkey{ { 13, 12, 4, 5, 9, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
	bool quickmenuTap{ true };   // a touch pad tap opens the quick menu (kept on when nothing else leaves a game)
	/* a game can be left: by the quick menu's tap or the hotkey's Leave game */
	bool canLeaveGame() const { return quickmenuTap || (hotkeys && hotkey[2] >= 0); }
	std::string file;            // "" is /data/psgopher/config/psgopher.ini (the host tests give another)

	/* Reads the file (written with the defaults when it is missing). */
	void load();
	/* Writes it whole. */
	bool save() const;
	/* The driver's display switches: once, before anything opens the display. title_dir is where
	 * the title folder is now (its sce_sys/param.json declares 119.88 Hz). */
	void applyDisplay(const std::string &titleDir) const;
	/* parallel-rdp's HDR10 switches and the audio buffer: before each game
	 * (vendor/gopher64/parallel-rdp/interface.cpp, src/ui/audio.rs). */
	void applyHdr() const;
};

extern const char kTitleSettingsFile[];

/* the hotkeys' actions, in psgopher.rs's order: the ini's name, the label */
struct HotkeyAction {
	const char *key, *label;
};
extern const HotkeyAction kHotkeyActions[15];
/* the N64's buttons by input profile index (0 D Right .. 17 Stick Up), as Gopher64 names them */
extern const char *const kN64Inputs[19];

} // namespace psgopher
