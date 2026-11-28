/*
 * PSGopher - the title's own settings (title_settings.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include "title_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>
#include <unistd.h>

extern "C" void say(const char *format, ...) __attribute__((format(printf, 1, 2)));

namespace psgopher {

const char kTitleSettingsFile[] = "/data/psgopher/config/psgopher.ini";
/* final_filter's words in the file and PSGOPHER_FINAL_FILTER (interface.cpp reads them), by TitleSettings::finalFilter */
static const char *const kFinalFilterWords[5] = { "sharp", "bilinear", "nearest", "bicubic", "lanczos" };

const HotkeyAction kHotkeyActions[15] = {
	{ "save", "Save State" },         { "load", "Load State" },         { "exit", "Leave Game" },
	{ "ff", "Fast Forward" },         { "rewind", "Rewind" },           { "pak", "Change Pak" },
	{ "reset", "Reset" },             { "pause", "Pause" },             { "advance", "Frame Advance" },
	{ "voldown", "Volume Down" },     { "volup", "Volume Up" },         { "fps", "Show Frame Rate" },
	{ "crop", "Crop Letterbox" },     { "slotnext", "Next Save Slot" }, { "slotprev", "Previous Save Slot" },
};

const char *const kN64Inputs[19] = { "D Right", "D Left", "D Down", "D Up", "Start", "Z", "B", "A", "C Right", "C Left",
	                                 "C Down", "C Up", "R", "L", "Control Stick Right", "Control Stick Left", "Control Stick Down", "Control Stick Up",
	                                 "Hotkey Activator" };

