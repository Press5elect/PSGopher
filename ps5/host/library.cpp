/*
 * PSGopher - the game list (library.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
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
#include <mutex>
#include <thread>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5_vfs.h"

extern "C" void say(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* Gopher64's facts of a cartridge (vendor/gopher64/src/psgopher.rs); weak, so the host tests may
 * give their own or none */
struct PsgopherRomFacts {
	uint32_t cic, save, rumble;
};
extern "C" bool psgopher_rom_facts(const uint8_t *rom, size_t len, PsgopherRomFacts *out) __attribute__((weak));

namespace psgopher {

namespace {

std::string g_root = "/data"; // the console's /data (the host tests' folder)
std::string listFile() { return g_root + "/psgopher/cache/library.tsv"; }
std::string playedFile() { return g_root + "/psgopher/cache/played.tsv"; }
std::string statesFolder() { return g_root + "/psgopher/data/states"; }
std::string playFolder() { return g_root + "/psgopher/cache/play"; }
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

/* The game's facts from its ROM's header (left as they are for a .zip or .7z, or an unreadable one) */
void readFacts(Game &g)
{
	const std::string l = lower(g.path);
	if (!rawRom(l))
		return;
	uint8_t head[0x1000];
	if (!readHeader(g.path, head))
		return;
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
	PsgopherRomFacts f = {};
	if (psgopher_rom_facts && psgopher_rom_facts(head, sizeof(head), &f)) {
		g.cic = f.cic;
		g.save = f.save;
		g.rumble = f.rumble != 0;
	}
	g.facts = true;
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
	return full == own + "cache" || full == own + "data" || full == own + "config";
}

std::string stem(const std::string &name)
{
	const size_t dot = name.rfind('.');
	return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

struct Place {
	std::string path;
	int tab;
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
	/* v3: tab, size, facts, cic, save, rumble, id, header, crc, path (the path last: it may hold
	 * anything but a newline; the header's name has no tab) */
	text += "#psgopher-library 3\n";
	for (const Game &g : games) {
		std::string header = g.header;
		std::replace(header.begin(), header.end(), '\t', ' ');
		text += std::to_string(g.tab) + "\t" + std::to_string(g.size) + "\t" + (g.facts ? "1" : "0") + "\t" +
			std::to_string(g.cic) + "\t" + std::to_string(g.save) + "\t" + (g.rumble ? "1" : "0") + "\t" +
			(g.id.empty() ? "-" : g.id) + "\t" + header + "\t" + (g.crc.empty() ? "-" : g.crc) + "\t" + g.path + "\n";
	}
	const std::string file = listFile(), temporary = file + ".new";
	if (FILE *f = fopen(temporary.c_str(), "wb")) {
		const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size(); // one write: few and small
		fclose(f);
		if (ok && rename(temporary.c_str(), file.c_str()) == 0)
			chmod(file.c_str(), 0666);
		else
			unlink(temporary.c_str());
	}
}

void search(const Place &place, std::vector<Game> &found, std::vector<std::string> &seen)
{
	const bool share = place.tab == TabNetwork;
	unsigned budget = share ? kShareBudget : kLocalBudget;
	struct Folder {
		std::string path;
		int depth;
	};
	std::vector<Folder> todo{ { place.path, 0 } };
	std::vector<Entry> entries;
	while (!todo.empty() && budget > 0) {
		const Folder folder = todo.back();
		todo.pop_back();
		if (std::find(seen.begin(), seen.end(), folder.path) != seen.end())
			continue;
		seen.push_back(folder.path);
		{
			std::lock_guard lock(state().mutex);
			state().status = folder.path;
		}
		if (!listFolder(folder.path, entries))
			continue;
		const bool n64Folder = lower(folder.path).find("n64") != std::string::npos ||
			folder.path.rfind(g_root + "/psgopher/roms", 0) == 0;
		for (const Entry &e : entries) {
			if (budget == 0)
				break;
			budget--;
			const std::string full = folder.path + "/" + e.name;
			if (e.folder) {
				if (folder.depth < kDepth && !skipFolder(full, e.name))
					todo.push_back({ full, folder.depth + 1 });
				continue;
			}
			const std::string l = lower(e.name);
			bool rom = rawRom(l);
			if (!rom && endsWith(l, ".zip"))
				rom = zipHoldsRom(full);
			if (!rom && endsWith(l, ".7z"))
				rom = n64Folder;
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
		say("library: %s: searched as far as its budget goes (%u entries)", place.path.c_str(),
			share ? kShareBudget : kLocalBudget);
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
	return rawRom(l) || endsWith(l, ".zip") || endsWith(l, ".7z");
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
			const int count = version >= 3 ? 10 : version == 2 ? 9 : 3;
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
				else
					g.facts = false; /* read again: v2 kept no key for cheats */
			}
			if (!g.path.empty())
				games.push_back(g);
		}
		fclose(f);
	}
	sortGames(games);
	std::lock_guard lock(state().mutex);
	state().games = std::move(games);
	state().version++;
}

void Library::rescan()
{
	if (state().scanning.exchange(true))
		return;
	std::thread([] {
		const auto started = std::chrono::steady_clock::now();
		std::vector<Game> found;
		std::vector<std::string> seen;
		for (const Place &p : places())
			search(p, found, seen);
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
				if (it != known.end() && it->path == g.path && it->size == g.size && it->facts) {
					g.facts = true;
					g.id = it->id;
					g.header = it->header;
					g.cic = it->cic;
					g.save = it->save;
					g.rumble = it->rumble;
					g.crc = it->crc;
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

void libraryUseRoot(const std::string &root)
{
	g_root = root;
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
	/* read whole (a ROM is at most 64 MiB, a zip of one less), then one write: a title's write()
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

} // namespace psgopher
