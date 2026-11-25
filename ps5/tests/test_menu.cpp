/*
 * PSGopher - the game list on the PC: ps5/host/menu.cpp built with PSGOPHER_MENU_HOST draws offscreen
 * through the PC's Vulkan (lavapipe in the tests), with Dear ImGui's own Vulkan backend as on the
 * console. A scripted pad goes through every screen (the tabs, the settings, the shares, a share's
 * form and the on-screen keyboard), pictures are taken on the way, and the checks are what the
 * screens did: Gopher64's settings changed through the bridge, the keyboard's text, the game played.
 * Under AddressSanitizer and UBSan; a Dear ImGui assertion fails the test.
 *
 *   test_menu <work folder> <pictures folder>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "library.h"
#include "web.h"
#include "menu.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <ctime>

extern "C" {
#include "platform.h"
}
#include "ps5_vfs.h"

static int failures = 0;
#define CHECK(x)                                                                    \
	do {                                                                            \
		if (!(x)) {                                                                 \
			std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x);     \
			failures++;                                                             \
		}                                                                           \
	} while (0)

/* ---------------------------------------------------------------- the PS5 layer, stood in for */

extern "C" void say(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	std::vfprintf(stdout, format, args);
	va_end(args);
	std::fputc('\n', stdout);
}

extern "C" double now_seconds(void)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

extern "C" {
const char *psgopher_menu_host_shot = nullptr;
}

/* The script: each step holds these buttons for some frames, then lets go for as many; a picture
 * is taken at the end of a step that names one. */
struct Step {
	uint32_t buttons;
	const char *shot;
	int frames;
};
static std::vector<Step> g_script;
static size_t g_step = 0;
static int g_frame_in_step = 0;
static std::string g_pictures;
static std::string g_shot_path;
static uint32_t g_held = 0;
static bool g_script_done = false;

extern "C" void pad_poll(struct pad *pad)
{
	uint32_t held = 0;
	if (g_step < g_script.size()) {
		const Step &s = g_script[g_step];
		const int hold = s.frames;
		if (g_frame_in_step < hold)
			held = s.buttons;
		g_frame_in_step++;
		if (g_frame_in_step >= hold * 2 + 2) {
			if (s.shot) {
				g_shot_path = g_pictures + "/" + s.shot + ".ppm";
				psgopher_menu_host_shot = g_shot_path.c_str();
			}
			g_step++;
			g_frame_in_step = 0;
		}
	} else {
		g_script_done = true;
		static int idle = 0;
		if (++idle > 300) {
			std::fprintf(stderr, "FAILED: the script ended and the list did not return\n");
			std::exit(1);
		}
	}
	*pad = {};
	pad->pressed = held & ~g_held;
	pad->held = held;
	g_held = held;
}

/* ---------------------------------------------------------------- Gopher64's settings, stood in for */

static std::map<std::string, int64_t> g_settings = {
	{ "video.upscale", 1 }, { "video.ssaa", 0 }, { "video.integer_scaling", 0 }, { "video.widescreen", 0 },
	{ "video.crt", 0 }, { "emulation.overclock", 0 }, { "emulation.disable_expansion_pak", 0 },
	{ "emulation.rewind", 0 }, { "emulation.usb", 0 }, { "input.emulate_vru", 0 },
	{ "input.controller_enabled.0", 1 }, { "input.controller_enabled.1", 0 },
	{ "input.controller_enabled.2", 0 }, { "input.controller_enabled.3", 0 },
};
static int g_setting_reads = 0;

extern "C" int64_t psgopher_setting_get(const char *name)
{
	g_setting_reads++;
	const auto it = g_settings.find(name);
	return it == g_settings.end() ? -1 : it->second;
}

extern "C" bool psgopher_setting_set(const char *name, int64_t value)
{
	const auto it = g_settings.find(name);
	if (it == g_settings.end())
		return false;
	if (std::string(name) == "video.upscale" && value != 1 && value != 2 && value != 4 && value != 8)
		return false;
	it->second = value;
	return true;
}

/* ---------------------------------------------------------------- Gopher64's facts, stood in for */

struct PsgopherRomFacts {
	uint32_t cic, save, rumble;
};
extern "C" bool psgopher_rom_facts(const uint8_t *rom, size_t len, PsgopherRomFacts *out)
{
	*out = { rom[0x3C] == 'B' ? 6105u : 6102u, rom[0x3C] == 'B' ? 32u : 1u | 4u, rom[0x3C] == 'K' ? 1u : 0u };
	return len >= 0x1000;
}

