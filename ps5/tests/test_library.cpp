/*
 * PSGopher - host tests of the game list's search (library.cpp), the title's settings file
 * (title_settings.cpp) and the shares file (shares.cpp), on the PC. ps5vfs is stood in for here: a
 * path under /smb/ is a folder of the test's own (the share), served through the ps5vfs calls the
 * list makes, so the share's path (listing, the zip check, the copy) is the one tested.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include "expansion_pak.h"
#include "game_settings.h"
#include "library.h"
#include "shares.h"
#include "title_settings.h"
#include "write_broker.h"
#include "../broker/server.hpp"

#include <cerrno>
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
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "ps5_vfs.h"

static int failures = 0;
#define CHECK(x)                                                                    \
	do {                                                                            \
		if (!(x)) {                                                                 \
			std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x);     \
			failures++;                                                             \
		}                                                                           \
	} while (0)

static std::vector<std::string> g_log; /* what was said, for the checks of the log's lines */
extern "C" void say(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	char line[1024];
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	g_log.push_back(line);
	std::fprintf(stdout, "%s\n", line);
}
static bool said(const std::string &text)
{
	for (const std::string &l : g_log)
		if (l == text)
			return true;
	return false;
}

/* Gopher64's settings, stood in for (the rows a game's own settings change) */
static std::map<std::string, int64_t> g_gopher = { { "video.upscale", 2 }, { "video.ssaa", 0 }, { "video.crt", 0 },
	{ "emulation.overclock", 0 }, { "emulation.disable_expansion_pak", 0 }, { "input.controller_enabled.1", 0 } };
extern "C" int64_t psgopher_setting_get(const char *name)
{
	const auto it = g_gopher.find(name);
	return it == g_gopher.end() ? -1 : it->second;
}
extern "C" bool psgopher_setting_set(const char *name, int64_t value)
{
	const auto it = g_gopher.find(name);
	if (it == g_gopher.end())
		return false;
	it->second = value;
	return true;
}

/* ---------------------------------------------------------------- ps5vfs, stood in for */

static std::string g_share; // the folder /smb/nas stands for

static std::string real(const char *path)
{
	return g_share + (path + strlen("/smb/nas"));
}
extern "C" int ps5vfs_is_virtual(const char *path)
{
	return strncmp(path, "/smb/", 5) == 0;
}
extern "C" const char *ps5vfs_mount_point(unsigned index)
{
	return index == 0 ? "/smb/nas" : nullptr;
}
struct ps5vfs_dir {
	DIR *d;
	std::string name;
};
static int g_share_fails = 0; /* the share's own folder fails to list this many times */
extern "C" ps5vfs_dir *ps5vfs_opendir(const char *path)
{
	if (g_share_fails > 0 && std::string(path) == "/smb/nas") {
		g_share_fails--;
		return nullptr;
	}
	DIR *d = opendir(real(path).c_str());
	return d ? new ps5vfs_dir{ d, {} } : nullptr;
}
extern "C" int ps5vfs_readdir(ps5vfs_dir *dir, struct ps5vfs_dirent *entry)
{
	while (dirent *e = readdir(dir->d)) {
		if (e->d_name[0] == '.')
			continue;
		dir->name = e->d_name;
		entry->name = dir->name.c_str();
		entry->is_directory = e->d_type == DT_DIR;
		entry->size = 0;
		entry->mtime = 0;
		return 1;
	}
	return 0;
}
extern "C" void ps5vfs_closedir(ps5vfs_dir *dir)
{
	closedir(dir->d);
	delete dir;
}
static int g_open_files = 0;
extern "C" FILE *ps5vfs_fopen(const char *path, const char *mode)
{
	FILE *f = std::fopen(real(path).c_str(), mode);
	if (f)
		g_open_files++;
	return f;
}
extern "C" size_t ps5vfs_fread(void *buffer, size_t size, size_t count, FILE *fp) { return std::fread(buffer, size, count, fp); }
extern "C" int ps5vfs_ferror(FILE *fp) { return std::ferror(fp); }
extern "C" int ps5vfs_fclose(FILE *fp)
{
	g_open_files--;
	return std::fclose(fp);
}

/* ---------------------------------------------------------------- Gopher64's facts, stood in for */

struct PsgopherRomFacts {
	uint32_t cic, save, rumble;
};
static int g_facts_calls = 0;
extern "C" bool psgopher_rom_facts(const uint8_t *rom, size_t len, PsgopherRomFacts *out)
{
	g_facts_calls++;
	/* always given in Gopher64's byte order (.z64), 4 KiB */
	CHECK(len == 0x1000);
	CHECK(rom[0] == 0x80 && rom[1] == 0x37 && rom[2] == 0x12 && rom[3] == 0x40);
	*out = { rom[0x3C] == 'C' ? 6102u : 6105u, rom[0x3C] == 'C' ? 1u : 32u, 0u };
	return true;
}

/* a ROM's first 4 KiB and more: header name, code, then in the byte order asked */
static std::string romImage(const char *name, const char *code, const char *order, size_t size = 0x2000, unsigned char revision = 0)
{
	std::string rom(size, '\0');
	const unsigned char magic[4] = { 0x80, 0x37, 0x12, 0x40 };
	memcpy(&rom[0], magic, 4);
	char title[20];
	memset(title, ' ', sizeof(title));
	memcpy(title, name, strlen(name));
	memcpy(&rom[0x20], title, 20);
	memcpy(&rom[0x3B], code, 4);
	rom[0x3F] = (char)revision;
	if (!strcmp(order, "v64"))
		for (size_t i = 0; i + 1 < rom.size(); i += 2)
			std::swap(rom[i], rom[i + 1]);
	else if (!strcmp(order, "n64"))
		for (size_t i = 0; i + 3 < rom.size(); i += 4) {
			std::swap(rom[i], rom[i + 3]);
			std::swap(rom[i + 1], rom[i + 2]);
		}
	return rom;
}