void TitleSettings::load()
{
	const std::string path = file.empty() ? kTitleSettingsFile : file;
	FILE *f = fopen(path.c_str(), "r");
	if (!f) {
		save();
		return;
	}
	pictureDefaults = 0; /* (a file from before picture_defaults: see below) */
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		/* a path (dd_ipl) may hold spaces: the rest of its line, trimmed */
		if (!strncmp(line, "dd_ipl", 6)) {
			const char *eq = strchr(line, '=');
			std::string path = eq ? eq + 1 : "";
			while (!path.empty() && strchr(" \t\r\n", path.front()))
				path.erase(0, 1);
			while (!path.empty() && strchr(" \t\r\n", path.back()))
				path.pop_back();
			ddIpl = path;
			continue;
		}
		char key[64] = {}, value[128] = {};
		if (line[0] == '#' || sscanf(line, " %63[a-z_0-9] = %127s", key, value) != 2)
			continue;
		if (!strcmp(key, "display"))
			display = (!strcmp(value, "120") || !strcmp(value, "vrr")) ? value : "60";
		else if (!strcmp(key, "hdr"))
			hdr = !strcmp(value, "on");
		else if (!strcmp(key, "hdr_white"))
			hdrWhite = std::clamp(atoi(value), 80, 400);
		else if (!strcmp(key, "hdr_peak"))
			hdrPeak = std::clamp(atoi(value), 0, 4000);
		else if (!strcmp(key, "stats"))
			stats = !strcmp(value, "on");
		else if (!strcmp(key, "expansion_pak")) {
			expansionPak = !strcmp(value, "on") ? 1 : !strcmp(value, "off") ? 2 : 0;
			expansionPakRead = true;
		}
		else if (!strcmp(key, "audio_buffer"))
			audioBuffer = std::clamp(atoi(value), 2, 8) / 2 * 2;
		else if (!strcmp(key, "shader_compile"))
			/* "now" (compiled when first needed) is no longer offered: the game waited for each compile and
			   froze at scene changes (console, 2026-11-26); it is read as "1" */
			shaderCompile = !strcmp(value, "4") ? "4" : "1";
		else if (!strcmp(key, "driver_thread"))
			driverThread = !strcmp(value, "on");
		else if (!strcmp(key, "gpu_timing"))
			gpuTiming = !strcmp(value, "on");
		else if (!strcmp(key, "slow_capture"))
			slowCapture = strcmp(value, "off") != 0;
		else if (!strcmp(key, "wave32"))
			wave32 = !strcmp(value, "on");
		else if (!strcmp(key, "small_types"))
			smallTypes = !strcmp(value, "on");
		else if (!strcmp(key, "keep_gpu_memory"))
			keepGpuMemory = strcmp(value, "off") != 0;
		else if (!strcmp(key, "rdp_layout"))
			rdpLayout = !strcmp(value, "0") ? 0 : 2;
		else if (!strcmp(key, "rdp_threaded"))
			rdpThreaded = false; /* taken out: on the console it made the RDP's commands about 20 times costlier
			                        on the emulation thread (40-52% against 100% single-threaded, 2026-11-27) */
		else if (!strcmp(key, "final_filter"))
			finalFilter = !strcmp(value, "bilinear") ? 1 : !strcmp(value, "nearest") ? 2 : !strcmp(value, "bicubic") ? 3 : !strcmp(value, "lanczos") ? 4 : 0;
		else if (!strcmp(key, "post_aa"))
			postAa = !strcmp(value, "fxaa") ? 1 : 0;
		else if (!strcmp(key, "sharpening"))
			sharpening = !strcmp(value, "low") ? 1 : !strcmp(value, "medium") ? 2 : !strcmp(value, "high") ? 3 : 0;
		else if (!strcmp(key, "vi_bilinear"))
			viBilinear = !strcmp(value, "on") ? 1 : !strcmp(value, "off") ? 2 : 0;
		else if (!strcmp(key, "vi_aa"))
			viAa = strcmp(value, "off") != 0;
		else if (!strcmp(key, "dither_filter"))
			ditherFilter = strcmp(value, "off") != 0;
		else if (!strcmp(key, "divot_filter"))
			divotFilter = strcmp(value, "off") != 0;
		else if (!strcmp(key, "gamma_dither"))
			gammaDither = !strcmp(value, "on");
		else if (!strcmp(key, "picture_defaults"))
			pictureDefaults = atoi(value);
		else if (!strcmp(key, "native_texrect"))
			nativeTexRect = strcmp(value, "off") != 0;
		else if (!strcmp(key, "crop_overscan"))
			cropOverscan = atoi(value) >= 16 ? 16 : atoi(value) >= 8 ? 8 : 0;
		else if (!strcmp(key, "downsample"))
			downsample = std::clamp(atoi(value), 0, 3);
		else if (!strcmp(key, "cpu_timing"))
			cpuTiming = !strcmp(value, "fast") ? 1 : 0;
		else if (!strcmp(key, "rdp_timing"))
			rdpTiming = !strcmp(value, "instant") ? 1 : 0;
		else if (!strcmp(key, "overclock_speed"))
			overclockSpeed = std::clamp(atoi(value), 0, 6);
		else if (!strcmp(key, "deinterlace"))
			deinterlace = !strcmp(value, "bob") ? 0 : 1;
		else if (!strcmp(key, "hotkeys"))
			hotkeys = strcmp(value, "off") != 0;
		else if (!strcmp(key, "quickmenu_tap"))
			quickmenuTap = strcmp(value, "off") != 0;
		else if (!strcmp(key, "update_check"))
			updateCheck = strcmp(value, "off") != 0;
		else if (!strcmp(key, "early_warning_seen"))
			earlyWarningSeen = !strcmp(value, "on");
		else if (!strcmp(key, "write_helper"))
			writeHelper = strcmp(value, "off") != 0;
		else if (!strncmp(key, "hotkey_", 7))
			for (size_t i = 0; i < hotkey.size(); i++)
				if (!strcmp(key + 7, kHotkeyActions[i].key)) {
					char *end = nullptr;
					const long v = strtol(value, &end, 10);
					hotkey[i] = end == value ? -1 : std::clamp((int)v, -1, 17); /* ("off" and the like: none) */
				}
	}
	fclose(f);
	/* One test build (64d2321) saved native_texrect = off and deinterlace = bob as its defaults: the
	   upscaled texture rectangles drew lines between the strips games draw backgrounds with (the
	   console, 2026-11-26). A file from before picture_defaults 2 takes the defaults again, once. */
	if (pictureDefaults < 2) {
		nativeTexRect = true;
		deinterlace = 1;
		pictureDefaults = 2;
		save();
	}
	setenv("PSGOPHER_WRITE_HELPER", writeHelper ? "1" : "0", 1); /* (read by the share copy: write_broker.cpp) */
}