/* the core's cheats, covers and descriptions, stood in for */
extern "C" size_t psgopher_cheats_of(const char *, char *buffer, size_t size)
{
	static const char text[] = "Infinite Lives\tStays at 3.\t\t1\t\nStart on Level\t\t1|2|3\t0\t\n";
	snprintf(buffer, size, "%s", text);
	return sizeof(text) - 1;
}
extern "C" bool psgopher_cheat_set(const char *, const char *, bool, const char *) { return true; }
/* the played game has a cover (the title's logo stands in), the others none */
extern "C" int psgopher_cover(const char *name, const char *file)
{
	if (strcmp(name, "Kart Champs (U)") != 0)
		return 404;
	return std::system(("cp '" PSGOPHER_SOURCE "/ps5/art/logo.png' '" + std::string(file) + "'").c_str()) == 0 ? 200 : 500;
}
extern "C" int psgopher_about(const char *name, char *buffer, size_t size)
{
	snprintf(buffer, size, "%s\n%s is a racing video game for the Nintendo 64, stood in for by the tests: a description of "
	         "the kind Wikipedia's summaries give, long enough to wrap over the three lines the panel shows of it and "
	         "then some more.", name, name);
	return 200;
}

/* ---------------------------------------------------------------- ps5vfs, stood in for (no shares) */

extern "C" void ps5vfs_set_shadow_path(const char *) {}
extern "C" void ps5vfs_set_log(void (*)(const char *)) {}
extern "C" int ps5vfs_is_virtual(const char *path) { return strncmp(path, "/smb/", 5) == 0; }
extern "C" const char *ps5vfs_mount_point(unsigned) { return nullptr; }
extern "C" ps5vfs_dir *ps5vfs_opendir(const char *) { return nullptr; }
extern "C" int ps5vfs_readdir(ps5vfs_dir *, struct ps5vfs_dirent *) { return 0; }
extern "C" void ps5vfs_closedir(ps5vfs_dir *) {}
extern "C" FILE *ps5vfs_fopen(const char *, const char *) { return nullptr; }
extern "C" size_t ps5vfs_fread(void *, size_t, size_t, FILE *) { return 0; }
extern "C" int ps5vfs_ferror(FILE *) { return 1; }
extern "C" int ps5vfs_fclose(FILE *) { return 0; }

static void write(const std::string &path, const std::string &bytes)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
}

