/*
 * PSGopher - the game list (library.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "library.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5_vfs.h"

extern "C" void say(const char *format, ...) __attribute__((format(printf, 1, 2)));

namespace psgopher {

namespace {

std::string g_root = "/data"; // the console's /data (the host tests' folder)
std::string listFile() { return g_root + "/psgopher/cache/library.tsv"; }
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
	for (const Game &g : games)
		text += std::to_string(g.tab) + "\t" + std::to_string(g.size) + "\t" + g.path + "\n";
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
			if (rom)
				found.push_back({ stem(e.name), full, e.size, place.tab });
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
		while (fgets(line, sizeof(line), f)) {
			int tab = 0;
			unsigned long long size = 0;
			char path[1900] = {};
			if (sscanf(line, "%d\t%llu\t%1899[^\n]", &tab, &size, path) != 3)
				continue;
			const std::string p = path;
			const size_t slash = p.rfind('/');
			games.push_back({ stem(slash == std::string::npos ? p : p.substr(slash + 1)), p, size,
				std::clamp(tab, 0, TabCount - 1) });
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
		const double started = (double)clock() / CLOCKS_PER_SEC;
		std::vector<Game> found;
		std::vector<std::string> seen;
		for (const Place &p : places())
			search(p, found, seen);
		/* one entry per file: a place inside another (/data/psgopher/roms in /data) is seen once */
		sortGames(found);
		save(found);
		say("library: %zu game(s) found (%.1f s of CPU)", found.size(), (double)clock() / CLOCKS_PER_SEC - started);
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