bool TitleSettings::save() const
{
	/* the share copy reads it when a game is started from the list, which is after the list's edit saves */
	setenv("PSGOPHER_WRITE_HELPER", writeHelper ? "1" : "0", 1);
	char text[16384];
	snprintf(text, sizeof(text),
		"# PSGopher's own settings (Gopher64's are in config.json). The game list's Settings write\n"
		"# this file; editing it over FTP works too.\n"
		"#\n"
		"# display: 60   59.94 Hz, the N64's own rate (PAL games' 50 Hz alternate one and two refreshes)\n"
		"#          120  119.88 Hz\n"
		"#          vrr  119.88 Hz with variable refresh: each frame shown when it is ready (NTSC and PAL\n"
		"#               alike), on a TV that has it; otherwise as 120\n"
		"# (read when the title starts: the display is opened once)\n"
		"display = %s\n"
		"\n"
		"# hdr: on sends HDR10 when the console's own output is HDR (its settings, and the TV); otherwise SDR\n"
		"hdr = %s\n"
		"# hdr_white: the nits white is shown at (80 to 400; 203 is HDR's reference)\n"
		"hdr_white = %d\n"
		"# hdr_peak: the nits the brightest parts of the picture are raised to (0 off; up to 4000)\n"
		"hdr_peak = %d\n"
		"\n"
		"# stats: on shows the stats overlay in a game (VIs, speed, CPU, waits, audio, frame times)\n"
		"stats = %s\n"
		"\n"
		"# expansion_pak: the N64's Expansion Pak (8 MB instead of 4): auto leaves it on, except for a game that\n"
		"# misbehaves with it (ps5/gamedata/expansion-pak.tsv); on and off force it. A game's own setting wins\n"
		"expansion_pak = %s\n"
		"\n"
		"# audio_buffer: the sound Gopher64 keeps queued, in VIs (a 60th of a second each): 2 is\n"
		"# Gopher64's own (the least delay), 4 about Crux2's, 6 and 8 for more room (more delay)\n"
		"audio_buffer = %d\n"
		"\n"
		"# shader_compile: parallel-rdp's shading pipelines for each state, until compiled drawn with\n"
		"# the slower general one: 1 one background thread (Gopher64's), 4 four\n"
		"shader_compile = %s\n"
		"# driver_thread: on records and submits the graphics commands on a thread of their own (Crux2's)\n"
		"driver_thread = %s\n"
		"# gpu_timing: on writes the GPU's time in each of parallel-rdp's stages to the boot log (gpu: lines)\n"
		"gpu_timing = %s\n"
		"# slow_capture: on measures a slow moment in a game (below 95%% for 3 s) into the boot log\n"
		"slow_capture = %s\n"
		"# wave32: on runs the GPU's compute shaders 32 wide (RADV's default is 64); small_types: on lets\n"
		"# parallel-rdp use 8 and 16-bit arithmetic (it turns that off on RADV). Measured with gpu_timing\n"
		"wave32 = %s\n"
		"small_types = %s\n"
		"# rdp_layout: how the upscaled copy of RDRAM is laid out: 2 a pixel's samples together (the default),\n"
		"# 0 parallel-rdp's own (a copy of RDRAM per sample, 8 MiB apart)\n"
		"rdp_layout = %d\n"
		"# keep_gpu_memory: on keeps the GPU's large memory blocks (the upscaled picture's) for reuse instead of\n"
		"# giving each back and asking the console for it again (which stalled games for seconds); off gives them back\n"
		"keep_gpu_memory = %s\n"
		"\n"
		"# The picture (BENCH, Video). final_filter: how the picture is scaled to the screen: sharp (crisp texels, a\n"
		"# one-pixel blend at their edges), bilinear, nearest, bicubic (Catmull-Rom) or lanczos (3 lobes); the CRT\n"
		"# shader, when on, takes the filter's place\n"
		"final_filter = %s\n"
		"# vi_bilinear: the N64's video interface's own smoothing when it scales: auto (off from 4x up), on, off\n"
		"vi_bilinear = %s\n"
		"# vi_aa, dither_filter, divot_filter: the video interface's anti-aliasing and two filters: on or off;\n"
		"# gamma_dither: its noise on the gamma curve (the N64's, a game's own choice): on or off\n"
		"vi_aa = %s\n"
		"dither_filter = %s\n"
		"divot_filter = %s\n"
		"gamma_dither = %s\n"
		"# native_texrect: on draws texture rectangles at the N64's resolution (the renderer's own way; off draws them upscaled)\n"
		"native_texrect = %s\n"
		"# crop_overscan: pixels cropped at each edge of the N64's picture: 0, 8 or 16\n"
		"crop_overscan = %d\n"
		"# downsample: steps the upscaled picture is scaled down by (0 off, 1 half, 2 a quarter, 3 an eighth; never below\n"
		"# the N64's resolution). SSAA downscaling (Gopher64's) takes it all the way whatever this says\n"
		"downsample = %d\n"
		"# deinterlace: bob shows each field on its own; blend merges each with the one before\n"
		"deinterlace = %s\n"
		"# picture_defaults: which defaults of the picture rows this file has taken (not a setting)\n"
		"picture_defaults = %d\n"
		"\n"
		"# hotkeys: Gopher64's, its Hotkey Activator (the touch pad by default) held and a button: on or off;\n"
		"# hotkey_<action>: the N64 button by Gopher64's input number (0 D Right, 1 D Left, 2 D Down, 3 D Up,\n"
		"# 4 Start, 5 Z, 6 B, 7 A, 8 C Right, 9 C Left, 10 C Down, 11 C Up, 12 R, 13 L), -1 none\n"
		"hotkeys = %s\n"
		"# quickmenu_tap: a touch pad tap opens the quick menu (on whenever nothing else leaves a game)\n"
		"quickmenu_tap = %s\n"
		"\n"
		"# write_helper: on has the write broker (write-broker.elf, a payload of its own) write a ROM copied from\n"
		"# a share into the play cache: the console holds back the title's own writes after about 1.3 GiB; the\n"
		"# broker's it doesn't. Off, or when it fails: the title writes the copy itself\n"
		"write_helper = %s\n"
		"\n"
		"# update_check: on asks GitHub whether a newer release is out each time the title starts (nothing is\n"
		"# downloaded without asking: BENCH, Console, Check for Updates)\n"
		"update_check = %s\n"
		"\n"
		"# early_warning_seen: on once the early-build warning at the first start was accepted (not a setting: delete the\n"
		"# line, or the file, to see it again)\n"
		"early_warning_seen = %s\n",
		display.c_str(), hdr ? "on" : "off", hdrWhite, hdrPeak, stats ? "on" : "off", expansionPak == 1 ? "on" : expansionPak == 2 ? "off" : "auto", audioBuffer,
		shaderCompile.c_str(), driverThread ? "on" : "off", gpuTiming ? "on" : "off", slowCapture ? "on" : "off", wave32 ? "on" : "off", smallTypes ? "on" : "off", rdpLayout, keepGpuMemory ? "on" : "off",
		kFinalFilterWords[std::clamp(finalFilter, 0, 4)], viBilinear == 1 ? "on" : viBilinear == 2 ? "off" : "auto",
		viAa ? "on" : "off", ditherFilter ? "on" : "off", divotFilter ? "on" : "off", gammaDither ? "on" : "off", nativeTexRect ? "on" : "off",
		cropOverscan, downsample, deinterlace == 1 ? "blend" : "bob", pictureDefaults, hotkeys ? "on" : "off", quickmenuTap ? "on" : "off", writeHelper ? "on" : "off", updateCheck ? "on" : "off", earlyWarningSeen ? "on" : "off");
	{
		std::string lines;
		for (size_t i = 0; i < hotkey.size(); i++)
			lines += std::string("hotkey_") + kHotkeyActions[i].key + " = " + std::to_string(hotkey[i]) + "\n";
		const size_t used = strlen(text);
		snprintf(text + used, sizeof(text) - used, "%s", lines.c_str());
	}
	{
		const size_t used = strlen(text);
		snprintf(text + used, sizeof(text) - used,
		         "\n# overclock_speed: the N64 CPU's clock when Overclock N64 CPU (Gopher64's switch, in its config.json) is on:\n"
		         "# 0 125 MHz (Gopher64's), 1 150 MHz, 2 187.5 MHz (twice the N64's), 3 250 MHz, 4 281.25 MHz (three times), 5 375 MHz, 6 468.75 MHz (five times)\n"
		         "overclock_speed = %d\n"
		         "# dd_ipl: the 64DD IPL (the drive's BIOS, a 4 MB dump) a disk starts with; empty: the first known IPL in\n"
		         "# /data/psgopher/data/64dd\n"
		         "dd_ipl = %s\n"
		         "# rdp_timing: accurate (the RDP takes the time Gopher64's model gives its drawing; heavy scenes lag as on an\n"
		         "# N64) or instant (drawing takes no emulated time, as in most emulators)\n"
		         "rdp_timing = %s\n"
		         "# cpu_timing: accurate (the CPU waits for memory as an N64's does: cache fills, uncached memory, cartridge\n"
		         "# reads) or fast (those waits left out, as in most emulators)\n"
		         "cpu_timing = %s\n",
		         overclockSpeed, ddIpl.c_str(), rdpTiming ? "instant" : "accurate", cpuTiming ? "fast" : "accurate");
	}
	{
		static const char *const kSharpeningWords[] = { "off", "low", "medium", "high" };
		const size_t used = strlen(text);
		snprintf(text + used, sizeof(text) - used,
		         "\n# post_aa: anti-aliasing at the screen's resolution, after the final filter: off or fxaa (NVIDIA FXAA 3.11)\n"
		         "post_aa = %s\n"
		         "# sharpening: AMD FidelityFX CAS at the screen's resolution, after post_aa: off, low, medium or high\n"
		         "sharpening = %s\n",
		         postAa == 1 ? "fxaa" : "off", kSharpeningWords[std::clamp(sharpening, 0, 3)]);
	}
	/* one write of a small file, beside it first */
	const std::string path = file.empty() ? kTitleSettingsFile : file;
	const std::string temporary = path + ".new";
	FILE *f = fopen(temporary.c_str(), "w");
	if (!f)
		return false;
	const size_t n = strlen(text);
	const bool ok = fwrite(text, 1, n, f) == n;
	fclose(f);
	if (!ok || rename(temporary.c_str(), path.c_str()) != 0) {
		unlink(temporary.c_str());
		return false;
	}
	chmod(path.c_str(), 0666);
	return true;
}