/* ---------------------------------------------------------------- helpers */

static void write(const std::string &path, const std::string &bytes)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
}

static void folders(const std::string &path)
{
	std::string sofar;
	for (size_t i = 1; i <= path.size(); i++)
		if (i == path.size() || path[i] == '/')
			mkdir(path.substr(0, i).c_str(), 0777);
}

/* a zip's first local header naming `name` (the list reads no further) */
static std::string zipNaming(const std::string &name)
{
	std::string z = "PK\x03\x04";
	z += std::string(22, '\0');
	z += (char)(name.size() & 0xff);
	z += (char)(name.size() >> 8);
	z += std::string(2, '\0');
	z += name;
	z += "data";
	return z;
}

static std::string slurp(const std::string &path)
{
	std::string out;
	if (FILE *f = std::fopen(path.c_str(), "rb")) {
		char b[4096];
		size_t n;
		while ((n = std::fread(b, 1, sizeof(b), f)) > 0)
			out.append(b, n);
		std::fclose(f);
	}
	return out;
}

static const psgopher::Game *find(const std::vector<psgopher::Game> &games, const std::string &name)
{
	for (const auto &g : games)
		if (g.name == name)
			return &g;
	return nullptr;
}

/* ---------------------------------------------------------------- the copy through the write broker */

/* A broker (the payload's own request handler, ps5/broker/server.cpp) on one end of a socketpair in a
 * thread; `dieAfter` makes it carry out that many requests and drop the connection, the last one
 * unanswered (a broker that dies part-way). Its "/data" is `data`, by a symlink under `base`. */
struct FakeBroker {
	std::vector<std::thread> threads;
	int launches = 0;
	unsigned dieAfter = 0;
	bool refuse = false;     /* the loader is not there */
	bool deadOnArrival = false; /* started, but it closes the connection at once */
	std::string base;
	int connect(std::string &why)
	{
		launches++;
		if (refuse) {
			why = "no payload loader at 127.0.0.1:9021 (errno 111: Connection refused)";
			return -1;
		}
		int fds[2];
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
			return -1;
		if (deadOnArrival) {
			close(fds[1]);
		} else {
			threads.emplace_back([this, server = fds[1]]() {
				::broker::ServerOptions o;
				o.root = base.c_str();
				o.die_after = dieAfter;
				o.message_ms = 5000;
				::broker::serve(server, server, o);
				close(server);
			});
		}
		why = "write-broker.elf sent to a fake loader";
		return fds[0];
	}
	void end()
	{
		psgopher::helperReset(); /* the connection closed: the broker ends */
		for (auto &t : threads)
			t.join();
		threads.clear();
	}
};

static void copyThroughHelper(const std::string &data)
{
	FakeBroker fake;
	fake.base = data + "-broker";
	folders(fake.base);
	char absolute[4096];
	CHECK(realpath(data.c_str(), absolute) != nullptr); /* (a relative link would be read from base) */
	CHECK(symlink(absolute, (fake.base + "/data").c_str()) == 0);
	psgopher::helperUseConnector([&fake](std::string &why) { return fake.connect(why); });
	std::string error;
	const std::string want(70000, 'd');
	const std::string target = data + "/psgopher/cache/play/Delta.z64";
	auto copy = [&]() {
		const std::string got = psgopher::localCopy("/smb/nas/n64/Delta.z64", &error);
		CHECK(got == target);
		CHECK(slurp(target) == want); /* byte for byte, whatever the broker did */
	};

	/* the broker is there: it writes the copy, once launched for the run */
	psgopher::helperReset();
	setenv("PSGOPHER_WRITE_HELPER", "1", 1);
	copy();
	CHECK(fake.launches == 1);
	copy();
	CHECK(fake.launches == 1);
	fake.end();

	/* the setting off: the broker is not asked for */
	psgopher::helperReset();
	setenv("PSGOPHER_WRITE_HELPER", "0", 1);
	copy();
	CHECK(fake.launches == 1);
	setenv("PSGOPHER_WRITE_HELPER", "1", 1);

	/* the broker dies part-way (the 2nd request is its open, the 3rd the write, the 4th the sync,
	 * the 5th the close: carried out, not answered): the title writes the copy, whole */
	for (unsigned die : { 1u, 2u, 3u, 4u, 5u }) {
		psgopher::helperReset();
		fake.dieAfter = die;
		const int before = fake.launches;
		copy();
		CHECK(fake.launches >= before + 1);
		fake.end();
	}
	fake.dieAfter = 0;

	/* the broker never starts, or closes the connection at once: the title writes the copy */
	psgopher::helperReset();
	fake.refuse = true;
	copy();
	fake.refuse = false;
	fake.deadOnArrival = true;
	psgopher::helperReset();
	copy();
	fake.deadOnArrival = false;
	fake.end();

	/* a file the broker left behind that the title may not write over is replaced */
	psgopher::helperReset();
	fake.dieAfter = 4;
	write(target, std::string(10, 'x'));
	chmod(target.c_str(), 0444);
	copy();
	fake.dieAfter = 0;
	fake.end();

	/* a copy outside /data (the console's USB) is not for the broker */
	psgopher::helperReset();
	std::string why;
	CHECK(!psgopher::helperWriteFile("/data", "/mnt/usb0/x.z64", (const unsigned char *)"abc", 3, &why) && !why.empty());
	CHECK(!psgopher::helperWriteFile("/data", "/data/../etc/x", (const unsigned char *)"abc", 3, &why));

	psgopher::helperUseConnector(nullptr);
	psgopher::helperReset();
}

