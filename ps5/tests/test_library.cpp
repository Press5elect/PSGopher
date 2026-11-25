/*
 * PSGopher - host tests of the game list's search (library.cpp), the title's settings file
 * (title_settings.cpp) and the shares file (shares.cpp), on the PC. ps5vfs is stood in for here: a
 * path under /smb/ is a folder of the test's own (the share), served through the ps5vfs calls the
 * list makes, so the share's path (listing, the zip check, the copy) is the one tested.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "library.h"
#include "shares.h"
#include "title_settings.h"

#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
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

extern "C" void say(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	std::vfprintf(stdout, format, args);
	va_end(args);
	std::fputc('\n', stdout);
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
extern "C" ps5vfs_dir *ps5vfs_opendir(const char *path)
{
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
static std::string romImage(const char *name, const char *code, const char *order, size_t size = 0x2000)
{
	std::string rom(size, '\0');
	const unsigned char magic[4] = { 0x80, 0x37, 0x12, 0x40 };
	memcpy(&rom[0], magic, 4);
	char title[20];
	memset(title, ' ', sizeof(title));
	memcpy(title, name, strlen(name));
	memcpy(&rom[0x20], title, 20);
	memcpy(&rom[0x3B], code, 4);
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
	write(data + "/psgopher/roms/beta.V64", romImage("MOSS TEMPLE", "NMTP", "v64"));
	write(data + "/psgopher/roms/packed.zip", zipNaming("inside.n64"));
	write(data + "/psgopher/roms/notrom.zip", zipNaming("readme.txt"));
	write(data + "/psgopher/roms/seven.7z", "7z");
	write(data + "/games/n64/deep/er/gamma.n64", romImage("HARBOR NIGHTS", "NHNJ", "n64"));
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
	/* a second search reads no header it knows */
	lib.rescan();
	for (int i = 0; i < 500 && lib.scanning(); i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	CHECK(g_facts_calls == 3);
	CHECK(find(lib.games(), "beta") && find(lib.games(), "beta")->cic == 6105);

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

	/* the title's settings: defaults written when missing, read back, out-of-range values held in */
	psgopher::TitleSettings t;
	t.file = data + "/psgopher/config/psgopher.ini";
	t.load();
	CHECK(access(t.file.c_str(), F_OK) == 0 && t.display == "60" && !t.hdr && t.hdrWhite == 203 && t.hdrPeak == 1000);
	t.display = "vrr";
	t.hdr = true;
	t.hdrWhite = 250;
	t.hdrPeak = 1500;
	CHECK(t.save());
	psgopher::TitleSettings u;
	u.file = t.file;
	u.load();
	CHECK(u.display == "vrr" && u.hdr && u.hdrWhite == 250 && u.hdrPeak == 1500);
	write(t.file, "display = 50\nhdr = yes\nhdr_white = 9000\nhdr_peak = -5\n");
	psgopher::TitleSettings v;
	v.file = t.file;
	v.load();
	CHECK(v.display == "60" && !v.hdr && v.hdrWhite == 400 && v.hdrPeak == 0);

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
