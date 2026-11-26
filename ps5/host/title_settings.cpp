/*
 * PSGopher - the title's own settings (title_settings.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
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

const HotkeyAction kHotkeyActions[15] = {
	{ "save", "Save state" },         { "load", "Load state" },         { "exit", "Leave the game" },
	{ "ff", "Fast forward" },         { "rewind", "Rewind" },           { "pak", "Change pak" },
	{ "reset", "Reset" },             { "pause", "Pause" },             { "advance", "Frame advance" },
	{ "voldown", "Volume down" },     { "volup", "Volume up" },         { "fps", "Gopher64's frame rate" },
	{ "crop", "Crop the letterbox" }, { "slotnext", "Next save slot" }, { "slotprev", "Previous save slot" },
};

const char *const kN64Inputs[19] = { "D Right", "D Left", "D Down", "D Up", "Start", "Z", "B", "A", "C Right", "C Left",
	                                 "C Down", "C Up", "R", "L", "Stick Right", "Stick Left", "Stick Down", "Stick Up",
	                                 "Hotkey Activator" };

void TitleSettings::load()
{
	const std::string path = file.empty() ? kTitleSettingsFile : file;
	FILE *f = fopen(path.c_str(), "r");
	if (!f) {
		save();
		return;
	}
	char line[256];
	while (fgets(line, sizeof(line), f)) {
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
		else if (!strcmp(key, "audio_buffer"))
			audioBuffer = std::clamp(atoi(value), 2, 8) / 2 * 2;
		else if (!strcmp(key, "shader_compile"))
			shaderCompile = (!strcmp(value, "4") || !strcmp(value, "now")) ? value : "1";
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
		else if (!strcmp(key, "rdp_layout"))
			rdpLayout = (!strcmp(value, "1") || !strcmp(value, "2")) ? atoi(value) : 0;
		else if (!strcmp(key, "hotkeys"))
			hotkeys = strcmp(value, "off") != 0;
		else if (!strcmp(key, "quickmenu_tap"))
			quickmenuTap = strcmp(value, "off") != 0;
		else if (!strncmp(key, "hotkey_", 7))
			for (size_t i = 0; i < hotkey.size(); i++)
				if (!strcmp(key + 7, kHotkeyActions[i].key)) {
					char *end = nullptr;
					const long v = strtol(value, &end, 10);
					hotkey[i] = end == value ? -1 : std::clamp((int)v, -1, 17); /* ("off" and the like: none) */
				}
	}
	fclose(f);
}

bool TitleSettings::save() const
{
	char text[4096];
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
		"# audio_buffer: the sound Gopher64 keeps queued, in VIs (a 60th of a second each): 2 is\n"
		"# Gopher64's own (the least delay), 4 about Crux2's, 6 and 8 for more room (more delay)\n"
		"audio_buffer = %d\n"
		"\n"
		"# shader_compile: parallel-rdp's shading pipelines for each state, until compiled drawn with\n"
		"# the slower general one: 1 one background thread (Gopher64's), 4 four, now compiled when first\n"
		"# needed (a short pause the first time; kept in the shader cache for the next start)\n"
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
		"# rdp_layout: how parallel-rdp lays out the upscaled picture's memory, an experiment for 8x: 0 its\n"
		"# own (a copy of RDRAM per sample, 8 MiB apart), 1 the copies padded apart, 2 a pixel's samples together\n"
		"rdp_layout = %d\n"
		"\n"
		"# hotkeys: Gopher64's, its Hotkey Activator (the touch pad by default) held and a button: on or off;\n"
		"# hotkey_<action>: the N64 button by Gopher64's input number (0 D Right, 1 D Left, 2 D Down, 3 D Up,\n"
		"# 4 Start, 5 Z, 6 B, 7 A, 8 C Right, 9 C Left, 10 C Down, 11 C Up, 12 R, 13 L), -1 none\n"
		"hotkeys = %s\n"
		"# quickmenu_tap: a touch pad tap opens the quick menu (on whenever nothing else leaves a game)\n"
		"quickmenu_tap = %s\n",
		display.c_str(), hdr ? "on" : "off", hdrWhite, hdrPeak, stats ? "on" : "off", audioBuffer,
		shaderCompile.c_str(), driverThread ? "on" : "off", gpuTiming ? "on" : "off", slowCapture ? "on" : "off", wave32 ? "on" : "off", smallTypes ? "on" : "off", rdpLayout, hotkeys ? "on" : "off", quickmenuTap ? "on" : "off");
	{
		std::string lines;
		for (size_t i = 0; i < hotkey.size(); i++)
			lines += std::string("hotkey_") + kHotkeyActions[i].key + " = " + std::to_string(hotkey[i]) + "\n";
		const size_t used = strlen(text);
		snprintf(text + used, sizeof(text) - used, "%s", lines.c_str());
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
	if (shaderCompile == "now") {
		setenv("PARALLEL_RDP_FORCE_SYNC_SHADER", "1", 1);
		unsetenv("PSGOPHER_RDP_COMPILE_THREADS");
	} else {
		unsetenv("PARALLEL_RDP_FORCE_SYNC_SHADER");
		setenv("PSGOPHER_RDP_COMPILE_THREADS", shaderCompile.c_str(), 1);
	}
	if (wave32)
		setenv("RADV_PERFTEST", "cswave32", 1);
	else
		unsetenv("RADV_PERFTEST");
	if (smallTypes)
		setenv("PARALLEL_RDP_SMALL_TYPES", "1", 1);
	else
		unsetenv("PARALLEL_RDP_SMALL_TYPES");
	/* the upscaled RDRAM's layout: read when the game's renderer makes its buffers (rdp_init logs it) */
	if (rdpLayout)
		setenv("PSGOPHER_RDP_LAYOUT", std::to_string(rdpLayout).c_str(), 1);
	else
		unsetenv("PSGOPHER_RDP_LAYOUT");
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
	say("rdp: shading pipelines %s; the driver %s", shaderCompile == "now" ? "compiled when first needed" :
	    shaderCompile == "4" ? "compiled by four background threads" : "compiled by one background thread (Gopher64's)",
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