int main(int argc, char **argv)
{
	const std::string work = argc > 1 ? argv[1] : "/tmp/psgopher-menu-test";
	g_pictures = argc > 2 ? argv[2] : work;
	if (std::system(("rm -rf '" + work + "' && mkdir -p '" + work + "/data/psgopher/roms' '" + work +
			"/data/psgopher/cache' '" + g_pictures + "'").c_str()) != 0)
		return 2;
	/* a list of the kind a console has: ROM files named as dumps are, with headers */
	static const char *const names[] = { "Aero Racer (U)", "Bomb Squad 64 (E)", "Cave Diver (J)", "Dino Park (U) [!]",
		"Echo Base (U)", "Fjord Fishing (E)", "Gem Keeper (U)", "Hover Cars (J)", "Ice Climb 64 (U)", "Jungle Run (E)",
		"Kart Champs (U)", "Lunar Lander 64 (U)", "Moto Trial (E)", "Night Ops (U)", "Orbit (J)" };
	for (const char *n : names) {
		std::string rom(1024 * (strlen(n) * 37 % 900 + 100), '\0');
		const unsigned char magic[4] = { 0x80, 0x37, 0x12, 0x40 };
		memcpy(&rom[0], magic, 4);
		std::string header = n;
		header = header.substr(0, header.find(" ("));
		for (char &c : header)
			c = (char)std::toupper((unsigned char)c);
		memset(&rom[0x20], ' ', 20);
		memcpy(&rom[0x20], header.data(), std::min<size_t>(20, header.size()));
		const char region = strstr(n, "(E)") ? 'P' : strstr(n, "(J)") ? 'J' : 'E';
		const char code[5] = { 'N', header[0], header[1] == ' ' ? 'X' : header[1], region, 0 };
		memcpy(&rom[0x3B], code, 4);
		write(work + "/data/psgopher/roms/" + n + ".z64", rom);
	}
	/* one game played (it leads the strip), two probes of another */
	if (std::system(("mkdir -p '" + work + "/data/psgopher/data/states'").c_str()) != 0)
		return 2;
	write(work + "/data/psgopher/cache/played.tsv", "36000\t" + std::to_string((long long)time(nullptr) - 86400) + "\tKart Champs (U)\n");
	for (int slot : { 1, 3 }) {
		std::string state(8192, '\0');
		for (size_t i = 0; i < state.size(); i++)
			state[i] = (char)((i * 2654435761u >> (slot * 3)) & 0xff);
		write(work + "/data/psgopher/data/states/BOMB SQUAD 64-0123.state" + std::to_string(slot), state);
	}
	psgopher::webStart(work + "/data/psgopher/cache/covers");
	psgopher::libraryUseRoot(work + "/data");
	psgopher::library().load();
	psgopher::library().rescan();
	for (int i = 0; i < 300 && psgopher::library().scanning(); i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

	const uint32_t none = 0;
	g_script = {
		{ none, nullptr, 6 },
		{ none, "1-home", 3 },
		{ PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { none, nullptr, 6 }, { none, "2-home-moved", 3 },
		{ PAD_R1, nullptr, 2 }, { none, nullptr, 6 }, { none, "3-rack", 3 },
		{ PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 },
		{ PAD_SQUARE, nullptr, 2 }, { PAD_OPTIONS, nullptr, 2 }, { none, "4-rack-sorted-filtered", 3 },
	};
	for (int i = 0; i < 6; i++)
		g_script.push_back({ PAD_OPTIONS, nullptr, 2 }); /* the filter back to All */
	for (int i = 0; i < 3; i++)
		g_script.push_back({ PAD_SQUARE, nullptr, 2 }); /* the sort back to A-Z */
	for (const Step &st : std::vector<Step>{
			{ PAD_R1, nullptr, 2 }, { none, nullptr, 6 }, { none, "5-bench", 3 },
			{ PAD_CROSS, nullptr, 2 },                     /* the crystal: 125 MHz */
			{ PAD_DOWN, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, /* the video stage: 2x */
			{ none, "6-bench-changed", 3 } })
		g_script.push_back(st);
	for (int i = 0; i < 17; i++) /* down to "Network shares" */
		g_script.push_back({ PAD_DOWN, nullptr, 2 });
	for (const Step &st : std::vector<Step>{
			{ none, "7-bench-console", 3 },
			{ PAD_CROSS, "8-shares", 3 },
			{ PAD_CROSS, "9-share-form", 3 },          /* Add a share */
			{ PAD_DOWN, nullptr, 2 },                  /* Server */
			{ PAD_CROSS, "10-keyboard", 3 },           /* the keyboard */
			{ PAD_CROSS, nullptr, 2 },                 /* types 'q' (the first key of the second row) */
			{ PAD_RIGHT, nullptr, 2 }, { PAD_CROSS, nullptr, 2 }, /* 'w' */
			{ PAD_SQUARE, nullptr, 2 },                /* deletes it */
			{ PAD_UP, nullptr, 2 }, { PAD_CROSS, "11-keyboard-typed", 3 }, /* '2' */
			{ PAD_OPTIONS, "12-share-form-filled", 3 }, /* done */
			{ PAD_CIRCLE, nullptr, 2 },                /* the form left without saving */
			{ PAD_CIRCLE, nullptr, 2 },                /* the shares left: the bench */
			{ PAD_L1, nullptr, 2 }, { PAD_L1, nullptr, 2 }, { none, nullptr, 6 }, /* home */
			{ PAD_SQUARE, "13-home-probes", 3 },       /* the probe picker (Bomb Squad 64 has two) */
			{ PAD_CIRCLE, nullptr, 2 },
			{ PAD_CROSS, nullptr, 2 } })                /* power on */
		g_script.push_back(st);

	setenv("PSGOPHER_ART", (std::string(PSGOPHER_SOURCE) + "/ps5/art").c_str(), 1);
	const psgopher::MenuResult r = psgopher::runMenu("A notice from the last game");
	std::printf("menu returned: play %d, %s (probe %d); Gopher64's settings read %d times\n", r.play, r.rom.c_str(), r.load_state,
	            g_setting_reads);
	CHECK(r.play);
	CHECK(r.rom == work + "/data/psgopher/roms/Bomb Squad 64 (E).z64");
	CHECK(r.name == "Bomb Squad 64 (E)" && r.load_state == -1);
	CHECK(g_settings["emulation.overclock"] == 1);
	CHECK(g_settings["video.upscale"] == 2);
	CHECK(g_setting_reads < 40); /* read once each, not every frame */
	for (const char *shot : { "1-home", "3-rack", "5-bench", "8-shares", "10-keyboard", "11-keyboard-typed", "13-home-probes" }) {
		struct stat st;
		CHECK(stat((g_pictures + "/" + shot + ".ppm").c_str(), &st) == 0 && st.st_size > 1000);
	}
	if (failures) {
		std::fprintf(stderr, "test_menu: %d check(s) failed\n", failures);
		return 1;
	}
	std::printf("test_menu: all checks passed\n");
	return 0;
}