int main(int argc, char **argv)
{
	const std::string base = argc > 1 ? argv[1] : "/tmp/psgopher-host-tests";
	if (std::system(("rm -rf '" + base + "'").c_str()) != 0)
		return 2;
	const std::string data = base + "/data", share = base + "/share";
	g_share = share;

	/* the console's /data, with the title's folders and ROMs in several places */
	folders(data + "/psgopher/roms");
	folders(data + "/psgopher/cache");
	folders(data + "/psgopher/config");
	folders(data + "/games/n64/deep/er");
	folders(data + "/other/a/b/c/d/e/f/g");
	write(data + "/psgopher/roms/Alpha (U).z64", romImage("COMET KART", "NCKE", "z64", 4096));
	write(data + "/psgopher/roms/beta.V64", romImage("MOSS TEMPLE", "NMTP", "v64", 0x2000, 2));
	write(data + "/psgopher/roms/packed.zip", zipNaming("inside.n64"));
	write(data + "/psgopher/roms/notrom.zip", zipNaming("readme.txt"));
	write(data + "/psgopher/roms/seven.7z", "7z");
	write(data + "/games/n64/deep/er/gamma.n64", romImage("HARBOR NIGHTS", "NHNJ", "n64", 0x2000, 11));
	write(data + "/games/random.7z", "7z");           /* not in an N64 folder */
	write(data + "/games/eboot.bin", "b");            /* .bin is never listed */
	write(data + "/other/a/b/c/d/e/f/g/toodeep.z64", "x");
	write(data + "/psgopher/cache/cached.z64", "x");   /* the title's own cache is skipped */
	write(data + "/psgopher/roms/.hidden.z64", "x");
	/* the share */
	folders(share + "/n64");
	write(share + "/n64/Delta.z64", std::string(70000, 'd'));
	write(data + "/psgopher/roms/[BIOS] PIF (Japan, USA).v64", std::string(2048, 'p')); /* too small for a ROM */
	write(share + "/n64/shared.zip", zipNaming("folder/inner.v64"));

	CHECK(psgopher::isRomName("x.Z64") && psgopher::isRomName("x.zip") && !psgopher::isRomName("x.bin"));
	CHECK(psgopher::tabOfPath("/mnt/usb0/a.z64") == psgopher::TabUsb);
	CHECK(psgopher::tabOfPath("/smb/nas/a.z64") == psgopher::TabNetwork);
	CHECK(psgopher::tabOfPath("/data/a.z64") == psgopher::TabInternal);

	psgopher::libraryUseRoot(data);
	auto &lib = psgopher::library();
	lib.load();
	CHECK(lib.games().empty());
	lib.rescan();
	for (int i = 0; i < 500 && lib.scanning(); i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	CHECK(!lib.scanning());
	unsigned version = 0;
	const auto games = lib.games(&version);
	for (const auto &g : games)
		std::printf("  found %-14s %-8s %s\n", g.name.c_str(), psgopher::tabName(g.tab), g.path.c_str());
	CHECK(find(games, "Alpha (U)") && find(games, "Alpha (U)")->size == 4096);
	CHECK(find(games, "beta"));
	CHECK(find(games, "packed"));
	CHECK(!find(games, "notrom"));
	CHECK(find(games, "seven"));
	CHECK(find(games, "gamma"));
	CHECK(!find(games, "random"));
	CHECK(!find(games, "eboot"));
	CHECK(!find(games, "toodeep"));
	CHECK(!find(games, "cached"));
	CHECK(!find(games, ".hidden"));
	CHECK(!find(games, "[BIOS] PIF (Japan, USA)"));
	CHECK(find(games, "Delta") && find(games, "Delta")->tab == psgopher::TabNetwork);
	CHECK(find(games, "shared") && find(games, "shared")->tab == psgopher::TabNetwork);
	/* each file once, though /data/psgopher/roms is inside /data */
	int alphas = 0;
	for (const auto &g : games)
		alphas += g.name == "Alpha (U)";
	CHECK(alphas == 1);
	/* sorted by name, whatever the case */
	for (size_t i = 1; i < games.size(); i++) {
		std::string a = games[i - 1].name, b = games[i].name;
		for (auto &c : a)
			c = (char)std::tolower((unsigned char)c);
		for (auto &c : b)
			c = (char)std::tolower((unsigned char)c);
		CHECK(a <= b);
	}
	CHECK(g_open_files == 0);

	/* the headers: read in each byte order, Gopher64 asked for its facts */
	const auto *alpha = find(games, "Alpha (U)"), *beta = find(games, "beta"), *gamma = find(games, "gamma");
	CHECK(alpha && alpha->facts && alpha->id == "NCKE" && alpha->header == "COMET KART" && alpha->cic == 6102 && alpha->save == 1);
	CHECK(beta && beta->facts && beta->id == "NMTP" && beta->header == "MOSS TEMPLE" && beta->cic == 6105 && beta->save == 32);
	CHECK(gamma && gamma->facts && gamma->id == "NHNJ" && gamma->header == "HARBOR NIGHTS");
	CHECK(find(games, "packed") && !find(games, "packed")->facts); /* a .zip: none */
	CHECK(find(games, "Delta") && !find(games, "Delta")->facts);   /* not a ROM inside */
	CHECK(g_facts_calls == 3);
	/* the revision byte is kept (a game's own settings are by the code, destination and revision) */
	CHECK(alpha->revision == 0 && beta->revision == 2 && gamma->revision == 11);
	{
		bool from_header = false;
		CHECK(psgopher::gameKey(*alpha, &from_header) == "NCKE-00" && from_header);
		CHECK(psgopher::gameKey(*beta, &from_header) == "NMTP-02" && from_header);
		CHECK(psgopher::gameKey(*gamma, &from_header) == "NHNJ-0B" && from_header);
		/* no header facts (a .zip): the file's name, and it says so */
		CHECK(psgopher::gameKey(*find(games, "packed"), &from_header) == "file-packed" && !from_header);
		psgopher::Game odd;
		odd.name = ".a:b/c*d";
		CHECK(psgopher::gameKey(odd) == "file-_a_b_c_d");
	}
	CHECK(psgopher::regionText("NCKE") == "USA" && psgopher::regionText("NHNJ") == "Japan" && psgopher::regionText("NAAP") == "Europe");
	CHECK(psgopher::saveText(1 | 4) == "EEPROM 4K or SRAM" && psgopher::saveText(32) == "Flash" && psgopher::saveText(0) == "None");
	CHECK(psgopher::cicText(6102) == "CIC-6102" && psgopher::cicText(0) == "Unknown");
	CHECK(psgopher::saveName(" Comet: Kart!  ") == "Comet Kart");

	/* kept, and read back at the next start */
	lib.load();
	unsigned version2 = 0;
	const auto again = lib.games(&version2);
	CHECK(version2 != version);
	CHECK(again.size() == games.size());
	CHECK(find(again, "Delta") && find(again, "Delta")->tab == psgopher::TabNetwork && find(again, "Delta")->size == 0);
	CHECK(find(again, "beta") && find(again, "beta")->facts && find(again, "beta")->header == "MOSS TEMPLE" &&
		find(again, "beta")->cic == 6105 && find(again, "beta")->save == 32 && find(again, "beta")->id == "NMTP");
	CHECK(find(again, "beta")->revision == 2 && find(again, "Alpha (U)")->revision == 0 && find(again, "gamma")->revision == 11);
	/* a list of the version before (no revision): read back with the revision unknown, and a game's key reads the header */
	{
		const std::string tsv = data + "/psgopher/cache/library.tsv", kept = slurp(tsv);
		write(tsv, "#psgopher-library 3\n0\t4096\t1\t6102\t1\t0\tNCKE\tCOMET KART\t11223344-55667788-C:45\t" + alpha->path + "\n");
		lib.load();
		const auto old = lib.games();
		CHECK(old.size() == 1 && old[0].facts && old[0].revision == -1);
		bool from_header = false;
		CHECK(psgopher::gameKey(old[0], &from_header) == "NCKE-00" && from_header);
		write(tsv, kept);
		lib.load();
		CHECK(find(lib.games(), "beta")->revision == 2);
	}
	/* a second search reads no header it knows */
	lib.rescan();
	for (int i = 0; i < 500 && lib.scanning(); i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	CHECK(g_facts_calls == 3);
	CHECK(find(lib.games(), "beta") && find(lib.games(), "beta")->cic == 6105);
	/* a share that cannot be listed (twice: the retry fails too) keeps the last list's games;
	 * one failure is covered by the retry */
	for (int fails : { 2, 1 }) {
		g_share_fails = fails;
		lib.rescan();
		for (int i = 0; i < 500 && lib.scanning(); i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		CHECK(g_share_fails == 0);
		CHECK(find(lib.games(), "Delta") && find(lib.games(), "Delta")->tab == psgopher::TabNetwork);
		CHECK(find(lib.games(), "shared"));
		CHECK(lib.games().size() == games.size());
		CHECK(g_facts_calls == 3); /* no header read again */
	}

	/* game folders chosen in BENCH: only they (with the roms folder and the shares) are searched,
	 * and a .7z in one is listed whatever its folder's name; with none, the whole search again */
	{
		CHECK(psgopher::gameFolders().empty());
		CHECK(psgopher::setGameFolders({ data + "/games/", data + "/games" }));
		const auto chosen = psgopher::gameFolders();
		CHECK(chosen.size() == 1 && chosen[0] == data + "/games");
		lib.rescan();
		for (int i = 0; i < 500 && lib.scanning(); i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		const auto picked = lib.games();
		CHECK(find(picked, "gamma"));             /* in the chosen folder */
		CHECK(find(picked, "random"));            /* a .7z there, not in an N64 folder */
		CHECK(find(picked, "Alpha (U)"));         /* the title's roms folder */
		CHECK(find(picked, "Delta"));             /* the shares */
		CHECK(!find(picked, "eboot"));
		CHECK(psgopher::setGameFolders({}));
		CHECK(psgopher::gameFolders().empty());
		lib.rescan();
		for (int i = 0; i < 500 && lib.scanning(); i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		CHECK(!find(lib.games(), "random") && lib.games().size() == games.size());
	}

	/* probes: Gopher64's states of the game by its header's name, newest first */
	folders(data + "/psgopher/data/states");
	write(data + "/psgopher/data/states/COMET KART-ABC.state1", "s");
	write(data + "/psgopher/data/states/COMET KART-ABC.state4", "s");
	write(data + "/psgopher/data/states/COMET KART-ABC.eep", "s");
	write(data + "/psgopher/data/states/MOSS TEMPLE-XYZ.state2", "s");
	{
		struct utimbuf old = { time(nullptr) - 7200, time(nullptr) - 7200 };
		utime((data + "/psgopher/data/states/COMET KART-ABC.state1").c_str(), &old);
	}
	const auto probes = psgopher::probesOf(*alpha);
	CHECK(probes.size() == 2 && probes[0].slot == 4 && probes[1].slot == 1);
	CHECK(psgopher::probesOf(*gamma).empty());

	/* play time by name, added up and kept */
	CHECK(psgopher::playedOf("Alpha (U)").seconds == 0 && psgopher::playedOf("Alpha (U)").last == 0);
	psgopher::addPlayed("Alpha (U)", 600);
	psgopher::addPlayed("beta", 30);
	psgopher::addPlayed("Alpha (U)", 1200);
	CHECK(psgopher::playedOf("Alpha (U)").seconds == 1800 && psgopher::playedOf("Alpha (U)").last > 0);
	CHECK(psgopher::playedOf("beta").seconds == 30);

	/* a share's ROM is copied, a local one is given as it is */
	std::string error;
	CHECK(psgopher::localCopy(data + "/games/n64/deep/er/gamma.n64", &error) == data + "/games/n64/deep/er/gamma.n64");
	const std::string copied = psgopher::localCopy("/smb/nas/n64/Delta.z64", &error);
	CHECK(copied == data + "/psgopher/cache/play/Delta.z64");
	CHECK(slurp(copied) == std::string(70000, 'd'));
	const std::string second = psgopher::localCopy("/smb/nas/n64/shared.zip", &error);
	CHECK(slurp(second) == zipNaming("folder/inner.v64"));
	CHECK(access(copied.c_str(), F_OK) != 0); /* one at a time */
	CHECK(psgopher::localCopy("/smb/nas/n64/missing.z64", &error).empty() && !error.empty());
	CHECK(g_open_files == 0);
	copyThroughHelper(data);

	/* the title's settings: defaults written when missing, read back, out-of-range values held in */
	psgopher::TitleSettings t;
	t.file = data + "/psgopher/config/psgopher.ini";
	t.load();
	CHECK(access(t.file.c_str(), F_OK) == 0 && t.display == "60" && !t.hdr && t.hdrWhite == 203 && t.hdrPeak == 1000 && t.audioBuffer == 4 && t.shaderCompile == "1" && !t.driverThread);
	t.display = "vrr";
	t.hdr = true;
	t.hdrWhite = 250;
	t.hdrPeak = 1500;
	t.audioBuffer = 8;
	t.shaderCompile = "4";
	t.driverThread = true;
	t.wave32 = true;
	t.smallTypes = true;
	CHECK(t.rdpLayout == 2);
	t.rdpLayout = 0;
	CHECK(t.keepGpuMemory); /* on by default */
	t.keepGpuMemory = false;
	CHECK(!t.rdpThreaded); /* taken out: always off */
	CHECK(t.save());
	psgopher::TitleSettings u;
	u.file = t.file;
	u.load();
	CHECK(u.display == "vrr" && u.hdr && u.hdrWhite == 250 && u.hdrPeak == 1500 && u.audioBuffer == 8 && u.shaderCompile == "4" && u.driverThread && u.wave32 && u.smallTypes && u.rdpLayout == 0 && !u.keepGpuMemory);
	CHECK(slurp(t.file).find("rdp_threaded") == std::string::npos && !u.isField("rdp_threaded"));
	write(t.file, "rdp_threaded = on\n"); /* a file from the build that had it */
	psgopher::TitleSettings old_threaded;
	old_threaded.file = t.file;
	old_threaded.load();
	old_threaded.applyHdr();
	CHECK(!old_threaded.rdpThreaded && !strcmp(getenv("PSGOPHER_RDP_THREADED"), "0"));
	write(t.file, "shader_compile = now\ndisplay = 50\nhdr = yes\nhdr_white = 9000\nhdr_peak = -5\nrdp_layout = 3\n");
	psgopher::TitleSettings v;
	v.file = t.file;
	v.load();
	CHECK(t.writeHelper && u.writeHelper && v.writeHelper); /* on by default, and after the file's other lines */
	t.writeHelper = false;
	CHECK(t.save());
	psgopher::TitleSettings w;
	w.file = t.file;
	w.load();
	CHECK(!w.writeHelper && !psgopher::helperEnabled() && access(t.file.c_str(), F_OK) == 0 && slurp(t.file).find("write_helper = off") != std::string::npos);
	t.writeHelper = true;
	CHECK(t.save() && psgopher::helperEnabled());
	CHECK(v.display == "60" && !v.hdr && v.hdrWhite == 400 && v.hdrPeak == 0 && v.rdpLayout == 2 && v.shaderCompile == "1" && v.keepGpuMemory);

	/* the picture rows: their defaults, the file's words, values held in range, and the table a game's settings go through */
	{
		psgopher::TitleSettings d;
		CHECK(d.finalFilter == 0 && d.viBilinear == 0 && d.viAa && d.ditherFilter && d.divotFilter && !d.gammaDither && d.nativeTexRect &&
		      d.cropOverscan == 0 && d.downsample == 0 && d.deinterlace == 1);
		d.finalFilter = 2, d.viBilinear = 1, d.viAa = false, d.ditherFilter = false, d.divotFilter = false, d.gammaDither = true;
		d.nativeTexRect = false, d.cropOverscan = 16, d.downsample = 2, d.deinterlace = 0;
		d.file = data + "/psgopher/config/picture.ini";
		CHECK(d.save());
		const std::string text = slurp(d.file);
		CHECK(text.find("final_filter = nearest") != std::string::npos && text.find("vi_bilinear = on") != std::string::npos &&
		      text.find("deinterlace = bob") != std::string::npos && text.find("crop_overscan = 16") != std::string::npos);
		psgopher::TitleSettings e;
		e.file = d.file;
		e.load();
		CHECK(e.finalFilter == 2 && e.viBilinear == 1 && !e.viAa && !e.ditherFilter && !e.divotFilter && e.gammaDither && !e.nativeTexRect &&
		      e.cropOverscan == 16 && e.downsample == 2 && e.deinterlace == 0);
		write(d.file, "final_filter = fancy\ncrop_overscan = 11\ndownsample = 9\nvi_bilinear = maybe\ndeinterlace = weave\n");
		psgopher::TitleSettings f;
		f.file = d.file;
		f.load();
		CHECK(f.finalFilter == 0 && f.cropOverscan == 8 && f.downsample == 3 && f.viBilinear == 0 && f.deinterlace == 1 && f.nativeTexRect);
		/* a file saved by the build whose defaults drew lines (no picture_defaults): the defaults again, once */
		write(d.file, "native_texrect = off\ndeinterlace = bob\n");
		psgopher::TitleSettings m;
		m.file = d.file;
		m.load();
		CHECK(m.nativeTexRect && m.deinterlace == 1 && m.pictureDefaults == 2);
		/* after that, the owner's own choice stays */
		m.nativeTexRect = false, m.deinterlace = 0;
		CHECK(m.save());
		psgopher::TitleSettings k;
		k.file = d.file;
		k.load();
		CHECK(!k.nativeTexRect && k.deinterlace == 0);
		/* the environment the renderer reads once a game */
		e.applyHdr();
		CHECK(!strcmp(getenv("PSGOPHER_FINAL_FILTER"), "nearest") && !strcmp(getenv("PSGOPHER_VI_BILINEAR"), "on") && !strcmp(getenv("PSGOPHER_VI_AA"), "0") &&
		      !strcmp(getenv("PSGOPHER_GAMMA_DITHER"), "1") && !strcmp(getenv("PSGOPHER_NATIVE_TEXRECT"), "0") && !strcmp(getenv("PSGOPHER_CROP_OVERSCAN"), "16") &&
		      !strcmp(getenv("PSGOPHER_DOWNSAMPLE"), "2") && !strcmp(getenv("PSGOPHER_DEINTERLACE"), "bob"));
		psgopher::TitleSettings plain;
		plain.applyHdr();
		CHECK(!strcmp(getenv("PSGOPHER_FINAL_FILTER"), "sharp") && !strcmp(getenv("PSGOPHER_VI_BILINEAR"), "auto") && !strcmp(getenv("PSGOPHER_GAMMA_DITHER"), "0") &&
		      !strcmp(getenv("PSGOPHER_DEINTERLACE"), "blend") && !strcmp(getenv("PSGOPHER_NATIVE_TEXRECT"), "1") && !strcmp(getenv("PSGOPHER_CROP_OVERSCAN"), "0"));
		/* the table: a value in, the same out; a key that is not one */
		CHECK(psgopher::TitleSettings::isField("downsample") && !psgopher::TitleSettings::isField("display") && !psgopher::TitleSettings::isField("hdr"));
		psgopher::TitleSettings g;
		CHECK(g.assign("downsample", 3) && g.value("downsample") == 3 && g.assign("shader_compile", 4) && g.shaderCompile == "4" && g.value("shader_compile") == 4);
		CHECK(!g.assign("display", 1) && g.value("display") == -1);
	}

	/* a game's own settings (game_settings.h): the file by the game's key, applied over the global settings for the game's length and put back */
	{
		using namespace psgopher;
		CHECK(gameSettingEligible("video.upscale") && gameSettingEligible("emulation.rewind") && gameSettingEligible("input.controller_enabled.2") &&
		      gameSettingEligible("input.transfer_pak.0") && gameSettingEligible("input.emulate_vru") && gameSettingEligible("final_filter") &&
		      gameSettingEligible("audio_buffer"));
		CHECK(!gameSettingEligible("display") && !gameSettingEligible("hdr") && !gameSettingEligible("hotkeys") && !gameSettingEligible("video.fullscreen") &&
		      !gameSettingEligible("input.gb_rom_path.0") && !gameSettingEligible("input.controller_enabled.7") && !gameSettingEligible("slow_capture"));
		CHECK(loadGameSettings("NCKE-00").empty());
		GameSettings gs;
		gs.set("video.upscale", 4);
		gs.set("final_filter", 2);
		gs.set("emulation.overclock", 1);
		gs.set("video.upscale", 8); /* (the same row again: replaced) */
		CHECK(gs.values.size() == 3 && *gs.find("video.upscale") == 8);
		CHECK(saveGameSettings("NCKE-00", gs));
		CHECK(access(gameSettingsFile("NCKE-00").c_str(), F_OK) == 0 && gameSettingsFile("NCKE-00") == data + "/psgopher/config/games/NCKE-00.ini");
		write(gameSettingsFile("NCKE-00"), slurp(gameSettingsFile("NCKE-00")) + "display = 1\nbogus = 3\n"); /* (rows that are not a game's: ignored) */
		const GameSettings back = loadGameSettings("NCKE-00");
		CHECK(back.values.size() == 3 && *back.find("video.upscale") == 8 && *back.find("final_filter") == 2 && *back.find("emulation.overclock") == 1 && !back.find("display"));
		CHECK(loadGameSettings("NMTP-02").empty()); /* (another game, another revision: its own file) */

		/* applied: Gopher64's config has the game's rows, the title's copy has its own; the global ones stay */
		TitleSettings global;
		g_log.clear();
		{
			GameScope scope("NCKE-00", true, global);
			CHECK(said("settings: per-game for NCKE-00: " + back.text()));
			CHECK(g_gopher["video.upscale"] == 8 && g_gopher["emulation.overclock"] == 1);
			CHECK(scope.settings().finalFilter == 2 && global.finalFilter == 0);
			CHECK(access((data + "/psgopher/config/games/restore.txt").c_str(), F_OK) == 0);
			/* a game that did not end: the next start puts Gopher64's config back */
			recoverGameSettings();
			CHECK(g_gopher["video.upscale"] == 2 && g_gopher["emulation.overclock"] == 0);
			CHECK(access((data + "/psgopher/config/games/restore.txt").c_str(), F_OK) != 0);
			CHECK(said("settings: a game did not end: its per-game settings are taken out of Gopher64's config again (2)"));
		}
		CHECK(g_gopher["video.upscale"] == 2 && g_gopher["emulation.overclock"] == 0);
		/* put back at the end, and the next game has the global settings */
		g_gopher["video.upscale"] = 4;
		g_log.clear();
		{
			GameScope scope("NCKE-00", true, global);
			CHECK(g_gopher["video.upscale"] == 8);
		}
		CHECK(g_gopher["video.upscale"] == 4 && g_gopher["emulation.overclock"] == 0 && said("settings: the global settings are back"));
		g_log.clear();
		{
			GameScope scope("NMTP-02", true, global);
			CHECK(said("settings: no per-game settings for NMTP-02"));
			CHECK(g_gopher["video.upscale"] == 4 && scope.settings().finalFilter == 0);
		}
		/* a game with no header facts is kept by its file's name, and the log says so */
		{
			GameSettings zipped;
			zipped.set("video.crt", 1);
			CHECK(saveGameSettings("file-packed", zipped));
			g_log.clear();
			GameScope scope("file-packed", false, global);
			CHECK(said("settings: the cartridge's header could not be read: its per-game settings are kept by the file's name (file-packed)"));
			CHECK(said("settings: per-game for file-packed: video.crt=1") && g_gopher["video.crt"] == 1);
		}
		CHECK(g_gopher["video.crt"] == 0);
		/* a row Gopher64 does not have is left out; an empty file is taken away */
		GameSettings odd;
		odd.set("input.controller_enabled.3", 1);
		CHECK(saveGameSettings("NXXX-01", odd));
		{
			g_log.clear();
			GameScope scope("NXXX-01", true, global);
			CHECK(said("settings: Gopher64 has no input.controller_enabled.3: left out"));
		}
		CHECK(saveGameSettings("NXXX-01", GameSettings{}) && access(gameSettingsFile("NXXX-01").c_str(), F_OK) != 0);
	}

	/* which games use the Expansion Pak (expansion_pak.h): the table, Auto's choice, a game's own choice over it, the log */
	{
		using namespace psgopher;
		PakTable t;
		CHECK(t.parse("# a comment\n"
		              "NRQE\trequired\tnintendo,wikipedia\tverified\n"
		              "nenp\tenhanced\twikipedia\tcited\n"
		              "NBAD-01\tincompatible\tpj64\tunverified\n"
		              "NBAD\tenhanced\twikipedia\tcited\n"
		              "01234567-89ABCDEF\tincompatible\tm64p\tunverified\n"
		              "NRQE\tenhanced\tduplicate\tcited\n"      /* (the first of a key stays) */
		              "NXX\trequired\tshort\tx\n"                  /* (not a key) */
		              "NXXE\tsometimes\tsource\tx\n"             /* (not a status) */
		              "NXXF\trequired\n"                            /* (no source) */
		              "\n") == 5 && t.size() == 5);
		CHECK(t.find("NRQE-00", "").status == PakStatus::Required && t.find("NRQE-00", "").source == "nintendo,wikipedia");
		CHECK(t.find("NRQE-7F", "").status == PakStatus::Required); /* (any revision) */
		CHECK(t.find("NENP-02", "").status == PakStatus::Enhanced && t.find("NENP-00", "").source == "wikipedia"); /* (a lower-case key is read as upper) */
		CHECK(t.find("NBAD-01", "").status == PakStatus::Incompatible && t.find("NBAD-00", "").status == PakStatus::Enhanced); /* (the revision's entry first) */
		CHECK(t.find("NZZE-00", "").status == PakStatus::NotListed && t.find("NZZE-00", "").source.empty());
		CHECK(t.find("file-NRQE", "").status == PakStatus::NotListed && t.find("NRQ", "").status == PakStatus::NotListed);
		CHECK(t.find("NZZE-00", "01234567-89ABCDEF-C:45").status == PakStatus::Incompatible); /* (Game::crc: CRC1-CRC2-C:country) */
		CHECK(t.find("NRQE-00", "01234567-89ABCDEF-C:45").status == PakStatus::Incompatible); /* (the CRC before the code) */
		CHECK(t.find("file-x", "01234567-89ABCDEF-C:45").status == PakStatus::Incompatible && t.find("file-x", "01234567-89ABCDEE-C:45").status == PakStatus::NotListed);
		CHECK(!strcmp(pakStatusName(PakStatus::Incompatible), "incompatible") && !strcmp(pakStatusName(PakStatus::NotListed), "not listed") &&
		      !strcmp(pakFactText(PakStatus::Required), "Required") && !strcmp(pakFactText(PakStatus::Enhanced), "Enhanced") &&
		      !strcmp(pakFactText(PakStatus::Incompatible), "Must be off") && !strcmp(pakFactText(PakStatus::NotListed), "Not listed"));
		/* Auto: on for every status but incompatible; On and Off as set */
		for (PakStatus s : { PakStatus::NotListed, PakStatus::Required, PakStatus::Enhanced })
			CHECK(pakIsOn(PakAuto, s) && pakIsOn(PakOn, s) && !pakIsOn(PakOff, s));
		CHECK(!pakIsOn(PakAuto, PakStatus::Incompatible) && pakIsOn(PakOn, PakStatus::Incompatible) && !pakIsOn(PakOff, PakStatus::Incompatible));

		/* the title's file: the row, kept in psgopher.ini */
		const std::string ini = data + "/psgopher/config/pak.ini";
		{
			TitleSettings s;
			s.file = ini;
			CHECK(s.expansionPak == 0);
			s.expansionPak = 2;
			CHECK(s.save());
			TitleSettings r;
			r.file = ini;
			r.load();
			CHECK(r.expansionPak == 2 && r.expansionPakRead && r.value("expansion_pak") == 2 && TitleSettings::isField("expansion_pak"));
			write(ini, "display = 60\n");
			TitleSettings old;
			old.file = ini;
			old.load();
			CHECK(old.expansionPak == 0 && !old.expansionPakRead);
		}

		/* applied at a game's start, over Gopher64's switch for the length of the game */
		write(data + "/pak.tsv", "NRQE\trequired\tnintendo,wikipedia\tverified\nNENP\tenhanced\twikipedia\tcited\nNBAD\tincompatible\tpj64\tunverified\n"
		                         "01234567-89ABCDEF\tincompatible\tm64p\tunverified\n");
		pakUseFile(data + "/pak.tsv");
		CHECK(pakTable().size() == 4 && pakInfoOf("NBAD-00", "").status == PakStatus::Incompatible);
		TitleSettings global; /* Auto */
		auto run = [&](const std::string &key, const TitleSettings &settings, const std::string &crc, int64_t *during) {
			g_log.clear();
			GameScope scope(key, true, settings, crc);
			*during = g_gopher["emulation.disable_expansion_pak"];
		};
		int64_t during = -1;
		run("NRQE-00", global, "", &during);
		CHECK(during == 0 && said("settings: expansion pak on (required; nintendo,wikipedia)") && g_gopher["emulation.disable_expansion_pak"] == 0);
		run("NENP-01", global, "", &during);
		CHECK(during == 0 && said("settings: expansion pak on (enhanced; wikipedia)"));
		run("NZZE-00", global, "", &during);
		CHECK(during == 0 && said("settings: expansion pak on (not listed; -)"));
		run("NBAD-00", global, "", &during);
		CHECK(during == 1 && said("settings: expansion pak off (incompatible; pj64)") && g_gopher["emulation.disable_expansion_pak"] == 0); /* (put back) */
		run("NZZE-00", global, "01234567-89ABCDEF-C:45", &during);
		CHECK(during == 1 && said("settings: expansion pak off (incompatible; m64p)"));
		/* On and Off as the global setting force it */
		TitleSettings forced_on = global, forced_off = global;
		forced_on.expansionPak = PakOn, forced_off.expansionPak = PakOff;
		run("NBAD-00", forced_on, "", &during);
		CHECK(during == 0 && said("settings: expansion pak on (incompatible; pj64)"));
		run("NRQE-00", forced_off, "", &during);
		CHECK(during == 1 && said("settings: expansion pak off (required; nintendo,wikipedia)"));
		/* the game's own choice wins over Auto, and over a global On or Off */
		GameSettings own;
		own.set("expansion_pak", PakOn);
		CHECK(saveGameSettings("NBAD-00", own));
		run("NBAD-00", global, "", &during);
		CHECK(during == 0 && said("settings: per-game for NBAD-00: expansion_pak=1") && said("settings: expansion pak on (incompatible; pj64)"));
		own.set("expansion_pak", PakOff);
		CHECK(saveGameSettings("NZZE-02", own));
		run("NZZE-02", global, "", &during);
		CHECK(during == 1 && said("settings: expansion pak off (not listed; -)") && g_gopher["emulation.disable_expansion_pak"] == 0);
		own.set("expansion_pak", PakAuto);
		CHECK(saveGameSettings("NRQE-00", own));
		run("NRQE-00", forced_off, "", &during);
		CHECK(during == 0 && said("settings: expansion pak on (required; nintendo,wikipedia)")); /* (the game's Auto over a global Off) */
		{
			GameScope scope("NRQE-00", true, forced_off);
			CHECK(scope.settings().expansionPak == PakAuto && forced_off.expansionPak == PakOff);
		}
		/* a game that did not end: the next start puts Gopher64's switch back too */
		{
			g_gopher["emulation.disable_expansion_pak"] = 0;
			GameScope scope("NBAD-02", true, global);
			CHECK(g_gopher["emulation.disable_expansion_pak"] == 1);
			g_log.clear();
			recoverGameSettings();
			CHECK(g_gopher["emulation.disable_expansion_pak"] == 0 && said("settings: a game did not end: its per-game settings are taken out of Gopher64's config again (1)"));
		}
		/* a file from before the row was Auto, On or Off held Gopher64's own switch */
		write(gameSettingsFile("NOLD-00"), "emulation.disable_expansion_pak = 1\nvideo.crt = 1\n");
		const GameSettings legacy = loadGameSettings("NOLD-00");
		CHECK(legacy.find("expansion_pak") && *legacy.find("expansion_pak") == PakOff && !legacy.find("emulation.disable_expansion_pak") && legacy.find("video.crt"));
		write(gameSettingsFile("NOLD-00"), "emulation.disable_expansion_pak = 0\n");
		CHECK(*loadGameSettings("NOLD-00").find("expansion_pak") == PakOn);
		write(gameSettingsFile("NOLD-00"), "emulation.disable_expansion_pak = 1\nexpansion_pak = 0\n");
		CHECK(*loadGameSettings("NOLD-00").find("expansion_pak") == PakAuto);
		CHECK(gameSettingEligible("expansion_pak") && !gameSettingEligible("emulation.disable_expansion_pak"));
	}

	/* the shares file (OGBox's format), written and read back with the accounts encoded */
	psgopher::ShareFile sf;
	psgopher::Share a;
	a.name = "NAS games";
	a.host = "192.168.1.10";
	a.path = "games/n64";
	a.user = "me";
	a.password = "p@ss w:rd";
	psgopher::Share b;
	b.name = "nfs";
	b.nfs = true;
	b.host = "192.168.1.20";
	b.path = "/volume1/n64";
	sf.shares = { a, b };
	const std::string cfg = data + "/psgopher/config/network.cfg";
	CHECK(psgopher::writeShares(cfg, sf, &error));
	psgopher::ShareFile back;
	CHECK(psgopher::readShares(cfg, back, &error));
	CHECK(back.shares.size() == 2);
	if (back.shares.size() == 2) {
		CHECK(back.shares[0].name == "NAS games" && back.shares[0].host == "192.168.1.10" && back.shares[0].path == "games/n64");
		CHECK(back.shares[0].user == "me" && back.shares[0].password == "p@ss w:rd" && !back.shares[0].nfs);
		CHECK(back.shares[1].nfs && back.shares[1].host == "192.168.1.20" && back.shares[1].path == "/volume1/n64");
		CHECK(psgopher::shareAddress(back.shares[1]) == "nfs://192.168.1.20/volume1/n64");
	}
	/* taken: the mounts in use, as /smb/<name> */
	const std::string first = psgopher::shareMountName("NAS games", {});
	CHECK(first == "NAS_games");
	CHECK(psgopher::shareMountName("NAS games", { "/smb/" + first }) != first);
	psgopher::Share m = back.shares[0];
	CHECK(psgopher::mountShare(m, back, {}, &error) && m.mount.rfind("/smb/", 0) == 0);

	if (failures) {
		std::fprintf(stderr, "test_library: %d check(s) failed\n", failures);
		return 1;
	}
	std::printf("test_library: all checks passed\n");
	return 0;
}
