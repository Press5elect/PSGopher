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
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include "expansion_pak.h"
#include "game_settings.h"
#include "image.h"
#include "library.h"
#include "probe_picture.h"
#include "web.h"
#include "menu.h"
#include "update.h"

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
	void (*before)() = nullptr; /* run when the step begins (the updater's stand-in is told what to answer) */
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
		if (g_frame_in_step == 0 && s.before)
			s.before();
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
	/* L2 and R2 are triggers: the script names them by the front end's digital bits */
	const uint32_t l2 = 1u << 28, r2 = 1u << 29;
	if (held & l2)
		pad->l2 = 1;
	if (held & r2)
		pad->r2 = 1;
	held &= ~(l2 | r2);
	pad->pressed = held & ~g_held;
	pad->held = held;
	g_held = held;
}

/* ---------------------------------------------------------------- Gopher64's settings, stood in for */

static std::map<std::string, int64_t> g_settings = {
	{ "video.upscale", 1 }, { "video.ssaa", 0 }, { "video.integer_scaling", 0 }, { "video.widescreen", 0 },
	{ "video.crt", 0 }, { "video.vsync", 1 }, { "emulation.overclock", 0 }, { "emulation.disable_expansion_pak", 0 },
	{ "emulation.rewind", 0 }, { "emulation.usb", 0 }, { "input.emulate_vru", 0 },
	{ "input.controller_enabled.0", 1 }, { "input.controller_enabled.1", 0 },
	{ "input.controller_enabled.2", 0 }, { "input.controller_enabled.3", 0 },
	{ "input.transfer_pak.0", 0 }, { "input.transfer_pak.1", 0 }, { "input.transfer_pak.2", 0 }, { "input.transfer_pak.3", 0 },
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

/* Gopher64's text settings and input profiles, stood in for */
static std::map<std::string, std::string> g_texts = {
	{ "input.input_profile_binding.0", "default" }, { "input.input_profile_binding.1", "default" },
	{ "input.input_profile_binding.2", "default" }, { "input.input_profile_binding.3", "default" },
	{ "input.controller_assignment.0", "ps5:pad1" }, { "input.controller_assignment.1", "" },
	{ "input.controller_assignment.2", "" },         { "input.controller_assignment.3", "" },
	{ "input.gb_rom_path.0", "" }, { "input.gb_rom_path.1", "" }, { "input.gb_rom_path.2", "" }, { "input.gb_rom_path.3", "" },
	{ "input.gb_ram_path.0", "" }, { "input.gb_ram_path.1", "" }, { "input.gb_ram_path.2", "" }, { "input.gb_ram_path.3", "" },
};
struct StubProfile {
	int kind[19] = {}, id[19] = {}, axis[19] = {};
	int64_t deadzone = 5, dinput = 0;
};
static std::map<std::string, StubProfile> g_profiles = { { "default", {} } };
extern "C" int psgopher_setting_text(const char *name, char *buffer, size_t size)
{
	const auto it = g_texts.find(name);
	if (it == g_texts.end())
		return -1;
	snprintf(buffer, size, "%s", it->second.c_str());
	return (int)it->second.size();
}
extern "C" bool psgopher_setting_text_set(const char *name, const char *value)
{
	const auto it = g_texts.find(name);
	if (it == g_texts.end())
		return false;
	it->second = value;
	return true;
}
extern "C" int psgopher_profiles(char *buffer, size_t size)
{
	std::string all;
	for (const auto &p : g_profiles)
		all += (all.empty() ? "" : "\n") + p.first;
	snprintf(buffer, size, "%s", all.c_str());
	return (int)all.size();
}
extern "C" bool psgopher_profile_new(const char *name)
{
	return g_profiles.emplace(name, StubProfile{}).second;
}
extern "C" bool psgopher_profile_delete(const char *name)
{
	return std::string(name) != "default" && g_profiles.erase(name) == 1;
}
extern "C" bool psgopher_profile_binding(const char *name, int index, int *kind, int *id, int *axis)
{
	const auto it = g_profiles.find(name);
	if (it == g_profiles.end() || index < 0 || index >= 19)
		return false;
	*kind = it->second.kind[index], *id = it->second.id[index], *axis = it->second.axis[index];
	return true;
}
extern "C" bool psgopher_profile_bind(const char *name, int index, int kind, int id, int axis)
{
	const auto it = g_profiles.find(name);
	if (it == g_profiles.end() || index < 0 || index >= 19)
		return false;
	it->second.kind[index] = kind, it->second.id[index] = id, it->second.axis[index] = axis;
	return true;
}
extern "C" int64_t psgopher_profile_number(const char *name, int what, int64_t value, bool set)
{
	const auto it = g_profiles.find(name);
	if (it == g_profiles.end())
		return -1;
	int64_t &v = what == 0 ? it->second.deadzone : it->second.dinput;
	if (set)
		v = value;
	return v;
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

/* ---------------------------------------------------------------- the updater (update.cpp), stood in for */

namespace updater_stub {
using psgopher::update::State;
static psgopher::update::Status g_status;       /* what GetStatus() says */
static psgopher::update::Status g_on_check;     /* what a Check() finds */
static int g_checks = 0, g_downloads = 0, g_cancels = 0, g_skips = 0, g_installs = 0, g_rollbacks = 0;
static void releaseOffered()
{
	psgopher::update::Status s;
	s.state = State::Available, s.version = "1.2.0", s.tag = "v1.2.0", s.name = "Sharper Pictures";
	psgopher::update::ReleaseNotes a, b;
	a.version = "1.2.0", a.tag = "v1.2.0", a.name = "Sharper Pictures", a.date = "2026-12-01";
	a.text = "Added: the variable refresh option, for a TV that has it.\nAdded: probes keep a picture of the moment they were taken.\n"
	         "Fixed: the quick menu moved two places for one press.\nFixed: a share's ROM is copied again when its size changes.";
	b.version = "1.1.0", b.tag = "v1.1.0", b.name = "Rack Filters", b.date = "2026-11-28";
	b.text = "Added: the rack's filters (internal, USB, network).\nChanged: the letters are shown large for a moment after a jump.";
	s.newer = { a, b };
	s.notes = a.text + "\n\n" + b.text;
	s.previous = "1.0.1", s.previous_label = "v1.0.1";
	g_on_check = s;
}
static void downloading()
{
	g_status.state = State::Downloading, g_status.done = 18u * 1024 * 1024, g_status.total = 48u * 1024 * 1024, g_status.speed = 3.2 * 1024 * 1024;
}
static void failed()
{
	g_status.state = State::Failed;
	g_status.error = "The download did not match its SHA-256 checksum; nothing in the title's folder was changed.";
}
static void ready()
{
	g_status.state = State::Ready;
}
static void upToDateWithPrevious()
{
	psgopher::update::Status s;
	s.state = State::UpToDate, s.version = "1.2.0", s.tag = "v1.2.0", s.previous = "1.1.0", s.previous_label = "v1.1.0";
	g_status = s;
}
static void gone()
{
	g_status.state = State::Installed, g_status.rollback = true;
}
} // namespace updater_stub

namespace psgopher::update {
Status GetStatus() { return updater_stub::g_status; }
void Check()
{
	updater_stub::g_checks++;
	updater_stub::g_status = updater_stub::g_on_check;
}
void Download()
{
	updater_stub::g_downloads++;
	updater_stub::downloading();
}
void Cancel() { updater_stub::g_cancels++; }
void Skip()
{
	updater_stub::g_skips++;
	updater_stub::g_status.skipped = true;
}
bool Install()
{
	updater_stub::g_installs++;
	updater_stub::g_status.state = State::Installed;
	return true;
}
void RollBack()
{
	updater_stub::g_rollbacks++;
	updater_stub::g_status.state = State::Verifying, updater_stub::g_status.rollback = true;
}
} // namespace psgopher::update

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

namespace psgopher { extern FILE *g_settings_words_dump; } /* (frontend.cpp) */

int main(int argc, char **argv)
{
	const std::string work = argc > 1 ? argv[1] : "/tmp/psgopher-menu-test";
	g_pictures = argc > 2 ? argv[2] : work;
	if (std::system(("rm -rf '" + work + "' && mkdir -p '" + work + "/data/psgopher/roms' '" + work +
			"/data/psgopher/cache' '" + g_pictures + "'").c_str()) != 0)
		return 2;
	/* every BENCH row's words, dumped once for ps5/tools/settings-words.py */
	psgopher::g_settings_words_dump = fopen((work + "/settings-words.jsonl").c_str(), "w");
	/* a list of the kind a console has: ROM files named as dumps are, with headers */
	static const char *const names[] = { "Aero Racer (U)", "Bomb Squad 64 (E)", "Cave Diver (J)", "Dino Park (U) [!]",
		"Echo Base (U)", "Fjord Fishing (E)", "Gem Keeper (U)", "Hover Cars (J)", "Ice Climb 64 (U)", "Jungle Run (E)",
		"Kart Champs (U)", "Lunar Lander 64 (U)", "Moto Trial (E)", "Night Ops (U)", "Orbit (J)",
		"007 Agent (U)", "1080 Racer (U)" };
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
	if (std::system(("mkdir -p '" + work + "/data/psgopher/data/states' '" + work + "/gamedata'").c_str()) != 0)
		return 2;
	write(work + "/data/psgopher/cache/played.tsv", "36000\t" + std::to_string((long long)time(nullptr) - 86400) + "\tKart Champs (U)\n");
	for (int slot : { 1, 3 }) {
		std::string state(8192, '\0');
		for (size_t i = 0; i < state.size(); i++)
			state[i] = (char)((i * 2654435761u >> (slot * 3)) & 0xff);
		const std::string state_file = work + "/data/psgopher/data/states/BOMB SQUAD 64-0123.state" + std::to_string(slot);
		write(state_file, state);
		/* probe 1 has a picture (a 640x480 VI frame, not square pixels), probe 3 none (an older probe) */
		if (slot == 1) {
			std::vector<uint8_t> rgb(640 * 480 * 3);
			for (int y = 0; y < 480; y++)
				for (int x = 0; x < 640; x++) {
					const bool bar = (x / 80) % 2 == (y / 80) % 2;
					uint8_t *px = &rgb[((size_t)y * 640 + x) * 3];
					px[0] = (uint8_t)(bar ? 40 + x * 200 / 640 : 220 - y * 160 / 480), px[1] = (uint8_t)(y * 255 / 480), px[2] = bar ? 200 : 60;
				}
			CHECK(psgopher::writePng(psgopher::probePicturePath(state_file), 640, 480, rgb.data()));
		}
	}
	/* a picture claiming more than 2048 pixels a side is not decoded (FE-030), one of 640x480 is */
	{
		std::vector<uint8_t> wide(2049 * 3, 90);
		CHECK(psgopher::writePng(work + "/too-wide.png", 2049, 1, wide.data()));
		CHECK(psgopher::decodeImage(work + "/too-wide.png").rgba.empty());
		CHECK(!psgopher::decodeImage(psgopher::probePicturePath(work + "/data/psgopher/data/states/BOMB SQUAD 64-0123.state1")).rgba.empty());
	}
	psgopher::webStart(work + "/data/psgopher/cache/covers");
	psgopher::libraryUseRoot(work + "/data");
	psgopher::library().load();
	psgopher::library().rescan();
	for (int i = 0; i < 300 && psgopher::library().scanning(); i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

	/* the table of games that use the Expansion Pak: the three of the list that are in it (the shipped table is checked below) */
	write(work + "/gamedata/expansion-pak.tsv", "NKAE\tenhanced\twikipedia\tcited\nNBOP\tincompatible\tpj64\tunverified\nNAEE\trequired\tnintendo,wikipedia\tverified\n");
	psgopher::pakUseFile(work + "/gamedata/expansion-pak.tsv");
	{
		psgopher::PakTable shipped;
		CHECK(shipped.loadFile(std::string(PSGOPHER_SOURCE) + "/ps5/gamedata/expansion-pak.tsv") && shipped.size() > 100);
		CHECK(shipped.find("NDOE-00", "").status == psgopher::PakStatus::Required && shipped.find("NZSJ-01", "").status == psgopher::PakStatus::Required);
		CHECK(shipped.find("NSVE-00", "BFE23884-EF48EAAF-C:45").status == psgopher::PakStatus::Incompatible && shipped.find("NSVE-00", "").status == psgopher::PakStatus::NotListed);
		CHECK(shipped.find("NAEE-00", "").status == psgopher::PakStatus::NotListed);
	}
	{
		psgopher::update::Status up;
		up.state = psgopher::update::State::UpToDate, up.version = "1.0.0", up.tag = "v1.0.0";
		updater_stub::g_on_check = up;
	}
	const uint32_t none = 0;
	g_script = {
		{ none, nullptr, 6 },
		{ none, "1-home", 3 },
		{ PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, /* (past the two digit titles) */
		{ none, nullptr, 6 }, { none, "2-home-moved", 3 },
		{ PAD_R1, nullptr, 2 }, { none, nullptr, 6 }, { none, "3-rack", 3 },
		{ PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 },
		{ PAD_SQUARE, nullptr, 2 }, { PAD_OPTIONS, nullptr, 2 }, { none, "4-rack-sorted-filtered", 3 },
	};
	for (int i = 0; i < 6; i++)
		g_script.push_back({ PAD_OPTIONS, nullptr, 2 }); /* the filter back to All */
	for (int i = 0; i < 3; i++)
		g_script.push_back({ PAD_SQUARE, nullptr, 2 }); /* the sort back to A-Z */
	const uint32_t L2 = 1u << 28, R2 = 1u << 29;
	for (const Step &st : std::vector<Step>{
			{ PAD_R1, nullptr, 2 }, { none, nullptr, 6 }, { none, "5-bench", 3 }, /* Video: Resolution */
			{ PAD_CROSS, nullptr, 2 },                     /* 2x */
			{ none, "6-bench-changed", 3 },
			{ R2, "6a-bench-screen", 3 }, { R2, "6b-bench-tv", 3 }, { R2, nullptr, 2 },
			{ PAD_CROSS, "6c-bench-emulation", 3 },       /* the CPU clock: 125 MHz */
			{ R2, "6d-bench-controllers", 3 },
			{ R2, "6e-bench-profiles", 3 } })
		g_script.push_back(st);
	for (int i = 0; i < 10; i++) /* down to the N64's A (Profile, New, Delete, D Right .. A) */
		g_script.push_back({ PAD_DOWN, nullptr, 2 });
	for (const Step &st : std::vector<Step>{
			{ PAD_CROSS, "6f-bench-binding", 3 },          /* bind it: */
			{ PAD_TRIANGLE, "6g-bench-bound", 3 },         /* to Triangle */
			{ R2, "6h-bench-hotkeys", 3 } })
		g_script.push_back(st);
	for (int i = 0; i < 5; i++) /* down to Leave Game (Hotkeys, Quick menu tap, Activator, Save, Load, Leave) */
		g_script.push_back({ PAD_DOWN, nullptr, 2 });
	for (const Step &st : std::vector<Step>{
			{ PAD_LEFT, nullptr, 2 },                      /* Leave Game: off */
			{ none, "6i-bench-hotkey-changed", 3 },
			{ R2, nullptr, 2 },                            /* Console (after Hotkeys, the last section) */
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
	/* The one-time early-build warning: a fresh config shows it before HOME is usable (Right and Circle do nothing, Cross
	 * dismisses it and starts nothing), the flag is saved, and a second run does not show it (Cross starts the game) */
	{
		const std::vector<Step> main_script = g_script;
		const std::string ini = work + "/data/psgopher/config/psgopher.ini";
		CHECK(std::system(("mkdir -p '" + work + "/data/psgopher/config'").c_str()) == 0);
		setenv("PSGOPHER_TITLE_INI", ini.c_str(), 1);
		CHECK(access(ini.c_str(), F_OK) != 0);
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = { { none, nullptr, 6 }, { PAD_RIGHT, nullptr, 2 }, { PAD_CIRCLE, nullptr, 2 }, { none, nullptr, 3 }, { none, "36-early-warning", 3 },
			{ PAD_CROSS, nullptr, 2 }, { none, nullptr, 6 }, { PAD_CROSS, nullptr, 2 } };
		const psgopher::MenuResult rw = psgopher::runMenu("");
		CHECK(rw.play && rw.rom == work + "/data/psgopher/roms/Kart Champs (U).z64"); /* (HOME's first game: the Right was not taken) */
		psgopher::TitleSettings saved;
		saved.file = ini;
		saved.load();
		CHECK(saved.earlyWarningSeen);
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = { { none, nullptr, 6 }, { PAD_CROSS, nullptr, 2 } };
		const psgopher::MenuResult rw2 = psgopher::runMenu("");
		CHECK(rw2.play && rw2.rom == rw.rom);
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = main_script;
	}
	const psgopher::MenuResult r = psgopher::runMenu("A notice from the last game");
	std::printf("menu returned: play %d, %s (probe %d); Gopher64's settings read %d times\n", r.play, r.rom.c_str(), r.load_state,
	            g_setting_reads);
	CHECK(r.play);
	CHECK(r.rom == work + "/data/psgopher/roms/Bomb Squad 64 (E).z64");
	CHECK(r.name == "Bomb Squad 64 (E)" && r.load_state == -1);
	CHECK(g_settings["emulation.overclock"] == 1);
	CHECK(g_settings["video.upscale"] == 2);
	CHECK(g_setting_reads < 60); /* read once each, not every frame */
	CHECK(g_profiles["default"].kind[7] == 1 && g_profiles["default"].id[7] == 3); /* A bound to Triangle */
	CHECK(updater_stub::g_checks == 1); /* the check at the title's start (Check for Updates at Start is on), once */
	/* BENCH, Console, the updater: Check for Updates, the offer with its notes, the download, the install
	 * asked first and closing the title; then Roll Back */
	{
		using namespace updater_stub;
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_status = {};
		g_checks = 0;
		g_script = {
			{ none, nullptr, 6 },
			{ PAD_R1, nullptr, 2 }, { PAD_R1, nullptr, 2 }, { L2, nullptr, 2 }, { none, nullptr, 6 }, /* BENCH, Console */
			{ PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 }, /* Check for Updates (after Game Folders) */
			{ none, "17-update-idle", 3 },
			{ none, nullptr, 1, releaseOffered },
			{ PAD_CROSS, "18-update-available", 3 },
			{ PAD_DOWN, nullptr, 2 },                                  /* Download Update */
			{ PAD_CROSS, nullptr, 2 }, { none, "19-update-downloading", 3 },
			{ none, nullptr, 1, failed }, { none, "20-update-failed", 3 },
			{ none, nullptr, 1, ready }, { none, "21-update-ready", 3 }, /* Install Update, then Roll Back */
			{ PAD_CROSS, "22-update-install-ask", 3 },
			{ PAD_CIRCLE, nullptr, 2 }, { none, nullptr, 6 },          /* cancelled: nothing installed */
			{ PAD_CROSS, nullptr, 2 }, { none, nullptr, 3 },
			{ PAD_CROSS, nullptr, 2 },                                 /* installed: the title closes */
		};
		const psgopher::MenuResult r4 = psgopher::runMenu("");
		CHECK(!r4.play && r4.rom.empty());
		CHECK(g_checks == 1 && g_downloads == 1 && g_installs == 1 && g_rollbacks == 0 && g_skips == 0);
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = {
			{ none, nullptr, 6 },
			{ PAD_R1, nullptr, 2 }, { PAD_R1, nullptr, 2 }, { L2, nullptr, 2 }, { none, nullptr, 6, upToDateWithPrevious }, /* BENCH, Console */
			{ PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 },
			{ PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 },        /* Roll Back */
			{ none, "23-update-rollback", 3 },
			{ PAD_CROSS, "24-update-rollback-ask", 3 },
			{ PAD_CROSS, nullptr, 2 }, { none, nullptr, 6 },           /* it is checked (the updater's thread) */
			{ none, nullptr, 20, gone }, { none, nullptr, 6 },         /* and put back: the title closes */
		};
		const psgopher::MenuResult r5 = psgopher::runMenu("");
		CHECK(!r5.play && g_rollbacks == 1 && g_installs == 1 && g_checks == 1);
	}
	/* BENCH, Console, Game Folders: the list (none), the chooser at the top, a folder used, the list
	 * with it; Circle closes it and searches again. Then the folder is removed (Cross on it). */
	{
		using namespace updater_stub;
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = {
			{ none, nullptr, 6 },
			{ PAD_R1, nullptr, 2 }, { PAD_R1, nullptr, 2 }, { L2, nullptr, 2 }, { none, nullptr, 6 }, /* BENCH, Console */
			{ PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 }, { none, "40-bench-game-folders", 3 },
			{ PAD_CROSS, nullptr, 2 }, { none, "41-game-folders-none", 3 },
			{ PAD_CROSS, nullptr, 2 }, { none, "42-game-folders-choose", 3 },                   /* Add a folder */
			{ PAD_CROSS, nullptr, 2 }, { none, nullptr, 3 },                                    /* /data */
			{ PAD_DOWN, nullptr, 2 }, { PAD_CROSS, nullptr, 2 }, { none, "43-game-folders-one", 3 }, /* Use this folder */
			{ PAD_CIRCLE, nullptr, 2 }, { none, nullptr, 30 },                                  /* closed: searched again */
			{ PAD_CROSS, nullptr, 2 }, { PAD_DOWN, nullptr, 2 }, { PAD_CROSS, nullptr, 2 },      /* removed */
			{ PAD_CIRCLE, nullptr, 2 }, { none, nullptr, 30 },
			{ PAD_R1, nullptr, 2 }, { none, nullptr, 8 },
			{ PAD_CROSS, nullptr, 2 } };                                                         /* power on */
		const psgopher::MenuResult rf = psgopher::runMenu("");
		CHECK(rf.play);
		CHECK(psgopher::gameFolders().empty());
	}
	/* A game's page (Triangle on HOME): its rows start at Use Global Setting; Resolution and Final Filter set for
	 * the game go to its file by its key; BENCH marks them; the game starts with them over the global settings and
	 * the next game, which has none, with the global ones */
	{
		const int64_t global_upscale = g_settings["video.upscale"];
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = {
			{ none, nullptr, 6 },
			{ PAD_TRIANGLE, nullptr, 2 }, { none, nullptr, 6 }, { none, "25-game-page", 3 },      /* Cheats first */
			{ R2, nullptr, 2 }, { none, "26-game-page-video", 3 },                              /* Video: Resolution */
			{ PAD_CROSS, nullptr, 2 }, { PAD_CROSS, nullptr, 2 }, { PAD_CROSS, nullptr, 2 },      /* 1x, 2x, 4x */
			{ PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 }, { PAD_DOWN, nullptr, 2 },         /* SSAA, Downsampling, Final Filter */
			{ PAD_CROSS, nullptr, 2 }, { PAD_CROSS, nullptr, 2 },                                  /* Sharp Bilinear, Bilinear */
			{ none, "27-game-page-set", 3 },
			{ R2, nullptr, 2 }, { R2, nullptr, 2 }, { R2, "28-game-page-controllers", 3 },        /* Screen & Sound, Emulation, Controllers */
			{ PAD_CIRCLE, nullptr, 2 }, { none, nullptr, 6 },                                    /* back to HOME */
			{ PAD_L1, nullptr, 2 }, { none, nullptr, 8 }, { none, "29-bench-marker", 3 },        /* BENCH marks them */
			{ PAD_R1, nullptr, 2 }, { none, nullptr, 8 },
			{ PAD_CROSS, nullptr, 2 } };                                                         /* power on */
		const psgopher::MenuResult rg = psgopher::runMenu("");
		CHECK(rg.play && rg.name == "Kart Champs (U)" && rg.key == "NKAE-00" && rg.keyFromHeader);
		CHECK(g_settings["video.upscale"] == global_upscale); /* (the page changed nothing global) */
		const std::string file = work + "/data/psgopher/config/games/NKAE-00.ini";
		const psgopher::GameSettings own = psgopher::loadGameSettings("NKAE-00");
		CHECK(access(file.c_str(), F_OK) == 0 && own.values.size() == 2 && own.find("video.upscale") && *own.find("video.upscale") == 4 &&
		      own.find("final_filter") && *own.find("final_filter") == 1);
		psgopher::TitleSettings title;
		{
			psgopher::GameScope scope(rg.key, rg.keyFromHeader, title);
			CHECK(g_settings["video.upscale"] == 4 && scope.settings().finalFilter == 1 && title.finalFilter == 0);
			scope.settings().applyHdr();
			CHECK(!strcmp(getenv("PSGOPHER_FINAL_FILTER"), "bilinear"));
		}
		CHECK(g_settings["video.upscale"] == global_upscale);
		title.applyHdr();
		CHECK(!strcmp(getenv("PSGOPHER_FINAL_FILTER"), "sharp"));
		/* the next game has none */
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = { { none, nullptr, 6 }, { PAD_RIGHT, nullptr, 2 }, { PAD_CROSS, nullptr, 2 } };
		const psgopher::MenuResult rn = psgopher::runMenu("");
		CHECK(rn.play && rn.key != rg.key && !rn.key.empty());
		{
			psgopher::GameScope scope(rn.key, rn.keyFromHeader, title);
			CHECK(g_settings["video.upscale"] == global_upscale && scope.settings().finalFilter == 0);
		}
	}
	/* The Expansion Pak on HOME's panel and its notice: a game's panel says what the table says; a game that needs the Expansion Pak,
	 * set to Off on its page, asks before it starts (Circle goes back, Cross starts it anyway); a game set to On, or Auto, does not ask */
	{
		psgopher::GameSettings off;
		off.set("expansion_pak", psgopher::PakOff);
		CHECK(psgopher::saveGameSettings("NAEE-00", off));
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = {
			{ none, nullptr, 6 }, { none, "30-pak-fact", 3 },                                      /* Kart Champs: Enhanced, 8 MB */
			{ PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { none, nullptr, 6 }, { none, "31-pak-fact-required", 3 }, /* Aero Racer: Required, 4 MB (Off) */
			{ PAD_CROSS, nullptr, 2 }, { none, nullptr, 4 }, { none, "32-pak-notice", 3 },        /* the notice */
			{ PAD_CIRCLE, nullptr, 2 }, { none, nullptr, 6 },                                      /* back: nothing starts */
			{ PAD_CROSS, nullptr, 2 }, { none, nullptr, 4 },                                       /* the notice again */
			{ PAD_CROSS, nullptr, 2 } };                                                           /* start anyway */
		const psgopher::MenuResult rp = psgopher::runMenu("");
		CHECK(rp.play && rp.name == "Aero Racer (U)" && rp.key == "NAEE-00" && rp.crc.size() > 17);
		/* with it On, the game starts at once: Cross and nothing between */
		off.set("expansion_pak", psgopher::PakOn);
		CHECK(psgopher::saveGameSettings("NAEE-00", off));
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = { { none, nullptr, 6 }, { PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { none, nullptr, 6 }, { PAD_CROSS, nullptr, 2 } };
		const psgopher::MenuResult ro = psgopher::runMenu("");
		CHECK(ro.play && ro.key == "NAEE-00");
		/* a game that is incompatible: its panel says it must be off (Bomb Squad 64, the fifth cartridge of the strip) */
		CHECK(psgopher::saveGameSettings("NAEE-00", psgopher::GameSettings{}));
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = { { none, nullptr, 6 }, { PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { PAD_RIGHT, nullptr, 2 }, { none, nullptr, 6 }, { none, "33-pak-fact-must-be-off", 3 },
		             { PAD_CROSS, nullptr, 2 } };
		const psgopher::MenuResult rb = psgopher::runMenu("");
		CHECK(rb.play && rb.key == "NBOP-00");
		/* the game's page: the row, Auto first and marked */
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = { { none, nullptr, 6 }, { PAD_TRIANGLE, nullptr, 2 }, { none, nullptr, 6 }, { R2, nullptr, 2 }, { R2, nullptr, 2 }, { R2, nullptr, 2 },
		             { none, "34-game-page-emulation", 3 }, { PAD_DOWN, nullptr, 2 }, { PAD_CROSS, nullptr, 2 }, { none, "35-game-page-pak-set", 3 },
		             { PAD_CIRCLE, nullptr, 2 }, { none, nullptr, 6 }, { PAD_CROSS, nullptr, 2 } };
		const psgopher::MenuResult rq = psgopher::runMenu("");
		CHECK(rq.play && rq.key == "NKAE-00");
	}
	/* Circle and the letters (a fresh menu each): the library back to HOME, HOME asking before it
	 * closes, L2 / R2 across the letters */
	{
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = {
			{ none, nullptr, 6 },
			{ PAD_R1, nullptr, 2 }, { none, nullptr, 6 },
			{ PAD_CIRCLE, nullptr, 2 }, { none, nullptr, 6 },   /* the library: back to HOME */
			{ PAD_CIRCLE, nullptr, 2 }, { none, "14-close-confirm", 3 }, /* HOME: "Close PSGopher?" */
			{ PAD_R1, nullptr, 2 }, { none, nullptr, 6 },       /* (the question has the pad: no view change) */
			{ PAD_CIRCLE, nullptr, 2 }, { none, nullptr, 6 },   /* stay */
			{ PAD_R1, nullptr, 2 }, { none, nullptr, 6 },       /* the library again, at Aero Racer */
		};
		for (int i = 0; i < 13; i++)                            /* B .. N: Orbit's letter is the 14th */
			g_script.push_back({ R2, nullptr, 2 });
		g_script.push_back({ R2, "15-rack-letter-O", 2 });
		for (const Step &st : std::vector<Step>{
				{ R2, "16-rack-letter-hash", 2 },                /* O to '#' (007 Agent) */
				{ R2, nullptr, 2 },                            /* the last letter: its last game (1080 Racer) */
				{ L2, nullptr, 2 },                            /* its first (007 Agent) */
				{ L2, nullptr, 2 },                            /* the letter before: Orbit */
				{ PAD_CROSS, nullptr, 2 } })
			g_script.push_back(st);
		const psgopher::MenuResult r2 = psgopher::runMenu("");
		CHECK(r2.play && r2.rom == work + "/data/psgopher/roms/Orbit (J).z64");
		g_step = 0, g_frame_in_step = 0, g_held = 0, g_script_done = false;
		g_script = { { none, nullptr, 6 }, { PAD_CIRCLE, nullptr, 2 }, { none, nullptr, 6 }, { PAD_CROSS, nullptr, 2 } };
		const psgopher::MenuResult r3 = psgopher::runMenu(""); /* Cross on the question: the menu ends, no game */
		CHECK(!r3.play && r3.rom.empty());
	}
	for (const char *shot : { "14-close-confirm", "15-rack-letter-O", "16-rack-letter-hash", "1-home", "3-rack", "5-bench", "8-shares", "10-keyboard", "11-keyboard-typed", "13-home-probes", "17-update-idle", "18-update-available", "19-update-downloading", "20-update-failed", "21-update-ready", "22-update-install-ask", "23-update-rollback", "24-update-rollback-ask", "25-game-page", "26-game-page-video", "27-game-page-set", "28-game-page-controllers", "29-bench-marker", "30-pak-fact", "31-pak-fact-required", "32-pak-notice", "33-pak-fact-must-be-off", "34-game-page-emulation", "35-game-page-pak-set", "36-early-warning" }) {
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
