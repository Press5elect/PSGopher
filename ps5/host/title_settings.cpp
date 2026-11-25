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
		if (line[0] == '#' || sscanf(line, " %63[a-z_] = %127s", key, value) != 2)
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
		"driver_thread = %s\n",
		display.c_str(), hdr ? "on" : "off", hdrWhite, hdrPeak, stats ? "on" : "off", audioBuffer,
		shaderCompile.c_str(), driverThread ? "on" : "off");
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
	if (driverThread)
		setenv("RADV_THREADED_RECORDING", "1", 1);
	else
		unsetenv("RADV_THREADED_RECORDING");
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