/* The settings a game can have of its own, by their BENCH rows' keys (one table: the rows, the per-game file and its
 * application all go through it) */
namespace {
struct Field {
	const char *key;
	int64_t (*get)(const TitleSettings &);
	void (*set)(TitleSettings &, int64_t);
};
const Field kFields[] = {
	{ "expansion_pak", [](const TitleSettings &t) -> int64_t { return t.expansionPak; }, [](TitleSettings &t, int64_t v) { t.expansionPak = (int)std::clamp<int64_t>(v, 0, 2); } },
	{ "shader_compile", [](const TitleSettings &t) -> int64_t { return atoi(t.shaderCompile.c_str()); },
	  [](TitleSettings &t, int64_t v) { t.shaderCompile = v == 4 ? "4" : "1"; } },
	{ "driver_thread", [](const TitleSettings &t) -> int64_t { return t.driverThread; }, [](TitleSettings &t, int64_t v) { t.driverThread = v != 0; } },
	{ "wave32", [](const TitleSettings &t) -> int64_t { return t.wave32; }, [](TitleSettings &t, int64_t v) { t.wave32 = v != 0; } },
	{ "small_types", [](const TitleSettings &t) -> int64_t { return t.smallTypes; }, [](TitleSettings &t, int64_t v) { t.smallTypes = v != 0; } },
	{ "rdp_layout", [](const TitleSettings &t) -> int64_t { return t.rdpLayout; }, [](TitleSettings &t, int64_t v) { t.rdpLayout = v == 0 ? 0 : 2; } },
	{ "keep_gpu_memory", [](const TitleSettings &t) -> int64_t { return t.keepGpuMemory; }, [](TitleSettings &t, int64_t v) { t.keepGpuMemory = v != 0; } },
	{ "audio_buffer", [](const TitleSettings &t) -> int64_t { return t.audioBuffer; },
	  [](TitleSettings &t, int64_t v) { t.audioBuffer = (int)std::clamp<int64_t>(v, 2, 8) / 2 * 2; } },
	{ "final_filter", [](const TitleSettings &t) -> int64_t { return t.finalFilter; }, [](TitleSettings &t, int64_t v) { t.finalFilter = (int)std::clamp<int64_t>(v, 0, 4); } },
	{ "post_aa", [](const TitleSettings &t) -> int64_t { return t.postAa; }, [](TitleSettings &t, int64_t v) { t.postAa = v == 1 ? 1 : 0; } },
	{ "sharpening", [](const TitleSettings &t) -> int64_t { return t.sharpening; }, [](TitleSettings &t, int64_t v) { t.sharpening = (int)std::clamp<int64_t>(v, 0, 3); } },
	{ "vi_bilinear", [](const TitleSettings &t) -> int64_t { return t.viBilinear; }, [](TitleSettings &t, int64_t v) { t.viBilinear = (int)std::clamp<int64_t>(v, 0, 2); } },
	{ "vi_aa", [](const TitleSettings &t) -> int64_t { return t.viAa; }, [](TitleSettings &t, int64_t v) { t.viAa = v != 0; } },
	{ "dither_filter", [](const TitleSettings &t) -> int64_t { return t.ditherFilter; }, [](TitleSettings &t, int64_t v) { t.ditherFilter = v != 0; } },
	{ "divot_filter", [](const TitleSettings &t) -> int64_t { return t.divotFilter; }, [](TitleSettings &t, int64_t v) { t.divotFilter = v != 0; } },
	{ "gamma_dither", [](const TitleSettings &t) -> int64_t { return t.gammaDither; }, [](TitleSettings &t, int64_t v) { t.gammaDither = v != 0; } },
	{ "native_texrect", [](const TitleSettings &t) -> int64_t { return t.nativeTexRect; }, [](TitleSettings &t, int64_t v) { t.nativeTexRect = v != 0; } },
	{ "crop_overscan", [](const TitleSettings &t) -> int64_t { return t.cropOverscan; },
	  [](TitleSettings &t, int64_t v) { t.cropOverscan = v >= 16 ? 16 : v >= 8 ? 8 : 0; } },
	{ "downsample", [](const TitleSettings &t) -> int64_t { return t.downsample; }, [](TitleSettings &t, int64_t v) { t.downsample = (int)std::clamp<int64_t>(v, 0, 3); } },
	{ "cpu_timing", [](const TitleSettings &t) -> int64_t { return t.cpuTiming; }, [](TitleSettings &t, int64_t v) { t.cpuTiming = v ? 1 : 0; } },
	{ "rdp_timing", [](const TitleSettings &t) -> int64_t { return t.rdpTiming; }, [](TitleSettings &t, int64_t v) { t.rdpTiming = v ? 1 : 0; } },
	{ "overclock_speed", [](const TitleSettings &t) -> int64_t { return t.overclockSpeed; },
	  [](TitleSettings &t, int64_t v) { t.overclockSpeed = (int)std::clamp<int64_t>(v, 0, 6); } },
	{ "deinterlace", [](const TitleSettings &t) -> int64_t { return t.deinterlace; }, [](TitleSettings &t, int64_t v) { t.deinterlace = v == 1 ? 1 : 0; } },
};
const Field *fieldOf(const std::string &key)
{
	for (const Field &f : kFields)
		if (key == f.key)
			return &f;
	return nullptr;
}
} // namespace

