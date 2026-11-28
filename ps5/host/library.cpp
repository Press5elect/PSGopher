/*
 * PSGopher - the game list (library.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include "library.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5_vfs.h"
#include "romhack.h"
#include "write_broker.h"

extern "C" void say(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* Gopher64's facts of a cartridge (vendor/gopher64/src/psgopher.rs); weak, so the host tests may
 * give their own or none */
struct PsgopherRomFacts {
	uint32_t cic, save, rumble;
};
extern "C" bool psgopher_rom_facts(const uint8_t *rom, size_t len, PsgopherRomFacts *out) __attribute__((weak));
/* a 64DD disk's ID and the name Gopher64 gives its saves and states ("64DD <ID>"), read from the
 * image's system blocks (psgopher.rs) */
extern "C" bool psgopher_disk_facts(const char *path, char *name, size_t name_size, char *id, int *region) __attribute__((weak));

namespace psgopher {

namespace {

std::string g_root = "/data"; // the console's /data (the host tests' folder)
std::string listFile() { return g_root + "/psgopher/cache/library.tsv"; }
/* the romhack patches' bases (patch path TAB base path): beside the list, whose format stays one the title
 * before them reads (version 4) */
std::string basesFile() { return g_root + "/psgopher/cache/library-bases.tsv"; }
std::string foldersFile() { return g_root + "/psgopher/config/folders.cfg"; }
std::string playedFile() { return g_root + "/psgopher/cache/played.tsv"; }
std::string statesFolder() { return g_root + "/psgopher/data/states"; }
std::string playFolder() { return g_root + "/psgopher/cache/play"; }
/* a romhack's patch file: a large hack's patch carries most of its ROM (some pass 100 MB) */
constexpr size_t kMaxPatch = 256u << 20;
constexpr int kDepth = 5;               // folders below a place
constexpr unsigned kLocalBudget = 60000; // entries a local place may cost
constexpr unsigned kShareBudget = 8000;  // ... a share (each folder is a round trip)

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

bool endsWith(const std::string &s, const char *tail)
{
	const size_t n = strlen(tail);
	return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

bool rawRom(const std::string &lowered)
{
	return endsWith(lowered, ".z64") || endsWith(lowered, ".n64") || endsWith(lowered, ".v64");
}

/* One folder's entries, through ps5vfs for a share and libc otherwise. */
struct Entry {
	std::string name;
	bool folder;
	uint64_t size;
};
bool listFolder(const std::string &path, std::vector<Entry> &out)
{
	out.clear();
	if (ps5vfs_is_virtual(path.c_str())) {
		ps5vfs_dir *d = ps5vfs_opendir(path.c_str());
		if (!d)
			return false;
		ps5vfs_dirent e;
		while (ps5vfs_readdir(d, &e) == 1)
			out.push_back({ e.name, e.is_directory != 0, (uint64_t)std::max<int64_t>(0, e.size) });
		ps5vfs_closedir(d);
		return true;
	}
	DIR *d = opendir(path.c_str());
	if (!d)
		return false;
	while (dirent *e = readdir(d)) {
		if (e->d_name[0] == '.')
			continue;
		const std::string full = path + "/" + e->d_name;
		struct stat st;
		bool folder = e->d_type == DT_DIR;
		uint64_t size = 0;
		if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK || !folder) {
			if (stat(full.c_str(), &st) != 0)
				continue;
			folder = S_ISDIR(st.st_mode);
			size = (uint64_t)st.st_size;
		}
		out.push_back({ e->d_name, folder, size });
	}
	closedir(d);
	return true;
}

/* A .zip's first file, from its first local header (30 bytes and the name): an N64 ROM or not. */
bool zipHoldsRom(const std::string &path)
{
	unsigned char head[30 + 256];
	size_t got = 0;
	if (ps5vfs_is_virtual(path.c_str())) {
		FILE *f = ps5vfs_fopen(path.c_str(), "rb");
		if (!f)
			return false;
		got = ps5vfs_fread(head, 1, sizeof(head), f);
		ps5vfs_fclose(f);
	} else {
		FILE *f = fopen(path.c_str(), "rb");
		if (!f)
			return false;
		got = fread(head, 1, sizeof(head), f);
		fclose(f);
	}
	if (got < 30 || memcmp(head, "PK\x03\x04", 4) != 0)
		return false;
	const size_t length = head[26] | (head[27] << 8);
	if (length == 0 || 30 + length > got)
		return false;
	return rawRom(lower(std::string((const char *)head + 30, length)));
}

/* The ROM's first 4 KiB in Gopher64's byte order (.z64: big-endian; .v64 swaps each 16-bit pair,
 * .n64 each 32-bit word), from the file or the share; false when it is not an N64 ROM. */
bool readHeader(const std::string &path, uint8_t *head)
{
	size_t got = 0;
	if (ps5vfs_is_virtual(path.c_str())) {
		FILE *f = ps5vfs_fopen(path.c_str(), "rb");
		if (!f)
			return false;
		got = ps5vfs_fread(head, 1, 0x1000, f);
		ps5vfs_fclose(f);
	} else {
		FILE *f = fopen(path.c_str(), "rb");
		if (!f)
			return false;
		got = fread(head, 1, 0x1000, f);
		fclose(f);
	}
	if (got < 0x1000)
		return false;
	const uint32_t magic = (uint32_t)head[0] << 24 | head[1] << 16 | head[2] << 8 | head[3];
	if (magic == 0x37804012u) { /* .v64 */
		for (size_t i = 0; i + 1 < 0x1000; i += 2)
			std::swap(head[i], head[i + 1]);
	} else if (magic == 0x40123780u) { /* .n64 */
		for (size_t i = 0; i + 3 < 0x1000; i += 4) {
			std::swap(head[i], head[i + 3]);
			std::swap(head[i + 1], head[i + 2]);
		}
	} else if (magic != 0x80371240u)
		return false;
	return true;
}

/* The game's facts from a ROM's first 4 KiB in Gopher64's byte order (.z64) */
void factsFromHeader(Game &g, const uint8_t *head)
{
	g.header.assign((const char *)head + 0x20, 20);
	g.header = g.header.substr(0, g.header.find('\0'));
	while (!g.header.empty() && g.header.back() == ' ')
		g.header.pop_back();
	g.id.clear();
	for (int i = 0x3B; i < 0x3F; i++)
		g.id += head[i] >= 0x20 && head[i] < 0x7f ? (char)head[i] : '-';
	char crc[40];
	snprintf(crc, sizeof(crc), "%02X%02X%02X%02X-%02X%02X%02X%02X-C:%02X", head[0x10], head[0x11], head[0x12], head[0x13],
	         head[0x14], head[0x15], head[0x16], head[0x17], head[0x3E]);
	g.crc = crc;
	g.revision = head[0x3F];
	PsgopherRomFacts f = {};
	if (psgopher_rom_facts && psgopher_rom_facts(head, 0x1000, &f)) {
		g.cic = f.cic;
		g.save = f.save;
		g.rumble = f.rumble != 0;
	}
	g.facts = true;
}

/* The game's facts from its ROM's header (left as they are for a .zip or .7z, or an unreadable one) */
void readFacts(Game &g)
{
	const std::string l = lower(g.path);
	/* a disk on the console's storage or a USB drive: its probes are found by the name Gopher64
	 * gives it (a share's is not read: Gopher64's reader takes the console's files only) */
	if ((isDiskName(l) || (rawRom(l) && isDiskSize(g.size) && isDiskFile(g.path, g.size))) && psgopher_disk_facts &&
		!ps5vfs_is_virtual(g.path.c_str())) {
		char name[64] = {}, id[5] = {};
		int region = -1;
		if (psgopher_disk_facts(g.path.c_str(), name, sizeof(name), id, &region)) {
			g.header = name;
			g.facts = true;
			g.revision = 0;
		}
		return;
	}
	if (!rawRom(l))
		return;
	uint8_t head[0x1000];
	if (!readHeader(g.path, head))
		return;
	factsFromHeader(g, head);
}

/* Folders not worth a look: the title's own (but its roms), systems' and other programs' data. */
bool skipFolder(const std::string &full, const std::string &name)
{
	if (name.empty() || name[0] == '.' || name[0] == '$')
		return true;
	static const char *const names[] = { "System Volume Information", "sce_sys", "sce_module", "LOST.DIR" };
	for (const char *n : names)
		if (name == n)
			return true;
	const std::string own = g_root + "/psgopher/";
	if (full == own + "cache" || full == own + "data" || full == own + "config")
		return true;
	/* other titles' folders in /data/homebrew (their data: an Xbox emulator's patch files were taken for
	 * romhacks, boot log 2026-11-27); PSGopher's own is searched */
	const std::string homebrew = g_root + "/homebrew/";
	return full.size() > homebrew.size() && full.compare(0, homebrew.size(), homebrew) == 0 &&
		full.find('/', homebrew.size()) == std::string::npos && name != "PPSA99280";
}

std::string stem(const std::string &name)
{
	const size_t dot = name.rfind('.');
	return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

struct Place {
	std::string path;
	int tab;
	bool chosen{ false }; // a game folder the user chose: every .7z in it is listed
};

bool isFolder(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<Place> places()
{
	std::vector<Place> out;
	/* the title's own folder first: what is found twice keeps the first place's tab */
	out.push_back({ g_root + "/psgopher/roms", TabInternal });
	/* game folders chosen in BENCH: only they are searched, not the whole of /data and every drive
	 * (a reporter's USB drive gave no games; a large drive or /data is slow to search whole) */
	const std::vector<std::string> chosen = gameFolders();
	for (const std::string &folder : chosen)
		out.push_back({ folder, tabOfPath(folder), true });
	if (!chosen.empty()) {
		for (unsigned i = 0;; i++) {
			const char *mount = ps5vfs_mount_point(i);
			if (!mount)
				break;
			if (strncmp(mount, "/smb/", 5) == 0)
				out.push_back({ mount, TabNetwork });
		}
		return out;
	}
	out.push_back({ g_root, TabInternal });
	for (int i = 0; i < 8; i++) {
		const std::string usb = "/mnt/usb" + std::to_string(i);
		if (isFolder(usb))
			out.push_back({ usb, TabUsb });
	}
	for (int i = 0; i < 2; i++) {
		const std::string ext = "/mnt/ext" + std::to_string(i);
		if (isFolder(ext))
			out.push_back({ ext, TabInternal });
	}
	for (unsigned i = 0;; i++) {
		const char *mount = ps5vfs_mount_point(i);
		if (!mount)
			break;
		if (strncmp(mount, "/smb/", 5) == 0)
			out.push_back({ mount, TabNetwork });
	}
	return out;
}

struct State {
	mutable std::mutex mutex;
	std::vector<Game> games;
	unsigned version{ 1 };
	std::string status;
	std::atomic<bool> scanning{ false };
	std::atomic<bool> again{ false }; // a search asked for while one ran: it runs when that one ends
};
State &state()
{
	static State s;
	return s;
}

void sortGames(std::vector<Game> &games)
{
	std::stable_sort(games.begin(), games.end(), [](const Game &a, const Game &b) {
		const std::string x = lower(a.name), y = lower(b.name);
		return x != y ? x < y : a.path < b.path;
	});
}

void save(const std::vector<Game> &games)
{
	std::string text;
	/* v4: tab, size, facts, cic, save, rumble, id, header, crc, revision, path (the path last: it may hold
	 * anything but a newline; the header's name has no tab). v3 has no revision (-1: read when asked). A
	 * romhack patch's base is not here but in library-bases.tsv (a dev build wrote it as a 12th field,
	 * version 5, before the title's file was kept for the one before it to read) */
	text += "#psgopher-library 4\n";
	std::string bases;
	for (const Game &g : games) {
		std::string header = g.header;
		std::replace(header.begin(), header.end(), '\t', ' ');
		text += std::to_string(g.tab) + "\t" + std::to_string(g.size) + "\t" + (g.facts ? "1" : "0") + "\t" +
			std::to_string(g.cic) + "\t" + std::to_string(g.save) + "\t" + (g.rumble ? "1" : "0") + "\t" +
			(g.id.empty() ? "-" : g.id) + "\t" + header + "\t" + (g.crc.empty() ? "-" : g.crc) + "\t" + std::to_string(g.revision) + "\t" +
			g.path + "\n";
		if (!g.base.empty())
			bases += g.path + "\t" + g.base + "\n";
	}
	/* (the bases first: a list with a patch in it always has its base on file) */
	for (const auto &[file, content] : { std::make_pair(basesFile(), bases), std::make_pair(listFile(), text) }) {
		const std::string temporary = file + ".new";
		if (FILE *f = fopen(temporary.c_str(), "wb")) {
			const bool ok = fwrite(content.data(), 1, content.size(), f) == content.size(); // one write: few and small
			fclose(f);
			if (ok && rename(temporary.c_str(), file.c_str()) == 0)
				chmod(file.c_str(), 0666);
			else
				unlink(temporary.c_str());
		}
	}
}

/* false when the place itself could not be listed (its games are then the last list's) */
bool search(const Place &place, std::vector<Game> &found, std::vector<Game> &patches, std::vector<std::string> &seen)
{
	bool listed = false;
	const bool share = place.tab == TabNetwork;
	unsigned budget = share ? kShareBudget : kLocalBudget;
	struct Folder {
		std::string path;
		int depth;
	};
	/* folders named for the N64 first, then the rest by depth (shallow before deep): a large drive
	 * spends its budget where games are likely (a USB drive of 60000 entries ran out before them,
	 * boot log 2026-11-27) */
	std::deque<Folder> todo{ { place.path, 0 } };
	std::vector<Entry> entries;
	while (!todo.empty() && budget > 0) {
		const Folder folder = todo.front();
		todo.pop_front();
		if (std::find(seen.begin(), seen.end(), folder.path) != seen.end())
			continue;
		seen.push_back(folder.path);
		{
			std::lock_guard lock(state().mutex);
			state().status = folder.path;
		}
		/* a share's listing is asked twice: on the console one timed out once
		 * (SMB2_STATUS_IO_TIMEOUT, boot log 2026-11-25) and the list came back empty */
		bool ok = listFolder(folder.path, entries);
		if (!ok && share)
			ok = listFolder(folder.path, entries);
		if (!ok)
			continue;
		if (folder.path == place.path)
			listed = true;
		const bool n64Folder = lower(folder.path).find("n64") != std::string::npos ||
			folder.path.rfind(g_root + "/psgopher/roms", 0) == 0;
		for (const Entry &e : entries) {
			if (budget == 0)
				break;
			budget--;
			const std::string full = folder.path + "/" + e.name;
			if (e.folder) {
				if (folder.depth < kDepth && !skipFolder(full, e.name)) {
					const std::string n = lower(e.name);
					const bool likely = n.find("n64") != std::string::npos || n.find("nintendo 64") != std::string::npos ||
						n.find("nintendo_64") != std::string::npos || n.find("nintendo64") != std::string::npos ||
						n.find("64dd") != std::string::npos;
					if (likely)
						todo.push_front({ full, folder.depth + 1 });
					else
						todo.push_back({ full, folder.depth + 1 });
				}
				continue;
			}
			const std::string l = lower(e.name);
			if (romhack::formatOfName(e.name) != romhack::Format::None) {
				/* a romhack patch is a cartridge of its own once a base ROM is found for it (resolvePatches) */
				Game g;
				g.name = stem(e.name);
				g.path = full;
				g.size = e.size;
				g.tab = place.tab;
				patches.push_back(g);
				continue;
			}
			/* a BIOS (No-Intro's "[BIOS] ..." names: the 64DD's IPL, the PIF) is no game: the 64DD IPL is
			 * chosen in BENCH > Emulation instead */
			if (l.rfind("[bios]", 0) == 0)
				continue;
			bool rom = rawRom(l);
			/* a raw file too small for a ROM's header and boot code is no game: the console's
			 * PIF (BIOS) image, 2 KiB as a .v64, was listed and failed to start (boot log
			 * 2026-11-25); a size of 0 is one the listing did not give */
			if (rom && e.size > 0 && e.size < 0x1000)
				rom = false;
			if (!rom && endsWith(l, ".zip"))
				rom = zipHoldsRom(full);
			if (!rom && endsWith(l, ".7z"))
				rom = n64Folder || place.chosen;
			/* a 64DD disk image: Gopher64 starts it with the 64DD's IPL (--disk) */
			if (!rom && isDiskName(l))
				rom = e.size == 0 || e.size >= 0x100000;
			if (rom) {
				Game g;
				g.name = stem(e.name);
				g.path = full;
				g.size = e.size;
				g.tab = place.tab;
				found.push_back(g);
			}
		}
	}
	if (budget == 0)
		say("library: %s: searched as far as its budget goes (%u entries; %zu folder(s) left unsearched: BENCH > Console > "
			"Game Folders searches only the folders chosen)",
			place.path.c_str(), share ? kShareBudget : kLocalBudget, todo.size());
	return listed;
}

/* A file read whole, through ps5vfs for a share (a patch or a ROM is at most 256 MiB: romhacks pass 64 MB, up to the cartridge space's 252 MB). `hint` is the
 * size the listing gave (0 unknown): the buffer is made that size at once and read into, so the file is
 * in memory once; with no hint it grows as it is read. */
bool readWhole(const std::string &path, size_t cap, std::vector<uint8_t> &data, std::string *error, uint64_t hint = 0)
{
	data.clear();
	const bool share = ps5vfs_is_virtual(path.c_str());
	FILE *f = share ? ps5vfs_fopen(path.c_str(), "rb") : fopen(path.c_str(), "rb");
	if (!f) {
		if (error)
			*error = std::string(share ? "the share did not open it: " : "it did not open: ") + strerror(errno);
		return false;
	}
	uint8_t block[1 << 16];
	bool too_large = false;
	size_t have = 0;
	if (hint > 0 && hint <= cap) {
		data.resize((size_t)hint);
		while (have < data.size()) {
			const size_t want = data.size() - have;
			const size_t n = share ? ps5vfs_fread(data.data() + have, 1, want, f) : fread(data.data() + have, 1, want, f);
			if (n == 0)
				break;
			have += n;
		}
		data.resize(have);
	}
	for (;;) {
		const size_t n = share ? ps5vfs_fread(block, 1, sizeof(block), f) : fread(block, 1, sizeof(block), f);
		if (n == 0)
			break;
		if (data.size() + n > cap) {
			too_large = true;
			break;
		}
		data.insert(data.end(), block, block + n);
	}
	const bool failed = (share ? ps5vfs_ferror(f) : ferror(f)) != 0;
	if (share)
		ps5vfs_fclose(f);
	else
		fclose(f);
	if (failed || too_large || data.empty()) {
		data.clear();
		if (error)
			*error = failed ? "it could not be read to its end" : too_large ? "the file is too large" : "the file is empty";
		return false;
	}
	return true;
}

/* A ROM file's CRC32 as stored and in .z64 order, read in blocks (no more than one block in memory) */
bool crcsOfRom(const std::string &path, uint32_t *raw, uint32_t *z64)
{
	const bool share = ps5vfs_is_virtual(path.c_str());
	FILE *f = share ? ps5vfs_fopen(path.c_str(), "rb") : fopen(path.c_str(), "rb");
	if (!f)
		return false;
	uint8_t block[1 << 16]; /* (a multiple of 4: a word is never split between blocks) */
	uint32_t a = 0, b = 0;
	int order = -2;
	for (;;) {
		const size_t n = share ? ps5vfs_fread(block, 1, sizeof(block), f) : fread(block, 1, sizeof(block), f);
		if (n == 0)
			break;
		if (order == -2)
			order = n >= 4 ? romhack::byteOrder(block) : -1;
		a = romhack::crc32(block, n, a);
		romhack::toZ64(block, n, order);
		b = romhack::crc32(block, n, b);
	}
	const bool failed = (share ? ps5vfs_ferror(f) : ferror(f)) != 0;
	if (share)
		ps5vfs_fclose(f);
	else
		fclose(f);
	*raw = a, *z64 = b;
	return !failed;
}

std::string folderOf(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

/* Lists each romhack patch found as a cartridge of its own, with its base ROM and the facts of the
 * patched header, and appends it to `found`. The base is (a) a raw ROM in the folder with the patch's
 * stem, else (b) for a BPS or UPS a raw ROM of the declared source size whose CRC32 (as stored or in
 * .z64 order) is the declared one (only size-matching ROMs are read), else (c) for an IPS the folder's
 * only ROM. A patch with no base is left out, with a line saying why. What the last list (`known`,
 * sorted by path) knew for the same patch (path and size) is kept, not read again. */
/* A patch beside its base with the same name (game.z64 + game.bps) would show as a second "game", and
 * share its play time (kept by name): it is "game (patched)" */
void namePatched(Game &g)
{
	if (g.base.empty())
		return;
	const size_t slash = g.base.rfind('/');
	if (lower(stem(slash == std::string::npos ? g.base : g.base.substr(slash + 1))) == lower(g.name))
		g.name += " (patched)";
}

/* patches that failed in this run (kept between searches) */
struct Failed {
	uint64_t size;      // the patch file's
	std::string why;
	std::string base;   // the ROM chosen for it ("" none found)
	uint64_t base_size; // its size
	uint64_t signature; // the ROMs there were (path and size), for a patch with no base found
};
std::map<std::string, Failed> g_failed;

void resolvePatches(std::vector<Game> &patches, std::vector<Game> &found, const std::vector<Game> &known)
{
	if (patches.empty())
		return;
	std::vector<Game> roms; /* the raw ROMs the bases are among, by path */
	for (const Game &g : found)
		if (g.base.empty() && rawRom(lower(g.path)))
			roms.push_back(g);
	std::sort(roms.begin(), roms.end(), [](const Game &a, const Game &b) { return a.path < b.path; });
	/* why a patch is not listed: the first 20 a search, then only the count (a folder of another
	 * system's patches filled the log) */
	unsigned told = 0;
	auto unlisted = [&told](const std::string &path, const std::string &why) {
		if (++told <= 20)
			say("romhack: %s is not listed: %s", path.c_str(), why.c_str());
		else if (told == 21)
			say("romhack: more patches are not listed; their reasons are left out of the log");
	};
	std::map<std::string, std::pair<uint32_t, uint32_t>> crcs;
	/* what the ROMs are, for a failure to be remembered against */
	std::map<std::string, uint64_t> rom_sizes;
	uint64_t signature = 1469598103934665603ull;
	for (const Game &r : roms) {
		rom_sizes[r.path] = r.size;
		for (const char c : r.path)
			signature = (signature ^ (unsigned char)c) * 1099511628211ull;
		signature = (signature ^ r.size) * 1099511628211ull;
	}
	unsigned listed = 0, read = 0;
	for (Game &p : patches) {
		{
			std::lock_guard lock(state().mutex);
			state().status = p.path;
		}
		auto it = std::lower_bound(known.begin(), known.end(), p.path,
			[](const Game &k, const std::string &path) { return k.path < path; });
		/* (the base's size as the last list had it: a replaced base is read again) */
		auto base_known = [&]() {
			if (it == known.end() || it->base.empty())
				return false;
			const auto now = rom_sizes.find(it->base);
			auto before = std::lower_bound(known.begin(), known.end(), it->base,
				[](const Game &k, const std::string &path) { return k.path < path; });
			return now != rom_sizes.end() && before != known.end() && before->path == it->base && before->size == now->second;
		};
		if (it != known.end() && it->path == p.path && it->size == p.size && it->facts && it->revision >= 0 && base_known()) {
			const int tab = p.tab;
			p = *it;
			p.tab = tab;
			found.push_back(p);
			listed++;
			continue;
		}
		const std::string folder = folderOf(p.path), l = lower(stem(p.path.substr(folder.size() + 1)));
		/* a patch that failed before, the same file (size) and the same base (path and size) or, with no base
		 * found, the same ROMs: not read again, the reason said once more */
		const auto failed = g_failed.find(p.path);
		if (failed != g_failed.end() && failed->second.size == p.size) {
			const Failed &f = failed->second;
			const auto b = rom_sizes.find(f.base);
			if (f.base.empty() ? f.signature == signature : (b != rom_sizes.end() && b->second == f.base_size)) {
				unlisted(p.path, f.why);
				continue;
			}
		}
		std::string base;
		/* (a failure of the file itself, a CRC32 that does not fit, no base: remembered; a read that failed, a share
		 * that did not answer, is tried again at the next search) */
		auto leave = [&](const std::string &why, bool permanent = true) {
			unlisted(p.path, why);
			if (!permanent)
				return;
			const auto b = rom_sizes.find(base);
			g_failed[p.path] = { p.size, why, base, b == rom_sizes.end() ? 0 : b->second, signature };
		};
		if (p.path.find_first_of("\t\n\r") != std::string::npos) {
			leave("its path holds a tab or a line break");
			continue;
		}
		std::vector<uint8_t> patch;
		std::string why;
		if (!readWhole(p.path, kMaxPatch, patch, &why, p.size)) {
			leave("the patch could not be read: " + why, false);
			continue;
		}
		romhack::Info info;
		if (!romhack::info(patch.data(), patch.size(), &info, &why)) {
			leave(why);
			continue;
		}
		/* does this ROM fit a BPS or UPS: its size and CRC32 (as stored or in .z64 order) are the declared ones
		 * (only a ROM of the right size is read) */
		auto fits = [&](const Game &r) {
			if (r.size == 0)
				return false;
			const bool as_source = r.size == info.source_size, as_target = info.either && r.size == info.target_size;
			if (!as_source && !as_target)
				return false;
			auto c = crcs.find(r.path);
			if (c == crcs.end()) {
				uint32_t raw = 0, z64 = 0;
				if (!crcsOfRom(r.path, &raw, &z64))
					return false;
				c = crcs.emplace(r.path, std::make_pair(raw, z64)).first;
			}
			for (const uint32_t crc : { c->second.first, c->second.second })
				if ((as_source && crc == info.source_crc) || (as_target && crc == info.target_crc))
					return true;
			return false;
		};
		/* (a) the same stem in the same folder; for a patch with a CRC it must fit (a size the listing did
		 * not give is taken as it is: applying checks it) */
		for (const Game &r : roms)
			if (folderOf(r.path) == folder && lower(stem(r.path.substr(folder.size() + 1))) == l &&
				(!info.has_crc || r.size == 0 || fits(r))) {
				base = r.path;
				break;
			}
		if (base.empty() && info.has_crc) { /* (b) by size and CRC32, the patch's folder first */
			for (int pass = 0; pass < 2 && base.empty(); pass++)
				for (const Game &r : roms)
					if ((folderOf(r.path) == folder) == (pass == 0) && fits(r)) {
						base = r.path;
						break;
					}
		}
		if (base.empty() && info.format == romhack::Format::Ips) { /* (c) the folder's only ROM */
			unsigned in_folder = 0;
			std::string only;
			for (const Game &g : found)
				if (g.base.empty() && rawRom(lower(g.path)) && folderOf(g.path) == folder) {
					in_folder++;
					only = g.path;
				}
			if (in_folder == 1)
				base = only;
		}
		if (base.empty()) {
			leave(info.has_crc ? "no ROM of its source size and CRC32 was found" : "no base ROM was found (the folder needs the ROM with the patch's name, or only one ROM)");
			continue;
		}
		if (base.find('\t') != std::string::npos) {
			leave("its base ROM's path holds a tab");
			continue;
		}
		/* the patched ROM's header gives its name, save type and CIC */
		std::vector<uint8_t> rom, result;
		if (!readWhole(base, romhack::kMaxTarget, rom, &why, rom_sizes[base])) {
			leave("the base ROM " + base + " could not be read: " + why, false);
			continue;
		}
		/* (only the header is made: the whole patched ROM, and its checks, are for when it is played) */
		if (!romhack::applyHead(patch.data(), patch.size(), rom.data(), rom.size(), 0x1000, result, &why)) {
			leave(why);
			continue;
		}
		read++;
		if (result.size() < 0x1000 || romhack::byteOrder(result.data()) != 0) {
			leave("the patched file is not an N64 ROM");
			continue;
		}
		p.base = base;
		namePatched(p);
		factsFromHeader(p, result.data());
		found.push_back(p);
		listed++;
	}
	say("romhack: %u patch(es) listed (%u applied for their headers), %zu not", listed, read, patches.size() - listed);
}

} // namespace

const char *tabName(int tab)
{
	return tab == TabUsb ? "USB" : tab == TabNetwork ? "Network" : "Internal";
}

int tabOfPath(const std::string &path)
{
	if (path.rfind("/mnt/usb", 0) == 0)
		return TabUsb;
	if (path.rfind("/smb/", 0) == 0)
		return TabNetwork;
	return TabInternal;
}

bool isRomName(const std::string &name)
{
	const std::string l = lower(name);
	return rawRom(l) || endsWith(l, ".zip") || endsWith(l, ".7z") || isDiskName(l);
}

bool isDiskSize(uint64_t size)
{
	/* 64DD images by size: the retail dump (NDD) and MAME's physical layout, whatever they are named
	 * (a share named them .n64, boot log 2026-11-27) */
	return size == 0x3DEC800 || size == 0x435B0C0;
}

bool isDiskFile(const std::string &path, uint64_t size)
{
	if (isDiskName(path))
		return true;
	if (!rawRom(lower(path)) || !isDiskSize(size))
		return false;
	/* a cartridge ROM of that size would start with an N64 header */
	uint8_t head[4] = {};
	FILE *f = ps5vfs_is_virtual(path.c_str()) ? ps5vfs_fopen(path.c_str(), "rb") : fopen(path.c_str(), "rb");
	if (!f)
		return true;
	const size_t got = ps5vfs_is_virtual(path.c_str()) ? ps5vfs_fread(head, 1, 4, f) : fread(head, 1, 4, f);
	ps5vfs_is_virtual(path.c_str()) ? ps5vfs_fclose(f) : fclose(f);
	return got != 4 || romhack::byteOrder(head) < 0;
}

bool isDiskName(const std::string &name)
{
	const std::string l = lower(name);
	return endsWith(l, ".ndd") || endsWith(l, ".d64");
}

bool isPatchName(const std::string &name)
{
	return romhack::formatOfName(name) != romhack::Format::None;
}

void Library::load()
{
	std::vector<Game> games;
	if (FILE *f = fopen(listFile().c_str(), "rb")) {
		char line[2048];
		int version = 1;
		while (fgets(line, sizeof(line), f)) {
			std::string text = line;
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
				text.pop_back();
			if (text.rfind("#psgopher-library ", 0) == 0) {
				version = atoi(text.c_str() + 18);
				continue;
			}
			std::vector<std::string> fields;
			size_t at = 0;
			const int count = version >= 5 ? 12 : version == 4 ? 11 : version == 3 ? 10 : version == 2 ? 9 : 3;
			for (int i = 0; i < count - 1; i++) {
				const size_t tab_at = text.find('\t', at);
				if (tab_at == std::string::npos)
					break;
				fields.push_back(text.substr(at, tab_at - at));
				at = tab_at + 1;
			}
			fields.push_back(text.substr(at));
			if ((int)fields.size() != count)
				continue;
			Game g;
			g.tab = std::clamp(atoi(fields[0].c_str()), 0, TabCount - 1);
			g.size = strtoull(fields[1].c_str(), nullptr, 10);
			g.path = fields.back();
			const size_t slash = g.path.rfind('/');
			g.name = stem(slash == std::string::npos ? g.path : g.path.substr(slash + 1));
			if (version >= 2) {
				g.facts = fields[2] == "1";
				g.cic = (uint32_t)strtoul(fields[3].c_str(), nullptr, 10);
				g.save = (uint32_t)strtoul(fields[4].c_str(), nullptr, 10);
				g.rumble = fields[5] == "1";
				g.id = fields[6] == "-" ? "" : fields[6];
				g.header = fields[7];
				if (version >= 3)
					g.crc = fields[8] == "-" ? "" : fields[8];
				if (version >= 4)
					g.revision = std::clamp(atoi(fields[9].c_str()), -1, 255);
				if (version >= 5) {
					g.base = fields[10] == "-" ? "" : fields[10];
					namePatched(g);
				}
				if (version < 3)
					g.facts = false; /* read again: v2 kept no key for cheats */
			}
			if (!g.path.empty())
				games.push_back(g);
		}
		fclose(f);
	}
	/* the patches' bases (a v5 list had them in its lines); a patch with none is not a game */
	std::map<std::string, std::string> bases;
	if (FILE *f = fopen(basesFile().c_str(), "rb")) {
		char line[4096];
		while (fgets(line, sizeof(line), f)) {
			std::string text = line;
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
				text.pop_back();
			const size_t tab_at = text.find('\t');
			if (tab_at != std::string::npos && tab_at > 0 && tab_at + 1 < text.size())
				bases[text.substr(0, tab_at)] = text.substr(tab_at + 1);
		}
		fclose(f);
	}
	for (Game &g : games) {
		if (!isPatchName(g.path))
			continue;
		const auto b = bases.find(g.path);
		if (g.base.empty() && b != bases.end()) {
			g.base = b->second;
			namePatched(g);
		}
	}
	games.erase(std::remove_if(games.begin(), games.end(), [](const Game &g) { return isPatchName(g.path) && g.base.empty(); }),
		games.end());
	sortGames(games);
	std::lock_guard lock(state().mutex);
	state().games = std::move(games);
	state().version++;
}

void Library::rescan()
{
	/* a search asked for while one runs (Game Folders changed, a share added) is not lost: the places
	 * were read at that search's start, so another runs when it ends */
	state().again = true; /* before the test: a search ending now sees it and runs again */
	if (state().scanning.exchange(true))
		return;
	state().again = false;
	std::thread([] {
		const auto started = std::chrono::steady_clock::now();
		std::vector<Game> found;
		std::vector<std::string> seen;
		std::vector<std::string> unlisted;
		std::vector<Game> patches;
		for (const Place &p : places()) {
			/* (a place inside one searched before is seen already: not a failure) */
			const bool already = std::find(seen.begin(), seen.end(), p.path) != seen.end();
			if (!search(p, found, patches, seen) && !already)
				unlisted.push_back(p.path);
		}
		/* a place that could not be listed keeps what the last list had there, with its facts,
		 * rather than losing every game on it to one failed listing */
		if (!unlisted.empty()) {
			std::vector<Game> known;
			{
				std::lock_guard lock(state().mutex);
				known = state().games;
			}
			for (const std::string &place : unlisted) {
				/* a USB drive that is gone (a chosen folder on it, unplugged) keeps nothing: its games
				 * would only fail to start */
				/* a place that is not there at all (a USB drive unplugged, a chosen folder deleted or on
				 * another drive now) keeps nothing: its games would only fail to start. One that is there
				 * but could not be listed (a share's timeout) keeps the last list's */
				if (place.rfind("/smb/", 0) != 0 && !isFolder(place)) {
					say("library: %s is not there: its games are left out", place.c_str());
					continue;
				}
				size_t kept = 0;
				for (const Game &g : known)
					if (g.path.size() > place.size() && g.path.compare(0, place.size(), place) == 0 &&
						g.path[place.size()] == '/' &&
						std::none_of(found.begin(), found.end(), [&](const Game &f) { return f.path == g.path; })) {
						found.push_back(g);
						kept++;
					}
				say("library: %s could not be listed: the last list's %zu game(s) there are kept", place.c_str(), kept);
			}
		}
		/* each ROM's header once: what the last list knew for the same file (path and size) stays */
		{
			std::vector<Game> known;
			{
				std::lock_guard lock(state().mutex);
				known = state().games;
			}
			std::sort(known.begin(), known.end(), [](const Game &a, const Game &b) { return a.path < b.path; });
			unsigned read = 0;
			for (Game &g : found) {
				auto it = std::lower_bound(known.begin(), known.end(), g.path,
					[](const Game &k, const std::string &path) { return k.path < path; });
				if (it != known.end() && it->path == g.path && it->size == g.size && it->facts && it->revision >= 0) {
					g.facts = true;
					g.id = it->id;
					g.header = it->header;
					g.cic = it->cic;
					g.save = it->save;
					g.rumble = it->rumble;
					g.crc = it->crc;
					g.revision = it->revision;
					continue;
				}
				{
					std::lock_guard lock(state().mutex);
					state().status = g.path;
				}
				readFacts(g);
				read++;
			}
			say("library: %u ROM header(s) read", read);
			/* the 64DD's IPL under another name: its header's ID */
			const size_t before = found.size();
			found.erase(std::remove_if(found.begin(), found.end(),
			                           [](const Game &g) { return g.facts && (g.id == "NDDJ" || g.id == "NDDE" || g.id == "NDXJ"); }),
			            found.end());
			if (found.size() != before)
				say("library: %zu 64DD IPL file(s) left out of the list (BENCH > Emulation > 64DD IPL chooses one)", before - found.size());
			/* the romhack patches, each with its base ROM (they need the ROMs' sizes, so after the search) */
			resolvePatches(patches, found, known);
		}
		/* one entry per file: a place inside another (/data/psgopher/roms in /data) is seen once */
		sortGames(found);
		save(found);
		say("library: %zu game(s) found in %.1f s", found.size(),
			std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
		{
			std::lock_guard lock(state().mutex);
			state().games = std::move(found);
			state().version++;
			state().status.clear();
		}
		state().scanning = false;
		/* asked for while this ran: once more (rescan() sets `again` before it tests `scanning`, so
		 * a request is never lost; one that starts its own search first makes this one a no-op) */
		if (state().again.load())
			library().rescan();
	}).detach();
}

bool Library::scanning() const
{
	return state().scanning.load();
}

std::vector<Game> Library::games(unsigned *version) const
{
	std::lock_guard lock(state().mutex);
	if (version)
		*version = state().version;
	return state().games;
}

std::string Library::status() const
{
	std::lock_guard lock(state().mutex);
	return state().status;
}

std::string libraryRoot() { return g_root; }

std::string gameKey(const Game &g, bool *from_header)
{
	auto clean = [](const std::string &in) {
		std::string out;
		for (const char c : in)
			out += (unsigned char)c < 0x20 || c == 0x7f || strchr("/\\:*?\"<>|", c) ? '_' : c;
		while (!out.empty() && out[0] == '.')
			out[0] = '_';
		return out.substr(0, 96);
	};
	if (g.facts && g.id.size() == 4) {
		int revision = g.revision;
		if (revision < 0) {
			uint8_t head[0x1000];
			if (readHeader(g.path, head))
				revision = head[0x3F];
		}
		if (revision >= 0) {
			if (from_header)
				*from_header = true;
			char suffix[8];
			snprintf(suffix, sizeof(suffix), "-%02X", revision & 0xFF);
			std::string key = clean(g.id) + suffix;
			/* a romhack patch often keeps its base's code: its own settings, by the hack's CRC1 ("NSME-00-1A2B3C4D") */
			if (!g.base.empty() && g.crc.size() >= 8)
				key += "-" + clean(g.crc.substr(0, 8));
			return key;
		}
	}
	if (from_header)
		*from_header = false;
	return "file-" + clean(g.name);
}

void libraryUseRoot(const std::string &root)
{
	g_root = root;
}

std::vector<std::string> gameFolders()
{
	std::vector<std::string> out;
	FILE *f = fopen(foldersFile().c_str(), "rb");
	if (!f)
		return out;
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		std::string path = line;
		while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
			path.pop_back();
		while (path.size() > 1 && path.back() == '/')
			path.pop_back();
		if (!path.empty() && path[0] == '/' && std::find(out.begin(), out.end(), path) == out.end())
			out.push_back(path);
	}
	fclose(f);
	return out;
}

bool setGameFolders(const std::vector<std::string> &folders)
{
	const std::string file = foldersFile(), temporary = file + ".new";
	mkdir((g_root + "/psgopher").c_str(), 0777);
	mkdir((g_root + "/psgopher/config").c_str(), 0777);
	chmod((g_root + "/psgopher/config").c_str(), 0777);
	if (folders.empty())
		return unlink(file.c_str()) == 0 || errno == ENOENT;
	std::string text;
	for (const std::string &folder : folders)
		text += folder + "\n";
	FILE *f = fopen(temporary.c_str(), "wb");
	if (!f)
		return false;
	const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
	fclose(f);
	if (ok && rename(temporary.c_str(), file.c_str()) == 0) {
		chmod(file.c_str(), 0666);
		return true;
	}
	unlink(temporary.c_str());
	return false;
}

Library &library()
{
	static Library l;
	return l;
}

std::string saveText(uint32_t save)
{
	if (save == 0)
		return "None";
	if (save == (1u | 4u))
		return "EEPROM 4K or SRAM";
	static const char *const names[] = { "EEPROM 4K", "EEPROM 16K", "SRAM", "SRAM 768K", "SRAM 1M", "Flash" };
	std::string out;
	for (int i = 0; i < 6; i++)
		if (save & (1u << i))
			out += (out.empty() ? "" : " + ") + std::string(names[i]);
	return out;
}

std::string cicText(uint32_t cic)
{
	return cic ? "CIC-" + std::to_string(cic) : "Unknown";
}

std::string regionText(const std::string &id)
{
	if (id.size() < 4)
		return "";
	switch (id[3]) {
	case 'E': return "USA";
	case 'J': return "Japan";
	case 'P': case 'X': case 'Y': return "Europe";
	case 'D': return "Germany";
	case 'F': return "France";
	case 'I': return "Italy";
	case 'S': return "Spain";
	case 'U': return "Australia";
	case 'A': return "Asia";
	case 'B': return "Brazil";
	case 'C': return "China";
	case 'K': return "Korea";
	default: return "";
	}
}

std::string saveName(const std::string &header)
{
	std::string out;
	for (const char c : header)
		if (std::isalnum((unsigned char)c) || c == '_' || c == ' ' || c == '-')
			out += c;
	const size_t first = out.find_first_not_of(" \t"), last = out.find_last_not_of(" \t");
	return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
}

std::vector<Probe> probesOf(const Game &g)
{
	std::vector<Probe> out;
	const std::string prefix = saveName(g.header) + "-";
	if (prefix.size() < 2)
		return out;
	if (DIR *d = opendir(statesFolder().c_str())) {
		while (dirent *e = readdir(d)) {
			const std::string name = e->d_name;
			const size_t dot = name.rfind(".state");
			if (name.rfind(prefix, 0) != 0 || dot == std::string::npos || dot + 7 != name.size() ||
				!std::isdigit((unsigned char)name.back()))
				continue;
			struct stat st;
			if (stat((statesFolder() + "/" + name).c_str(), &st) == 0)
				out.push_back({ name.back() - '0', (int64_t)st.st_mtime });
		}
		closedir(d);
	}
	std::sort(out.begin(), out.end(), [](const Probe &a, const Probe &b) { return a.time > b.time; });
	return out;
}

namespace {
std::mutex g_played_mutex;
std::vector<std::pair<std::string, Played>> readPlayed()
{
	std::vector<std::pair<std::string, Played>> all;
	if (FILE *f = fopen(playedFile().c_str(), "rb")) {
		char line[1024];
		while (fgets(line, sizeof(line), f)) {
			double seconds = 0;
			long long last = 0;
			char name[900] = {};
			if (sscanf(line, "%lf\t%lld\t%899[^\n]", &seconds, &last, name) == 3)
				all.push_back({ name, { seconds, (int64_t)last } });
		}
		fclose(f);
	}
	return all;
}
} // namespace

std::vector<std::pair<std::string, Played>> playedAll()
{
	std::lock_guard lock(g_played_mutex);
	return readPlayed();
}

std::string Library::statesFolder() const
{
	return ::psgopher::statesFolder();
}

Played playedOf(const std::string &name)
{
	std::lock_guard lock(g_played_mutex);
	for (const auto &p : readPlayed())
		if (p.first == name)
			return p.second;
	return {};
}

void addPlayed(const std::string &name, double seconds)
{
	std::lock_guard lock(g_played_mutex);
	auto all = readPlayed();
	bool there = false;
	for (auto &p : all)
		if (p.first == name) {
			p.second.seconds += seconds;
			p.second.last = (int64_t)time(nullptr);
			there = true;
		}
	if (!there)
		all.push_back({ name, { seconds, (int64_t)time(nullptr) } });
	std::string text;
	char line[1024];
	for (const auto &p : all) {
		snprintf(line, sizeof(line), "%.0f\t%lld\t%s\n", p.second.seconds, (long long)p.second.last, p.first.c_str());
		text += line;
	}
	const std::string file = playedFile(), temporary = file + ".new";
	if (FILE *f = fopen(temporary.c_str(), "wb")) {
		const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
		fclose(f);
		if (ok && rename(temporary.c_str(), file.c_str()) == 0)
			chmod(file.c_str(), 0666);
		else
			unlink(temporary.c_str());
	}
}

std::string localCopy(const std::string &path, std::string *error)
{
	/* a romhack patch is made into its ROM here (the copy a share's game gets, with the menu's copy notice
	 * up): the result is in the play folder, as a copy would be */
	if (isPatchName(path))
		return playablePath(path, error);
	if (!ps5vfs_is_virtual(path.c_str()))
		return path;
	const std::string folder = playFolder();
	mkdir(folder.c_str(), 0777);
	chmod(folder.c_str(), 0777);
	/* the last one copied goes: one ROM at a time is kept */
	if (DIR *d = opendir(folder.c_str())) {
		while (dirent *e = readdir(d))
			if (e->d_name[0] != '.')
				unlink((folder + "/" + e->d_name).c_str());
		closedir(d);
	}
	const size_t slash = path.rfind('/');
	const std::string target = folder + "/" + path.substr(slash + 1);
	FILE *in = ps5vfs_fopen(path.c_str(), "rb");
	if (!in) {
		if (error)
			*error = std::string("the share did not open it: ") + strerror(errno);
		return {};
	}
	/* read whole (a ROM is at most 256 MiB: the cartridge space is 252 MB, which romhacks use), then one write: a title's write()
	 * costs about 3 ms each, whatever its size */
	std::vector<unsigned char> data;
	unsigned char block[1 << 16];
	size_t n;
	while ((n = ps5vfs_fread(block, 1, sizeof(block), in)) > 0) {
		data.insert(data.end(), block, block + n);
		if (data.size() > (256u << 20))
			break;
	}
	const bool failed = ps5vfs_ferror(in) != 0;
	ps5vfs_fclose(in);
	if (failed || data.empty() || data.size() > (256u << 20)) {
		if (error)
			*error = failed ? "the share stopped answering" : data.empty() ? "the file is empty" : "the file is too large for a ROM";
		return {};
	}
	/* PSGopher: from OGBox db63d46 (the write broker): a copy under /data/ is written by the helper
	 * payload (the console holds back the title's own writes after about 1.3 GiB; the helper's are not);
	 * when it is off, not there or fails, the title writes the copy itself, as before */
	std::string helperWhy;
	if (helperWriteFile(g_root, target, data.data(), data.size(), &helperWhy)) {
		chmod(target.c_str(), 0666);
		say("library: %s copied from the share (%zu KiB, by the helper)", path.c_str(), data.size() / 1024);
		return target;
	}
	unlink(target.c_str()); /* (what the helper left of it: its file may not be the title's to open) */
	FILE *out = fopen(target.c_str(), "wb");
	if (!out || fwrite(data.data(), 1, data.size(), out) != data.size()) {
		if (out)
			fclose(out);
		if (error)
			*error = "it could not be written to " + folder + ": " + strerror(errno);
		return {};
	}
	fclose(out);
	chmod(target.c_str(), 0666);
	say("library: %s copied from the share (%zu KiB)", path.c_str(), data.size() / 1024);
	return target;
}

std::string playablePath(const std::string &path, std::string *error)
{
	if (!isPatchName(path))
		return path;
	auto fail = [&](const std::string &why) {
		if (error)
			*error = why;
		return std::string();
	};
	const std::string patch_name = path.substr(path.rfind('/') + 1);
	std::string base;
	uint64_t patch_size = 0, base_size = 0;
	std::vector<Game> listed;
	/* (a run started before the list was loaded, by autostart.txt or benchmark.txt: the kept list is read once) */
	for (int attempt = 0; attempt < 2 && base.empty(); attempt++) {
		if (attempt == 1)
			library().load();
		listed = library().games();
		for (const Game &g : listed)
			if (g.path == path) {
				base = g.base;
				patch_size = g.size;
				break;
			}
	}
	for (const Game &g : listed)
		if (!base.empty() && g.path == base)
			base_size = g.size;
	if (base.empty())
		return fail(patch_name + ": no base ROM is known for this patch (it is listed only with one)");
	std::vector<uint8_t> patch, rom, result;
	std::string why;
	if (!readWhole(path, kMaxPatch, patch, &why, patch_size))
		return fail(patch_name + ": the patch could not be read: " + why);
	if (!readWhole(base, romhack::kMaxTarget, rom, &why, base_size))
		return fail(patch_name + ": its base ROM " + base.substr(base.rfind('/') + 1) + " could not be read: " + why);
	if (!romhack::apply(patch.data(), patch.size(), rom.data(), rom.size(), result, &why))
		return fail(patch_name + ": " + why);
	rom.clear(), rom.shrink_to_fit();
	/* as localCopy: the last one written goes, one ROM at a time is kept */
	const std::string folder = playFolder();
	mkdir(folder.c_str(), 0777);
	chmod(folder.c_str(), 0777);
	if (DIR *d = opendir(folder.c_str())) {
		while (dirent *e = readdir(d))
			if (e->d_name[0] != '.')
				unlink((folder + "/" + e->d_name).c_str());
		closedir(d);
	}
	const std::string target = folder + "/" + stem(patch_name) + ".z64";
	const romhack::Format format = romhack::formatOfData(patch.data(), patch.size());
	std::string helperWhy;
	if (helperWriteFile(g_root, target, result.data(), result.size(), &helperWhy)) {
		chmod(target.c_str(), 0666);
	} else {
		unlink(target.c_str());
		FILE *out = fopen(target.c_str(), "wb");
		if (!out || fwrite(result.data(), 1, result.size(), out) != result.size()) {
			if (out)
				fclose(out);
			return fail(patch_name + ": the patched ROM could not be written to " + folder + ": " + strerror(errno));
		}
		fclose(out);
		chmod(target.c_str(), 0666);
	}
	say("romhack: %s applied to %s (%s, %zu KiB)", path.c_str(), base.c_str(), romhack::formatName(format), result.size() / 1024);
	return target;
}

} // namespace psgopher
