/*
 * PSGopher - the title's own settings, /data/psgopher/config/psgopher.ini (Gopher64's are its
 * config.json): the display's refresh and HDR10. The game list's Settings edit them; the file stays
 * one that can be edited over FTP.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace psgopher {

struct TitleSettings {
	std::string display{ "60" }; // 60, 120 or vrr: from the title's start (the display opens once)
	bool hdr{ false };           // HDR10 when the console's output is HDR: from the next game
	int hdrWhite{ 203 };         // nits, 80..400
	int hdrPeak{ 1000 };         // nits, 0 (off) or up to 4000
	bool stats{ false };         // the stats overlay in a game
	int expansionPak{ 0 };       // the Expansion Pak (expansion_pak.h): 0 Auto (on, except for a game that is incompatible
	                             // with it), 1 On, 2 Off; applied to Gopher64's setting when a game starts (game_settings.cpp)
	int audioBuffer{ 4 };        // the audio Gopher64 keeps queued, in VIs (2 is its own; 4, 6, 8)
	std::string shaderCompile{ "1" }; // parallel-rdp's specialised shading pipelines: "1" one background
	                                  // thread (Gopher64's), "4" four ("now" is read as "1")
	bool driverThread{ false };  // RADV records and submits on a thread of its own (Crux2's switch)
	bool gpuTiming{ false };     // parallel-rdp's GPU timestamps, each stage's time in the boot log
	bool slowCapture{ true };    // a slow moment in a game (below 95% for 3 s) measured into the boot log
	bool wave32{ false };        // RADV runs compute shaders 32 wide (RADV_PERFTEST=cswave32; its default is 64)
	bool smallTypes{ false };    // parallel-rdp's 8/16-bit arithmetic (it turns it off on RADV by default)
	bool keepGpuMemory{ true }; // large GPU memory blocks are kept for reuse instead of given back to the kernel
	                            // (PSGOPHER_KEEP_GPU_MEMORY: its allocations stalled the game for seconds)
	int rdpLayout{ 2 };          // the upscaled RDRAM's layout (PSGOPHER_RDP_LAYOUT): 2 per pixel (measured at
	                             // 8x: depth blending ~15 times faster), 0 parallel-rdp's own
	bool rdpThreaded{ false };   // parallel-rdp's command processing on a thread of its own (PSGOPHER_RDP_THREADED;
	                             // vendor/gopher64/parallel-rdp/interface.cpp: rdp_threaded); off: on the emulation thread
	/* the picture (BENCH > Video; vendor/gopher64/parallel-rdp/interface.cpp reads them once a game) */
	int finalFilter{ 0 };        // the final blit's filter (PSGOPHER_FINAL_FILTER): 0 sharp bilinear, 1 bilinear, 2 nearest,
	                             // 3 bicubic (Catmull-Rom), 4 lanczos (3 lobes, anti-ringing)
	int postAa{ 0 };             // post anti-aliasing at the screen's resolution (PSGOPHER_POST_AA): 0 off, 1 FXAA
	int sharpening{ 0 };         // sharpening, FidelityFX CAS (PSGOPHER_SHARPENING): 0 off, 1 low, 2 medium, 3 high
	int viBilinear{ 0 };         // the video interface's bilinear scaling (PSGOPHER_VI_BILINEAR): 0 auto (off from 4x up), 1 on, 2 off
	bool viAa{ true };           // the video interface's anti-aliasing (PSGOPHER_VI_AA)
	bool ditherFilter{ true };   // its dither filter (PSGOPHER_DITHER_FILTER)
	bool divotFilter{ true };    // its divot filter (PSGOPHER_DIVOT_FILTER)
	bool gammaDither{ false };   // its gamma dither (PSGOPHER_GAMMA_DITHER)
	bool nativeTexRect{ true }; // texture rectangles drawn at the N64's resolution (PSGOPHER_NATIVE_TEXRECT)
	int cropOverscan{ 0 };       // pixels cropped at each edge: 0, 8 or 16 (PSGOPHER_CROP_OVERSCAN)
	std::string ddIpl;           // the 64DD IPL chosen in BENCH (a file of 4 MB); "" Gopher64 looks in data/64dd (PSGOPHER_DD_IPL)
	int overclockSpeed{ 0 };
	int rdpTiming{ 0 };
	int cpuTiming{ 0 };
	int viTiming{ 0 };           // 0 accurate (the field as the VI's registers give it), 1 compatible (a field shorter than a TV's lasts 1/60 s) (PSGOPHER_VI_TIMING)          // 0 accurate (the CPU's memory stalls charged), 1 fast (left out) (PSGOPHER_CPU_TIMING)          // 0 accurate (Gopher64's model of the RDP's speed), 1 instant (PSGOPHER_RDP_TIMING)     // the clock with Overclock N64 CPU on: 0 125 MHz (Gopher64's), 1 150, 2 187.5, 3 250, 4 281.25, 5 375, 6 468.75 (PSGOPHER_OVERCLOCK_HZ)
	int downsample{ 0 };         // steps the upscaled picture is scaled down by, 0..3 (PSGOPHER_DOWNSAMPLE); SSAA
	                             // downscaling takes it all the way, whatever this says
	int deinterlace{ 1 };        // 0 bob, 1 blend (the default)
	int pictureDefaults{ 2 };    // which picture defaults the file has taken (2: texture rectangles native, blend) (PSGOPHER_DEINTERLACE)
	/* the controller's hotkeys (Gopher64's: its Hotkey Activator held, the touch pad by default, and
	 * a button): on or off, and each action's N64 button (an input profile index, -1 none); the
	 * actions are kHotkeyActions' (psgopher.rs's HOTKEY_ACTIONS, in its order) */
	bool hotkeys{ true };
	std::array<int, 15> hotkey{ { 13, 12, 4, 5, 9, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };
	bool writeHelper{ true };    // a ROM copied from a share is written by the write broker (a payload of its own:
	                             // the console holds the title's own writes back after ~1.3 GiB; OGBox's decision: on)
	bool updateCheck{ true };    // the title asks GitHub for a newer release at its start (OGBox's default: on)
	bool earlyWarningSeen{ false }; // the one-time early-build warning was accepted (frontend.cpp); a reset config shows it again
	bool quickmenuTap{ true };   // a touch pad tap opens the quick menu (kept on when nothing else leaves a game)
	/* a game can be left: by the quick menu's tap or the hotkey's Leave game */
	bool canLeaveGame() const { return quickmenuTap || (hotkeys && hotkey[2] >= 0); }
	bool expansionPakRead{ false }; // load() found an expansion_pak line (else the file is from before it: main.cpp takes Gopher64's old row over)
	std::string file;            // "" is /data/psgopher/config/psgopher.ini (the host tests give another)

	/* A setting by its BENCH row's key (the rows that a game can have of its own: game_settings.h). value() is -1 for
	 * a key that is not one; assign() is false for a key that is not one (or a value it does not take). */
	static bool isField(const std::string &key);
	int64_t value(const std::string &key) const;
	bool assign(const std::string &key, int64_t v);

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