bool TitleSettings::isField(const std::string &key) { return fieldOf(key) != nullptr; }
int64_t TitleSettings::value(const std::string &key) const
{
	const Field *f = fieldOf(key);
	return f ? f->get(*this) : -1;
}
bool TitleSettings::assign(const std::string &key, int64_t v)
{
	const Field *f = fieldOf(key);
	if (f)
		f->set(*this, v);
	return f != nullptr;
}

void TitleSettings::applyDisplay(const std::string &titleDir) const
{
	/* Crux2's RADV (patches 0015/0016) reads the title's param.json, which declares 119.88 Hz: at
	 * 60 Hz it is given none (/dev/null, as OGBox does) and PS5_VIDEOOUT_59HZ; at 120 Hz and with
	 * variable refresh the title folder's copy, which outside the sandbox is no longer at /app0 */
	const std::string declared = titleDir + "/sce_sys/param.json";
	struct stat st;
	const bool there = stat(declared.c_str(), &st) == 0;
	if (display == "60") {
		setenv("PS5_VIDEOOUT_59HZ", "1", 1);
		setenv("PS5_VIDEOOUT_PARAM_JSON", "/dev/null", 1);
		say("display: 59.94 Hz (%s: display = 60)", kTitleSettingsFile);
		return;
	}
	if (there)
		setenv("PS5_VIDEOOUT_PARAM_JSON", declared.c_str(), 1);
	if (display == "vrr")
		setenv("PS5_VIDEOOUT_VRR", "1", 1);
	say("display: 119.88 Hz%s asked for; the driver reads the declaration from %s", display == "vrr" ? " with variable refresh" : "",
		there ? declared.c_str() : "/app0 (the title folder has no sce_sys/param.json)");
}

void TitleSettings::applyHdr() const
{
	/* (the audio buffer goes with it: the same moment, before each game) */
	setenv("PSGOPHER_AUDIO_VIS", std::to_string(audioBuffer).c_str(), 1);
	say("audio: %d VIs of sound kept queued (psgopher.ini: audio_buffer)", audioBuffer);
	/* parallel-rdp's shading pipelines and the driver's thread: read when the game's device starts */
	unsetenv("PARALLEL_RDP_FORCE_SYNC_SHADER");
	setenv("PSGOPHER_RDP_COMPILE_THREADS", shaderCompile.c_str(), 1);
	if (wave32)
		setenv("RADV_PERFTEST", "cswave32", 1);
	else
		unsetenv("RADV_PERFTEST");
	if (smallTypes)
		setenv("PARALLEL_RDP_SMALL_TYPES", "1", 1);
	else
		unsetenv("PARALLEL_RDP_SMALL_TYPES");
	/* the upscaled RDRAM's layout: read when the game's renderer makes its buffers (rdp_init logs it) */
	setenv("PSGOPHER_RDP_LAYOUT", rdpLayout == 0 ? "0" : "2", 1);
	/* parallel-rdp's command processing: read when the game's video starts (src/ui/video.rs; rdp_init logs which) */
	setenv("PSGOPHER_RDP_THREADED", "0", 1); /* (the threaded path stays in the core, unused) */
	/* read when the game's device starts (vulkan/memory_allocator.cpp logs what it does) */
	setenv("PSGOPHER_KEEP_GPU_MEMORY", keepGpuMemory ? "1" : "0", 1);
	/* the picture: read when the game's renderer starts (rdp_init logs them) */
	setenv("PSGOPHER_FINAL_FILTER", kFinalFilterWords[std::clamp(finalFilter, 0, 4)], 1);
	setenv("PSGOPHER_POST_AA", postAa == 1 ? "fxaa" : "off", 1);
	{
		static const char *const kSharpeningWords[] = { "off", "low", "medium", "high" };
		setenv("PSGOPHER_SHARPENING", kSharpeningWords[std::clamp(sharpening, 0, 3)], 1);
	}
	setenv("PSGOPHER_VI_BILINEAR", viBilinear == 1 ? "on" : viBilinear == 2 ? "off" : "auto", 1);
	setenv("PSGOPHER_VI_AA", viAa ? "1" : "0", 1);
	setenv("PSGOPHER_DITHER_FILTER", ditherFilter ? "1" : "0", 1);
	setenv("PSGOPHER_DIVOT_FILTER", divotFilter ? "1" : "0", 1);
	setenv("PSGOPHER_GAMMA_DITHER", gammaDither ? "1" : "0", 1);
	setenv("PSGOPHER_NATIVE_TEXRECT", nativeTexRect ? "1" : "0", 1);
	setenv("PSGOPHER_CROP_OVERSCAN", std::to_string(cropOverscan).c_str(), 1);
	setenv("PSGOPHER_DOWNSAMPLE", std::to_string(downsample).c_str(), 1);
	/* the CPU's clock with Overclock on: read when the game's CPU starts (cpu.rs) */
	static const char *const kOverclockHz[] = { "125000000", "150000000", "187500000", "250000000", "281250000", "375000000", "468750000" };
	setenv("PSGOPHER_OVERCLOCK_HZ", kOverclockHz[std::clamp(overclockSpeed, 0, 6)], 1);
	/* the 64DD IPL chosen in BENCH: read when a disk starts (lib.rs) */
	setenv("PSGOPHER_DD_IPL", ddIpl.c_str(), 1);
	/* the RDP's timing: read when the game's renderer starts (rdp_init logs it) */
	setenv("PSGOPHER_RDP_TIMING", rdpTiming ? "instant" : "accurate", 1);
	/* the CPU's memory stalls: read when the game's CPU starts (cpu.rs) */
	setenv("PSGOPHER_CPU_TIMING", cpuTiming ? "fast" : "accurate", 1);
	setenv("PSGOPHER_DEINTERLACE", deinterlace == 1 ? "blend" : "bob", 1);
	say("rdp: compute waves %d wide, 8/16-bit arithmetic %s%s", wave32 ? 32 : 64, smallTypes ? "on" : "off (parallel-rdp's choice on RADV)",
	    gpuTiming ? ", GPU timing on" : "");
	setenv("PSGOPHER_SLOW_CAPTURE", slowCapture ? "1" : "0", 1);
	if (gpuTiming)
		setenv("PARALLEL_RDP_BENCH", "2", 1);
	else
		unsetenv("PARALLEL_RDP_BENCH");
	if (driverThread)
		setenv("RADV_THREADED_RECORDING", "1", 1);
	else
		unsetenv("RADV_THREADED_RECORDING");
	/* the hotkeys, and the quick menu's tap (on when nothing else would leave the game) */
	{
		std::string table = std::string("on=") + (hotkeys ? "1" : "0");
		for (size_t i = 0; i < hotkey.size(); i++)
			table += std::string(",") + kHotkeyActions[i].key + "=" + std::to_string(hotkey[i]);
		setenv("PSGOPHER_HOTKEYS", table.c_str(), 1);
		const bool tap = canLeaveGame() ? quickmenuTap : true;
		setenv("PSGOPHER_QUICKMENU_TAP", tap ? "1" : "0", 1);
		say("hotkeys: %s; the quick menu's tap %s%s", hotkeys ? "on" : "off", tap ? "on" : "off",
		    tap && !quickmenuTap ? " (kept on: nothing else leaves the game)" : "");
	}
	say("rdp: shading pipelines %s; the driver %s", shaderCompile == "4" ? "compiled by four background threads" : "compiled by one background thread (Gopher64's)",
	    driverThread ? "records on a thread of its own" : "records on the emulation thread");
	if (!hdr) {
		unsetenv("PSGOPHER_HDR");
		return;
	}
	setenv("PSGOPHER_HDR", "1", 1);
	setenv("PSGOPHER_HDR_WHITE", std::to_string(hdrWhite).c_str(), 1);
	setenv("PSGOPHER_HDR_PEAK", std::to_string(hdrPeak).c_str(), 1);
	say("display: HDR10 asked for: white %d nits, highlights %d nits", hdrWhite, hdrPeak);
}

} // namespace psgopher
