// PSGopher - the updater (update.cpp), on the PC.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attributions "PSGopher by Press5elect" and
// "Crux2 by Press5elect"; see NOTICE.
//
// PSGopher 0.0.1: from Crux2 v1.0.17 (08c3507, ps5/tests/update/update_test.cpp), for PSGopher's updater.
// PSGopher: from OGBox db63d46 (tests/update/update_test.cpp), for PSGopher's updater
// (ps5/host/update.cpp): PSGopher's title folder (art/, licenses/, write-broker.elf), its title ID
// and its releases' names; the ZIPs are written here by a small writer (stored and deflate-stored
// entries) and read by the same `zip` crate code the title links (ps5/tests/update_zip); a
// developer build, the JSON reader and the ZIP functions are tested as well.
//
//   test_update <work folder> [a release ZIP]
//
// A folder of the work folder plays the title folder, with an older version's files and a user's
// files in it. GitHub is played by the test: its releases page is JSON written here, its ZIPs are
// made here, and both are served through the same HttpGet and HttpDownload the title
// gives the updater. Each step is held against what must be on the disk afterwards: the program's
// files replaced, the user's never touched, and after a failure or an interrupted install the
// folder exactly as it was.

#include "update.h"
#include "update_json.h"
#include "update_zip.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace psgopher::update;

static int s_failures = 0;
static int s_checks = 0;

#define CHECK(condition) \
	do \
	{ \
		s_checks++; \
		if (!(condition)) \
		{ \
			s_failures++; \
			std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

static bool Contains(const std::string& text, const char* part)
{
	return text.find(part) != std::string::npos;
}

// ------------------------------------------------------------------------------------- files

static void Run(const std::string& command)
{
	if (std::system(command.c_str()) != 0)
		std::fprintf(stderr, "command failed: %s\n", command.c_str());
}

static void MakeDirs(const std::string& path)
{
	for (size_t at = 1; at <= path.size(); at++)
		if (at == path.size() || path[at] == '/')
			mkdir(path.substr(0, at).c_str(), 0777);
}

static void Put(const std::string& path, const std::string& data)
{
	const size_t slash = path.rfind('/');
	if (slash != std::string::npos)
		MakeDirs(path.substr(0, slash));
	FILE* file = std::fopen(path.c_str(), "wb");
	if (!file)
	{
		std::fprintf(stderr, "cannot write %s\n", path.c_str());
		std::exit(2);
	}
	std::fwrite(data.data(), 1, data.size(), file);
	std::fclose(file);
}

static std::string Get(const std::string& path)
{
	std::string data;
	FILE* file = std::fopen(path.c_str(), "rb");
	if (!file)
		return "<missing>";
	char buffer[65536];
	size_t got;
	while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
		data.append(buffer, got);
	std::fclose(file);
	return data;
}

static bool Exists(const std::string& path)
{
	struct stat st;
	return lstat(path.c_str(), &st) == 0;
}

// Every file and folder below `dir`, with each file's bytes ("/" for a folder). .update/ left out
// when asked.
static void Walk(const std::string& dir, const std::string& relative, std::map<std::string, std::string>& out,
	bool skip_update)
{
	DIR* list = opendir((dir + relative).c_str());
	if (!list)
		return;
	std::vector<std::string> names;
	while (const dirent* entry = readdir(list))
		if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0)
			names.push_back(entry->d_name);
	closedir(list);
	for (const std::string& name : names)
	{
		const std::string path = relative + name;
		if (skip_update && path == ".update")
			continue;
		struct stat st;
		if (lstat((dir + path).c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
		{
			out[path + "/"] = "/";
			Walk(dir, path + "/", out, skip_update);
		}
		else
			out[path] = Get(dir + path);
	}
}

static std::map<std::string, std::string> Snapshot(const std::string& dir, bool skip_update = false)
{
	std::map<std::string, std::string> out;
	Walk(dir, "", out, skip_update);
	return out;
}

static std::string Filler(const std::string& seed, size_t size)
{
	std::string out;
	unsigned value = 2166136261u;
	for (char c : seed)
		value = (value ^ static_cast<unsigned char>(c)) * 16777619u;
	while (out.size() < size)
	{
		value = value * 1103515245u + 12345u;
		out += static_cast<char>('a' + (value >> 16) % 26);
	}
	return out;
}

// ---------------------------------------------------------------------------------- the ZIPs

struct Item
{
	std::string name;
	std::string data;
	bool folder = false;
	uint32_t mode = 0100777; // what make-release-zip.py writes; 0: no Unix mode at all
};

static uint32_t Crc32(const std::string& data)
{
	static uint32_t table[256];
	if (table[1] == 0)
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	uint32_t crc = 0xffffffffu;
	for (const unsigned char c : data)
		crc = table[(crc ^ c) & 0xff] ^ (crc >> 8);
	return crc ^ 0xffffffffu;
}

static void Put16(std::string& out, uint32_t value)
{
	out += static_cast<char>(value & 0xff);
	out += static_cast<char>((value >> 8) & 0xff);
}

static void Put32(std::string& out, uint32_t value)
{
	Put16(out, value & 0xffff);
	Put16(out, value >> 16);
}

// A deflate stream that only stores: blocks of at most 65535 bytes, so that the entry is method 8
// (as the release's real entries are) without a compressor in the test.
static std::string DeflateStored(const std::string& data)
{
	std::string out;
	size_t at = 0;
	do
	{
		const size_t take = std::min<size_t>(65535, data.size() - at);
		const bool last = at + take == data.size();
		out += static_cast<char>(last ? 1 : 0);
		Put16(out, static_cast<uint32_t>(take));
		Put16(out, static_cast<uint32_t>(~take) & 0xffff);
		out.append(data, at, take);
		at += take;
	} while (at < data.size());
	return out;
}

// A ZIP of these entries, written here: stored entries, and the larger ones as method 8 (a
// deflate stream of stored blocks). Names that would not be written as they are (".." and the
// like) are given as a stand-in of the same length and patched into the bytes afterwards:
// `patches` maps stand-in to the real name. `encrypted` and `method` mark the entry of that name:
// the flag bit of an encrypted entry, and a compression method nothing here can read.
static std::string MakeZip(const std::string& work, const std::vector<Item>& items,
	const std::vector<std::pair<std::string, std::string>>& patches = {}, const std::string& encrypted = "",
	const std::string& odd_method = "")
{
	(void)work;
	std::string bytes, central;
	for (const Item& item : items)
	{
		std::string name = item.name;
		if (item.folder && (name.empty() || name.back() != '/'))
			name += '/';
		const std::string data = item.folder ? "" : item.data;
		const bool deflate = data.size() >= 4096;
		const std::string packed = deflate ? DeflateStored(data) : data;
		const uint32_t crc = Crc32(data);
		uint32_t method = deflate ? 8 : 0;
		uint32_t flags = 0x800; // UTF-8 names
		if (name == encrypted)
			flags |= 1;
		if (name == odd_method)
			method = 12; // bzip2: not one of the two the updater reads
		const uint32_t offset = static_cast<uint32_t>(bytes.size());
		Put32(bytes, 0x04034b50);
		Put16(bytes, 20);
		Put16(bytes, flags);
		Put16(bytes, method);
		Put32(bytes, 0x58210000); // 2024-01-01 00:00
		Put32(bytes, crc);
		Put32(bytes, static_cast<uint32_t>(packed.size()));
		Put32(bytes, static_cast<uint32_t>(data.size()));
		Put16(bytes, static_cast<uint32_t>(name.size()));
		Put16(bytes, 0);
		bytes += name;
		bytes += packed;
		Put32(central, 0x02014b50);
		Put16(central, (item.mode != 0 ? 3u << 8 : 0u) | 20); // made on Unix, or on DOS
		Put16(central, 20);
		Put16(central, flags);
		Put16(central, method);
		Put32(central, 0x58210000);
		Put32(central, crc);
		Put32(central, static_cast<uint32_t>(packed.size()));
		Put32(central, static_cast<uint32_t>(data.size()));
		Put16(central, static_cast<uint32_t>(name.size()));
		Put16(central, 0);
		Put16(central, 0);
		Put16(central, 0);
		Put16(central, 0);
		Put32(central, item.mode != 0 ? (item.folder ? 040777u : item.mode) << 16 : (item.folder ? 0x10u : 0u));
		Put32(central, offset);
		central += name;
	}
	const uint32_t central_at = static_cast<uint32_t>(bytes.size());
	bytes += central;
	Put32(bytes, 0x06054b50);
	Put16(bytes, 0);
	Put16(bytes, 0);
	Put16(bytes, static_cast<uint32_t>(items.size()));
	Put16(bytes, static_cast<uint32_t>(items.size()));
	Put32(bytes, static_cast<uint32_t>(central.size()));
	Put32(bytes, central_at);
	Put16(bytes, 0);
	for (const auto& [from, to] : patches)
	{
		if (from.size() != to.size())
			std::exit(3);
		for (size_t at = bytes.find(from); at != std::string::npos; at = bytes.find(from, at + 1))
			bytes.replace(at, from.size(), to);
	}
	return bytes;
}

// The release package of a version, laid out as make-release-zip.py lays it out: everything below
// PPSA99280/, with folder entries. It carries some of the user's places too (bios/README.txt,
// games/README.txt as the real package does, and, to be sure they are passed over, an
// config/psgopher.ini and a psgopher-boot.log, a user's file in PSGopher).
static std::vector<Item> Package(const std::string& version, size_t eboot_size = 300000)
{
	const std::string t = "PPSA99280/";
	std::vector<Item> items;
	const auto folder = [&](const std::string& name) { items.push_back({t + name, "", true}); };
	const auto file = [&](const std::string& name, const std::string& data) { items.push_back({t + name, data}); };
	items.push_back({t, "", true});
	file("NOTICE.txt", "legal " + version);
	file("README.md", "# PSGopher " + version);
	folder("bios/");
	file("bios/README.txt", "put a BIOS here (" + version + ")");
	// A text that compresses well, as an eboot.bin does in part, and a part that does not.
	std::string eboot = "EBOOT " + version + "\n";
	while (eboot.size() < eboot_size / 2)
		eboot += "the same line again and again ";
	eboot += Filler(version, eboot_size - eboot.size());
	file("eboot.bin", eboot);
	folder("games/");
	file("games/README.txt", "put games here (" + version + ")");
	folder("inis/");
	file("config/psgopher.ini", "[a release must never carry this] " + version);
	folder("licenses/");
	file("licenses/GPL-3.0.txt", "GPL " + version);
	folder("art/");
	file("sce_sys/games.db", "games " + version);
	file("psgopher-boot.log", "the release's patches " + version);
	folder("art/fonts/");
	file("art/fonts/Roboto-Bold.ttf", "font " + version);
	folder("art/new-in-" + version + "/");
	file("art/new-in-" + version + "/thing.txt", "new " + version);
	file("sandbox-elevator.elf", "elevator " + version);
	folder("sce_module/");
	file("sce_module/libc.prx", "libc " + version);
	folder("sce_sys/");
	file("sce_sys/icon0.png", "icon " + version);
	file("sce_sys/param.json", "{\"titleId\": \"PPSA99280\", \"contentVersion\": \"" + version + "\"}");
	file("write-broker.elf", "symbols " + version);
	return items;
}

// The title folder of an older version, with a user's files in it.
static void OldFolder(const std::string& app)
{
	Run("rm -rf '" + app + "'");
	MakeDirs(app);
	Put(app + "/eboot.bin", "EBOOT old");
	Put(app + "/NOTICE.txt", "legal old");
	Put(app + "/README.md", "readme old");
	Put(app + "/write-broker.elf", "symbols old");
	Put(app + "/sandbox-elevator.elf", "elevator old");
	Put(app + "/sce_sys/param.json", "{\"titleId\": \"PPSA99280\", \"contentVersion\": \"old\"}");
	Put(app + "/sce_sys/icon0.png", "icon old");
	Put(app + "/sce_module/libc.prx", "libc old");
	Put(app + "/sce_module/extra.prx", "a module the new release does not have");
	Put(app + "/sce_sys/games.db", "games old");
	Put(app + "/art/fonts/Roboto-Bold.ttf", "font old");
	Put(app + "/licenses/GPL-3.0.txt", "GPL old");
	Put(app + "/licenses/old-only.txt", "a notice the new release does not have");
	// The user's.
	Put(app + "/psgopher-boot.log", "the user's patches");
	Put(app + "/config/psgopher.ini", "[UI]\nmine=1\n");
	Put(app + "/memcards/Mcd001.ps2", Filler("card", 4096));
	Put(app + "/bios/scph39001.bin", Filler("bios", 8192));
	Put(app + "/bios/README.txt", "put a BIOS here (old)");
	Put(app + "/sstates/NXXE.0.state", "state");
	Put(app + "/covers/NXXE.png", "cover");
	Put(app + "/network.cfg", "share=smb://192.0.2.10/games\n");
	Put(app + "/startup.wav", "my sound");
	Put(app + "/psgopher-boot.previous.log", "log");
	Put(app + "/games/README.txt", "put games here (old)");
}

// ------------------------------------------------------------------------------ the releases

struct Asset
{
	std::string name, url;
	uint64_t size = 0;
	std::string digest; // "sha256:..." or empty
};

struct Rel
{
	std::string tag, name, body;
	bool draft = false, prerelease = true;
	std::vector<Asset> assets;
};

static std::string Json(const std::string& text)
{
	std::string out = "\"";
	for (const unsigned char c : text)
	{
		if (c == '"' || c == '\\')
			out += '\\', out += static_cast<char>(c);
		else if (c == '\n')
			out += "\\n";
		else if (c < 0x20)
		{
			char buffer[8];
			std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
			out += buffer;
		}
		else
			out += static_cast<char>(c);
	}
	return out + "\"";
}

static std::string ReleasesJson(const std::vector<Rel>& releases)
{
	std::string out = "[";
	for (size_t i = 0; i < releases.size(); i++)
	{
		const Rel& r = releases[i];
		out += i ? ",\n{" : "{";
		out += "\"url\": \"https://api.github.com/x\", \"id\": " + std::to_string(1000 + i) + ", \"tag_name\": " +
		       Json(r.tag) + ", \"name\": " + Json(r.name) + ", \"draft\": " + (r.draft ? "true" : "false") +
		       ", \"prerelease\": " + (r.prerelease ? "true" : "false") +
		       ", \"published_at\": \"2026-10-1" + std::to_string(i % 10) + "T12:00:00Z\", \"author\": {\"login\": \"x\"}, \"assets\": [";
		for (size_t j = 0; j < r.assets.size(); j++)
		{
			const Asset& a = r.assets[j];
			out += j ? ", {" : "{";
			out += "\"name\": " + Json(a.name) + ", \"browser_download_url\": " + Json(a.url) +
			       ", \"size\": " + std::to_string(a.size) + ", \"content_type\": \"application/zip\"";
			if (!a.digest.empty())
				out += ", \"digest\": " + Json(a.digest);
			out += "}";
		}
		out += "], \"body\": " + Json(r.body) + "}";
	}
	return out + "]";
}

// -------------------------------------------------------------------------------- the server

static const std::string API = "https://api.github.com/repos/Press5elect/PSGopher/releases?per_page=20";
static const std::string DOWNLOADS = "https://github.com/Press5elect/PSGopher/releases/download/";

struct Server
{
	std::mutex mutex;
	std::map<std::string, std::pair<int, std::string>> pages; // url -> code, body
	std::vector<std::string> asked;
	std::atomic<int> delay_ms{0};
	size_t chunk = 65536;

	void Clear()
	{
		std::lock_guard<std::mutex> lock(mutex);
		pages.clear();
		asked.clear();
		delay_ms = 0;
	}
	void Serve(const std::string& url, const std::string& body, int code = 200)
	{
		std::lock_guard<std::mutex> lock(mutex);
		pages[url] = {code, body};
	}
	bool Find(const std::string& url, int& code, std::string& body)
	{
		std::lock_guard<std::mutex> lock(mutex);
		asked.push_back(url);
		const auto found = pages.find(url);
		if (found == pages.end())
		{
			code = 404;
			body = "{\"message\": \"Not Found\"}";
			return false;
		}
		code = found->second.first;
		body = found->second.second;
		return true;
	}
	bool Asked(const std::string& url)
	{
		std::lock_guard<std::mutex> lock(mutex);
		for (const std::string& one : asked)
			if (one == url)
				return true;
		return false;
	}
};
static Server s_server;

static std::mutex s_log_mutex;
static std::vector<std::string> s_log;

static Setup MakeSetup(const std::string& app, const std::string& version)
{
	Setup setup;
	setup.app_dir = app;
	setup.version = version;
	setup.http_get = [](const std::string& url, std::vector<uint8_t>& out, unsigned) {
		int code;
		std::string body;
		s_server.Find(url, code, body);
		if (code < 0)
			return code;
		out.assign(body.begin(), body.end());
		return code;
	};
	setup.http_download = [](const std::string& url, const std::string& path, uint64_t limit,
							  const std::function<bool(uint64_t, uint64_t)>& progress) {
		int code;
		std::string body;
		s_server.Find(url, code, body);
		if (code != 200)
			return code;
		if (body.size() > limit)
			return -1;
		const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0)
			return -1;
		size_t done = 0;
		while (done < body.size())
		{
			const size_t take = std::min(s_server.chunk, body.size() - done);
			if (write(fd, body.data() + done, take) != static_cast<ssize_t>(take))
			{
				close(fd);
				return -1;
			}
			done += take;
			if (s_server.delay_ms > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(s_server.delay_ms.load()));
			if (!progress(done, body.size()))
			{
				close(fd);
				return -1;
			}
		}
		close(fd);
		return 200;
	};
	setup.log = [](const char* line) {
		std::lock_guard<std::mutex> lock(s_log_mutex);
		s_log.push_back(line);
	};
	return setup;
}

static Status Wait()
{
	for (int i = 0; i < 20000; i++)
	{
		const Status status = GetStatus();
		if (status.state != State::Checking && status.state != State::Downloading && status.state != State::Verifying)
			return status;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::fprintf(stderr, "the updater did not finish in time\n");
	std::exit(2);
}

// A release of `version` served with its ZIP (whose bytes are `zip`), and, as asked, a
// SHA256SUMS, a .sha256 file and GitHub's digest; their checksums can be made wrong.
struct Serving
{
	bool sums = true, single = false, digest = true;
	std::string sums_hash, single_hash, digest_hash; // empty: the right one
	bool zip_asset = true;
	int64_t size = -1; // -1: the right one
};

static Rel ServeRelease(const std::string& version, const std::string& zip, const Serving& how = {})
{
	const std::string tag = "v" + version;
	const std::string name = "PSGopher-" + tag + ".zip";
	const std::string hash = Sha256Hex(zip.data(), zip.size());
	Rel rel;
	rel.tag = tag;
	rel.name = "PSGopher " + tag;
	rel.body = "## What's new in " + tag + "\n\n- Faster in " + tag;
	if (how.zip_asset)
	{
		Asset asset{name, DOWNLOADS + tag + "/" + name, how.size < 0 ? zip.size() : static_cast<uint64_t>(how.size), ""};
		if (how.digest)
			asset.digest = "sha256:" + (how.digest_hash.empty() ? hash : how.digest_hash);
		rel.assets.push_back(asset);
		s_server.Serve(asset.url, zip);
	}
	rel.assets.push_back({"PSGopher-" + tag + "-source.zip", DOWNLOADS + tag + "/PSGopher-" + tag + "-source.zip", 10, ""});
	if (how.sums)
	{
		const std::string url = DOWNLOADS + tag + "/SHA256SUMS";
		rel.assets.push_back({"SHA256SUMS", url, 100, ""});
		s_server.Serve(url, (how.sums_hash.empty() ? hash : how.sums_hash) + "  " + name + "\n" +
								std::string(64, 'c') + "  PSGopher-" + tag + "-source.zip\n");
	}
	if (how.single)
	{
		const std::string url = DOWNLOADS + tag + "/" + name + ".sha256";
		rel.assets.push_back({name + ".sha256", url, 80, ""});
		s_server.Serve(url, (how.single_hash.empty() ? hash : how.single_hash) + "  " + name + "\n");
	}
	return rel;
}

// From a fresh start up to Ready (or whatever came of it): an old folder, one release, Check(),
// Download().
static Status ToReady(const std::string& work, const std::string& app, const std::string& zip,
	const Serving& how = {}, const std::string& running = "1.0.0")
{
	s_server.Clear();
	OldFolder(app);
	const Rel rel = ServeRelease("1.2.0", zip, how);
	s_server.Serve(API, ReleasesJson({rel}));
	Init(MakeSetup(app, running));
	Check();
	const Status checked = Wait();
	if (checked.state != State::Available)
		return checked;
	Download();
	(void)work;
	return Wait();
}

// Whether the title folder is just as `before` and .update/ is gone.
static bool Unchanged(const std::string& app, const std::map<std::string, std::string>& before)
{
	const auto now = Snapshot(app);
	if (now != before)
	{
		for (const auto& [path, data] : now)
			if (!before.count(path) || before.at(path) != data)
				std::fprintf(stderr, "  changed or new: %s\n", path.c_str());
		for (const auto& [path, data] : before)
			if (!now.count(path))
				std::fprintf(stderr, "  missing: %s\n", path.c_str());
		return false;
	}
	return true;
}

// ------------------------------------------------------------------------------------- tests

static void TestHelpers()
{
	std::fprintf(stderr, "versions, names, text, SHA-256\n");
	Version v = ParseVersion("1.0.0");
	CHECK(v.valid && v.major == 1 && v.minor == 0 && v.patch == 0);
	v = ParseVersion("v1.2.3");
	CHECK(v.valid && v.major == 1 && v.minor == 2 && v.patch == 3);
	v = ParseVersion("  V10.20.30-test");
	CHECK(v.valid && v.major == 10 && v.minor == 20 && v.patch == 30);
	v = ParseVersion("1.2");
	CHECK(v.valid && v.major == 1 && v.minor == 2 && v.patch == 0);
	CHECK(ToString(ParseVersion("v0.9.0-rc1")) == "0.9.0");
	CHECK(!ParseVersion("").valid);
	CHECK(!ParseVersion("abc").valid);
	CHECK(!ParseVersion("7").valid);
	CHECK(!ParseVersion("vv1.0.0").valid);
	CHECK(!ParseVersion("1234567890.0.0").valid);
	CHECK(CompareVersions(ParseVersion("1.0.0"), ParseVersion("1.0.1")) < 0);
	CHECK(CompareVersions(ParseVersion("1.0.1"), ParseVersion("1.1.0")) < 0);
	CHECK(CompareVersions(ParseVersion("1.1.0"), ParseVersion("2.0.0")) < 0);
	CHECK(CompareVersions(ParseVersion("1.10.0"), ParseVersion("1.9.9")) > 0);
	CHECK(CompareVersions(ParseVersion("v1.0.0"), ParseVersion("1.0.0-test")) == 0);
	CHECK(CompareVersions(ParseVersion("1.0"), ParseVersion("1.0.0")) == 0);
	CHECK(CompareVersions(ParseVersion("x"), ParseVersion("0.0.1")) < 0);
	CHECK(CompareVersions(ParseVersion("x"), ParseVersion("y")) == 0);
	CHECK(ToString(VersionIn("PSGopher-v1.0.0.zip")) == "1.0.0");
	CHECK(ToString(VersionIn("PSGopher-v1.2.3-part1of3.zip")) == "1.2.3");
	CHECK(ToString(VersionIn("v0.9.0")) == "0.9.0");
	CHECK(ToString(VersionIn("PSGopher v2.10.4 (test)")) == "2.10.4");
	CHECK(ToString(VersionIn("release-1.4.0")) == "1.4.0");
	CHECK(!VersionIn("PSGopher").valid);
	CHECK(!VersionIn("PSGopher-source.zip").valid);
	CHECK(!VersionIn("PPSA99280").valid);

	CHECK(IsManaged("eboot.bin"));
	CHECK(IsManaged("art/logo.png") && IsManaged("art/board.png"));
	CHECK(IsManaged("licenses/OFL-JetBrainsMono.txt") && IsManaged("NOTICE.txt") && IsManaged("write-broker.elf"));
	CHECK(IsManaged("CHANGELOG.md") && IsManaged("README.md"));
	// What is the user's, in the title folder or beside it.
	CHECK(!IsManaged("psgopher-boot.log") && !IsManaged("psgopher-boot.previous.log") && !IsManaged("probe.txt"));
	CHECK(!IsManaged("config/psgopher.ini") && !IsManaged("roms/game.z64") && !IsManaged("data/saves/x.sra"));
	CHECK(!IsManaged("cache/radv/x") && !IsManaged("states/1.state") && !IsManaged("covers/NXXE.png"));
	CHECK(IsManaged("sce_sys/param.json"));
	CHECK(IsManaged("sce_module/libc.prx"));
	CHECK(IsManaged("art/fonts/Roboto-Bold.ttf"));
	CHECK(IsManaged("sce_sys/games.db"));
	CHECK(IsManaged("licenses/GPL-3.0.txt"));
	CHECK(IsManaged("NOTICE.txt"));
	CHECK(IsManaged("README.md"));
	CHECK(IsManaged("write-broker.elf"));
	CHECK(IsManaged("sandbox-elevator.elf"));
	CHECK(!IsManaged("psgopher-boot.log"));
	CHECK(!IsManaged("PSGopher-Boot.LOG"));
	CHECK(!IsManaged("config/psgopher.ini"));
	CHECK(!IsManaged("memcards/Mcd001.ps2"));
	CHECK(!IsManaged("bios/README.txt"));
	CHECK(!IsManaged("bios/scph39001.bin"));
	CHECK(!IsManaged("games/README.txt"));
	CHECK(!IsManaged("network.cfg"));
	CHECK(!IsManaged("startup.wav"));
	CHECK(!IsManaged("background.png"));
	CHECK(!IsManaged("psgopher-boot.previous.log"));
	CHECK(!IsManaged("radv-shader-cache/x"));
	CHECK(!IsManaged(".update/stage/eboot.bin"));
	CHECK(!IsManaged("sce_sys"));
	CHECK(!IsManaged("sce_sys/"));
	CHECK(!IsManaged("art/../config/psgopher.ini"));
	CHECK(!IsManaged("/eboot.bin"));
	CHECK(!IsManaged("assets\\x"));
	CHECK(!IsManaged("EBOOT.BIN"));
	CHECK(!IsManaged("something-new.txt"));

	const std::string plain = PlainText(
		"## What's new\n\n> [!NOTE]\n> Read **this** first.\n\n* A `fast` path, see [the notes](https://example.org/n).\n"
		"![picture](https://example.org/p.png)\n\n\n\n### Fixed\n---\n- Item [one]\n```\ncode **kept**\n```\n");
	CHECK(plain == "What's new\n\nRead this first.\n\n- A fast path, see the notes.\npicture\n\nFixed\n\n- Item [one]\n"
	               "code **kept**");
	if (plain != "What's new\n\nRead this first.\n\n- A fast path, see the notes.\npicture\n\nFixed\n\n- Item [one]\ncode **kept**")
		std::fprintf(stderr, "  plain text was:\n%s\n", plain.c_str());

	CHECK(Sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	const char* long_one = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	CHECK(Sha256Hex(long_one, std::strlen(long_one)) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	const std::string million(1000000, 'a');
	CHECK(Sha256Hex(million.data(), million.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void TestCheck(const std::string& work, const std::string& app)
{
	std::fprintf(stderr, "asking for releases\n");
	OldFolder(app);
	const std::string zip12 = MakeZip(work, Package("1.2.0"));
	const std::string zip11 = MakeZip(work, Package("1.1.0"));

	const auto releases = [&](bool shuffled) {
		s_server.Clear();
		Rel draft = ServeRelease("2.0.0", zip12);
		draft.draft = true;
		draft.body = "Draft notes nobody may see";
		Rel r12 = ServeRelease("1.2.0", zip12);
		r12.body = "# PSGopher v1.2.0\n\n> [!NOTE]\n> Pre-release.\n\n- **Faster** shaders, see [the README](https://example.org)\n- `eboot.bin` smaller";
		Rel r11 = ServeRelease("1.1.0", zip11);
		r11.name = "Second";
		r11.body = "### Fixed\n* Cards";
		Rel r10;
		r10.tag = "v1.0.0";
		r10.name = "PSGopher v1.0.0";
		r10.body = "first notes";
		Rel r09;
		r09.tag = "v0.9.0";
		r09.name = "";
		r09.body = "older notes";
		Rel junk;
		junk.tag = "nightly";
		junk.name = "a release with no version";
		junk.body = "junk";
		std::vector<Rel> list = {draft, r12, r11, r10, r09, junk};
		if (shuffled)
			list = {r10, r11, junk, draft, r09, r12};
		s_server.Serve(API, ReleasesJson(list));
	};

	for (bool shuffled : {false, true})
	{
		releases(shuffled);
		Init(MakeSetup(app, "1.0.0"));
		Check();
		const Status status = Wait();
		CHECK(status.state == State::Available);
		CHECK(status.version == "1.2.0");
		CHECK(status.tag == "v1.2.0");
		CHECK(status.name == "PSGopher v1.2.0");
		CHECK(status.newer.size() == 2);
		if (status.newer.size() == 2)
		{
			CHECK(status.newer[0].version == "1.2.0" && status.newer[1].version == "1.1.0");
			CHECK(status.newer[0].prerelease);
			CHECK(status.newer[1].name == "Second");
			CHECK(status.newer[1].text == "Fixed\n- Cards");
			CHECK(status.newer[0].date.size() == 10 && status.newer[0].date.compare(0, 8, "2026-10-") == 0);
		}
		CHECK(Contains(status.notes, "Faster shaders, see the README"));
		CHECK(Contains(status.notes, "eboot.bin smaller"));
		CHECK(Contains(status.notes, "Pre-release."));
		CHECK(Contains(status.notes, "v1.1.0 - Second"));
		CHECK(status.notes.find("PSGopher v1.2.0") < status.notes.find("v1.1.0 - Second"));
		CHECK(!Contains(status.notes, "**") && !Contains(status.notes, "`") && !Contains(status.notes, "]("));
		CHECK(!Contains(status.notes, "[!NOTE]") && !Contains(status.notes, "# "));
		CHECK(!Contains(status.notes, "first notes") && !Contains(status.notes, "older notes"));
		CHECK(!Contains(status.notes, "Draft") && !Contains(status.notes, "junk"));
		CHECK(!status.reinstall);
		CHECK(status.error.empty());
	}
	// What was asked: the one releases page, and nothing that says who is asking.
	{
		std::lock_guard<std::mutex> lock(s_server.mutex);
		CHECK(s_server.asked.size() == 1 && s_server.asked[0] == API);
	}

	// Up to date, and newer than the newest.
	for (const char* running : {"1.2.0", "v1.2.0", "1.3.0-test"})
	{
		releases(false);
		Init(MakeSetup(app, running));
		Check();
		const Status status = Wait();
		CHECK(status.state == State::UpToDate);
		CHECK(status.version == "1.2.0");
		CHECK(status.reinstall);
		CHECK(status.newer.empty() && status.notes.empty());
	}
	// A developer build (no version, or one that cannot be read) is never offered an update over
	// itself: the newest release is named, nothing is offered, nothing is downloaded.
	for (const char* running : {"", "unknown"})
	{
		releases(false);
		Init(MakeSetup(app, running));
		Check();
		const Status status = Wait();
		CHECK(status.state == State::UpToDate && status.developer_build);
		CHECK(status.version == "1.2.0" && !status.reinstall);
		CHECK(status.newer.empty() && status.notes.empty());
		Download();
		Reinstall();
		CHECK(GetStatus().state == State::UpToDate);
		CHECK(!Exists(app + "/.update"));
	}
	// ... unless the set-up asks (offer_developer): then every release is newer.
	releases(false);
	{
		Setup setup = MakeSetup(app, "");
		setup.offer_developer = true;
		Init(setup);
	}
	Check();
	{
		const Status status = Wait();
		CHECK(status.state == State::Available && status.newer.size() == 4 && status.developer_build);
		CHECK(Contains(status.notes, "older notes") && Contains(status.notes, "v0.9.0"));
	}
	// An older one in between: only those newer than it.
	releases(false);
	Init(MakeSetup(app, "1.1.0"));
	Check();
	{
		const Status status = Wait();
		CHECK(status.state == State::Available && status.newer.size() == 1);
	}

	// The newest has no ZIP (yet).
	s_server.Clear();
	{
		Rel r13 = ServeRelease("1.3.0", zip12, Serving{true, false, true, "", "", "", false});
		s_server.Serve(API, ReleasesJson({r13, ServeRelease("1.2.0", zip12)}));
	}
	Init(MakeSetup(app, "1.0.0"));
	Check();
	{
		const Status status = Wait();
		CHECK(status.state == State::NoRelease);
		CHECK(Contains(status.error, "PSGopher-v1.3.0.zip"));
		CHECK(status.version == "1.3.0");
		Download(); // nothing to download: nothing starts
		CHECK(GetStatus().state == State::NoRelease);
	}
	// Up to date with a newest release that has no ZIP: nothing to install again.
	Init(MakeSetup(app, "1.3.0"));
	Check();
	{
		const Status status = Wait();
		CHECK(status.state == State::UpToDate && !status.reinstall);
		Reinstall();
		CHECK(GetStatus().state == State::UpToDate);
	}

	// Answers that are not a list of releases.
	const struct
	{
		int code;
		const char* body;
		State state;
		const char* says;
	} answers[] = {
		{200, "[]", State::NoRelease, "no release"},
		{200, "[{\"tag_name\": \"nightly\", \"draft\": false}]", State::NoRelease, "no release"},
		{404, "{}", State::NoRelease, "not found"},
		{403, "{\"message\": \"API rate limit exceeded\"}", State::Failed, "Try again later"},
		{500, "", State::Failed, "500"},
		{-1, "", State::Failed, "online"},
		{200, "[{\"tag_name\": \"v1.2.0\", ", State::Failed, "could not be read"},
		{200, "{\"tag_name\": \"v1.2.0\"}", State::Failed, "could not be read"},
		{200, "not json at all", State::Failed, "could not be read"},
	};
	for (const auto& answer : answers)
	{
		s_server.Clear();
		s_server.Serve(API, answer.body, answer.code);
		Init(MakeSetup(app, "1.0.0"));
		Check();
		const Status status = Wait();
		CHECK(status.state == answer.state);
		CHECK(Contains(status.error, answer.says));
		if (!Contains(status.error, answer.says))
			std::fprintf(stderr, "  said: %s\n", status.error.c_str());
	}
	// A deeply nested answer is refused, not recursed into (update_json.h stops at 64 levels).
	s_server.Clear();
	s_server.Serve(API, std::string(100000, '[') + std::string(100000, ']'));
	Init(MakeSetup(app, "1.0.0"));
	Check();
	{
		const Status status = Wait();
		CHECK(status.state == State::Failed && Contains(status.error, "could not be read"));
	}
	// A repository that is no "owner/name".
	for (const char* repository : {"evil/../x", "", "noslash", "a/b/c", "a/b?c"})
	{
		Setup setup = MakeSetup(app, "1.0.0");
		setup.repository = repository;
		s_server.Clear();
		Init(setup);
		Check();
		CHECK(Wait().state == State::Failed);
		std::lock_guard<std::mutex> lock(s_server.mutex);
		CHECK(s_server.asked.empty());
	}
	// Not initialised for a moment: Check() before Init() of another set-up does no harm.
	Init(MakeSetup(app, "1.0.0"));
	CHECK(GetStatus().state == State::Idle);
}

static void TestInstall(const std::string& work, const std::string& app)
{
	std::fprintf(stderr, "download, check, unpack, install\n");
	const std::string zip = MakeZip(work, Package("1.2.0"));
	const Status ready = ToReady(work, app, zip);
	CHECK(ready.state == State::Ready);
	CHECK(ready.done == zip.size() && ready.total == zip.size());
	const auto before = Snapshot(app, true);
	// Unpacked beside the title: the program's files only, eboot.bin last in the plan.
	CHECK(Get(app + "/.update/stage/eboot.bin").compare(0, 12, "EBOOT 1.2.0\n") == 0);
	CHECK(Exists(app + "/.update/stage/sce_sys/param.json"));
	CHECK(Exists(app + "/.update/stage/art/new-in-1.2.0/thing.txt"));
	CHECK(!Exists(app + "/.update/stage/inis"));
	CHECK(!Exists(app + "/.update/stage/bios"));
	CHECK(!Exists(app + "/.update/stage/games"));
	CHECK(!Exists(app + "/.update/stage/psgopher-boot.log"));
	{
		const std::string plan = Get(app + "/.update/plan");
		CHECK(plan.size() > 10 && plan.compare(plan.size() - 10, 10, "eboot.bin\n") == 0);
		CHECK(!Contains(plan, "inis/") && !Contains(plan, "psgopher-boot.log") && !Contains(plan, "bios/"));
	}
	struct stat st;
	CHECK(stat((app + "/.update/stage/eboot.bin").c_str(), &st) == 0 && (st.st_mode & 0777) == 0777);
	// Nothing of the title changed yet.
	OldFolder(work + "/reference");
	CHECK(Snapshot(work + "/reference") == before);

	// Waiting: a check now would only lose it.
	Check();
	CHECK(GetStatus().state == State::Ready);
	// Recover() must not take away what is waiting.
	CHECK(!Recover(app));
	CHECK(Exists(app + "/.update/plan"));

	CHECK(Install());
	const Status installed = GetStatus();
	CHECK(installed.state == State::Installed);
	CHECK(!Exists(app + "/.update"));
	// The program's files are the release's.
	for (const Item& item : Package("1.2.0"))
	{
		if (item.folder)
			continue;
		const std::string path = item.name.substr(10);
		if (IsManaged(path))
		{
			CHECK(Get(app + "/" + path) == item.data);
			CHECK(stat((app + "/" + path).c_str(), &st) == 0 && (st.st_mode & 0777) == 0777);
		}
	}
	// The user's are as they were, also those the release carries a file of.
	CHECK(Get(app + "/config/psgopher.ini") == "[UI]\nmine=1\n");
	CHECK(Get(app + "/memcards/Mcd001.ps2") == Filler("card", 4096));
	CHECK(Get(app + "/bios/scph39001.bin") == Filler("bios", 8192));
	CHECK(Get(app + "/bios/README.txt") == "put a BIOS here (old)");
	CHECK(Get(app + "/games/README.txt") == "put games here (old)");
	CHECK(Get(app + "/psgopher-boot.log") == "the user's patches");
	CHECK(Get(app + "/network.cfg") == "share=smb://192.0.2.10/games\n");
	CHECK(Get(app + "/startup.wav") == "my sound");
	CHECK(Get(app + "/psgopher-boot.previous.log") == "log");
	CHECK(Get(app + "/sstates/NXXE.0.state") == "state");
	CHECK(Get(app + "/covers/NXXE.png") == "cover");
	// Files the release does not have are left.
	CHECK(Get(app + "/sce_module/extra.prx") == "a module the new release does not have");
	CHECK(Get(app + "/licenses/old-only.txt") == "a notice the new release does not have");
	// Every file of the folder now: the release's managed files, and the rest exactly as before.
	{
		const auto after = Snapshot(app);
		for (const auto& [path, data] : before)
			if (!IsManaged(path) && path.back() != '/')
				CHECK(after.count(path) && after.at(path) == data);
	}
	// Once Installed, nothing more is started.
	Check();
	CHECK(GetStatus().state == State::Installed);
	CHECK(!Install());

	// Install again: the folder is v1.2.0 now, and up to date.
	std::fprintf(stderr, "install again\n");
	{
		const auto installed_files = Snapshot(app);
		Put(app + "/eboot.bin", "damaged");
		s_server.Clear();
		const Rel rel = ServeRelease("1.2.0", zip);
		s_server.Serve(API, ReleasesJson({rel}));
		Init(MakeSetup(app, "1.2.0"));
		Check();
		CHECK(Wait().state == State::UpToDate);
		CHECK(GetStatus().reinstall);
		Reinstall();
		const Status status = Wait();
		CHECK(status.state == State::Ready);
		CHECK(status.version == "1.2.0");
		CHECK(Install());
		CHECK(Snapshot(app) == installed_files);
	}

	// A second release on top of it: the folders the first one added stay, the new one is added.
	std::fprintf(stderr, "a release after that\n");
	{
		const std::string zip13 = MakeZip(work, Package("1.3.0"));
		s_server.Clear();
		const Rel rel = ServeRelease("1.3.0", zip13);
		s_server.Serve(API, ReleasesJson({rel}));
		Init(MakeSetup(app, "1.2.0"));
		Check();
		CHECK(Wait().state == State::Available);
		Download();
		CHECK(Wait().state == State::Ready);
		CHECK(Install());
		CHECK(Get(app + "/eboot.bin").compare(0, 12, "EBOOT 1.3.0\n") == 0);
		CHECK(Exists(app + "/art/new-in-1.2.0/thing.txt"));
		CHECK(Exists(app + "/art/new-in-1.3.0/thing.txt"));
		CHECK(Get(app + "/config/psgopher.ini") == "[UI]\nmine=1\n");
	}
}

static void TestChecksums(const std::string& work, const std::string& app)
{
	std::fprintf(stderr, "checksums and sizes\n");
	const std::string zip = MakeZip(work, Package("1.2.0"));
	const std::string wrong = std::string(64, 'a');
	const struct
	{
		Serving how;
		bool ready;
		const char* says;
	} cases[] = {
		{Serving{true, false, true, wrong, "", "", true, -1}, false, "checksum"},       // SHA256SUMS wrong
		{Serving{true, false, true, "", "", wrong, true, -1}, false, "checksum"},       // GitHub's digest wrong
		{Serving{false, false, true, "", "", wrong, true, -1}, false, "checksum"},      // the digest alone, wrong
		{Serving{false, true, false, "", wrong, "", true, -1}, false, "checksum"},      // the .sha256 file wrong
		{Serving{true, true, false, "", wrong, "", true, -1}, false, "differ"},         // two that disagree
		{Serving{true, false, true, "", "", "", true, static_cast<int64_t>(zip.size()) + 1}, false, "bytes"},
		{Serving{false, true, false, "", "", "", true, -1}, true, ""},                  // the .sha256 file alone
		{Serving{true, true, true, "", "", "", true, -1}, true, ""},                    // all three
		{Serving{false, false, false, "", "", "", true, -1}, true, ""},                 // none: the ZIP's own CRCs
	};
	for (const auto& one : cases)
	{
		const Status status = ToReady(work, app, zip, one.how);
		if (one.ready)
		{
			CHECK(status.state == State::Ready);
			continue;
		}
		CHECK(status.state == State::Failed);
		CHECK(Contains(status.error, one.says));
		if (!Contains(status.error, one.says))
			std::fprintf(stderr, "  said: %s\n", status.error.c_str());
		OldFolder(work + "/reference");
		CHECK(Unchanged(app, Snapshot(work + "/reference")));
		CHECK(!Install());
	}
	// SHA256SUMS without a line for the ZIP: it says nothing of it.
	s_server.Clear();
	OldFolder(app);
	{
		Rel rel = ServeRelease("1.2.0", zip, Serving{false, false, false, "", "", "", true, -1});
		rel.assets.push_back({"SHA256SUMS", DOWNLOADS + "v1.2.0/SHA256SUMS", 70, ""});
		s_server.Serve(DOWNLOADS + "v1.2.0/SHA256SUMS", std::string(64, 'd') + "  something-else.zip\n");
		s_server.Serve(API, ReleasesJson({rel}));
	}
	Init(MakeSetup(app, "1.0.0"));
	Check();
	Wait();
	Download();
	CHECK(Wait().state == State::Ready);
	// A checksum file that cannot be fetched.
	s_server.Clear();
	OldFolder(app);
	{
		Rel rel = ServeRelease("1.2.0", zip, Serving{false, true, false, "", "", "", true, -1});
		s_server.Serve(API, ReleasesJson({rel}));
		s_server.Serve(DOWNLOADS + "v1.2.0/PSGopher-v1.2.0.zip.sha256", "", 500);
	}
	Init(MakeSetup(app, "1.0.0"));
	Check();
	Wait();
	Download();
	{
		const Status status = Wait();
		CHECK(status.state == State::Failed && Contains(status.error, "checksum could not be read"));
	}
	// The ZIP itself not served.
	s_server.Clear();
	OldFolder(app);
	{
		Rel rel = ServeRelease("1.2.0", zip, Serving{false, false, false, "", "", "", true, -1});
		s_server.Serve(API, ReleasesJson({rel}));
		s_server.Serve(DOWNLOADS + "v1.2.0/PSGopher-v1.2.0.zip", "", 404);
	}
	Init(MakeSetup(app, "1.0.0"));
	Check();
	Wait();
	Download();
	{
		const Status status = Wait();
		CHECK(status.state == State::Failed && Contains(status.error, "404"));
		CHECK(!Exists(app + "/.update"));
	}
	// Bytes that are no ZIP, with no checksum to catch it.
	{
		const Status status = ToReady(work, app, "PK\x03\x04 this is not a zip", Serving{false, false, false, "", "", "", true, -1});
		CHECK(status.state == State::Failed && Contains(status.error, "not a ZIP"));
		CHECK(!Exists(app + "/.update"));
	}
	// A ZIP damaged after it was made (a byte of eboot.bin's data changed): the CRC catches it.
	{
		// eboot.bin's data is most of the archive: a byte a third of the way in is in it.
		std::string damaged = MakeZip(work, Package("1.2.0"));
		damaged[damaged.size() / 3] ^= 0x55;
		const Status status = ToReady(work, app, damaged, Serving{false, false, false, "", "", "", true, -1});
		CHECK(status.state == State::Failed);
		OldFolder(work + "/reference");
		CHECK(Unchanged(app, Snapshot(work + "/reference")));
	}
}

static void TestRefusedZips(const std::string& work, const std::string& app)
{
	std::fprintf(stderr, "ZIPs that are refused\n");
	const std::string t = "PPSA99280/";
	struct Case
	{
		const char* what;
		std::vector<Item> items;
		std::vector<std::pair<std::string, std::string>> patches;
		const char* says;
	};
	std::vector<Case> cases;
	{
		auto items = Package("1.2.0");
		items.push_back({t + "zz/x", "escape"});
		cases.push_back({"a \"..\" entry", items, {{t + "zz/x", t + "../x"}}, "\"..\""});
	}
	{
		auto items = Package("1.2.0");
		items.push_back({"xetc/passwd", "escape"});
		cases.push_back({"an absolute path", items, {{"xetc/passwd", "/etc/passwd"}}, "absolute"});
	}
	{
		auto items = Package("1.2.0");
		items.push_back({t + "art/aXb", "x"});
		cases.push_back({"a backslash", items, {{t + "art/aXb", t + "art/a\\b"}}, "backslash"});
	}
	{
		auto items = Package("1.2.0");
		items.push_back({t + "art/link", "/etc/passwd", false, 0120777});
		cases.push_back({"a symbolic link", items, {}, "symbolic link"});
	}
	{
		auto items = Package("1.2.0");
		items.push_back({t + "art/fifo", "", false, 010777});
		cases.push_back({"a special file", items, {}, "not a plain file"});
	}
	{
		auto items = Package("1.2.0");
		items.push_back({"OTHER/eboot.bin", "x"});
		cases.push_back({"an entry outside the title's folder", items, {}, "outside"});
	}
	{
		auto items = Package("1.2.0");
		items.erase(std::remove_if(items.begin(), items.end(), [&](const Item& i) { return i.name == t + "eboot.bin"; }),
			items.end());
		cases.push_back({"no eboot.bin", items, {}, "no eboot.bin"});
	}
	{
		auto items = Package("1.2.0");
		items.erase(std::remove_if(items.begin(), items.end(),
						[&](const Item& i) { return i.name == t + "sce_sys/param.json"; }),
			items.end());
		cases.push_back({"no param.json", items, {}, "no sce_sys/param.json"});
	}
	{
		auto items = Package("1.2.0");
		for (Item& item : items)
			if (item.name == t + "sce_sys/param.json")
				item.data = "{\"titleId\": \"PPSA99249\"}";
		cases.push_back({"another title's param.json", items, {}, "param.json is not"});
	}
	{
		auto items = Package("1.2.0");
		items.push_back({t + "readme.md", "x"});
		cases.push_back({"two names that differ only in case", items, {}, "twice"});
	}
	{
		auto items = Package("1.2.0");
		items.push_back({t + "sce_sys/games.db/inner", "x"});
		cases.push_back({"a file below a file", items, {}, "below something"});
	}
	for (const Case& one : cases)
	{
		std::fprintf(stderr, "  %s\n", one.what);
		const std::string zip = MakeZip(work, one.items, one.patches);
		const Status status = ToReady(work, app, zip);
		CHECK(status.state == State::Failed);
		CHECK(Contains(status.error, "refused") || Contains(status.error, "not a ZIP"));
		CHECK(Contains(status.error, one.says));
		if (!Contains(status.error, one.says))
			std::fprintf(stderr, "  said: %s\n", status.error.c_str());
		OldFolder(work + "/reference");
		CHECK(Unchanged(app, Snapshot(work + "/reference")));
		CHECK(!Exists(work + "/x") && !Exists(app + "/../x"));
	}
	// An encrypted entry, and one packed in a way that cannot be read (the flag and the method
	// written into an entry of the program's files).
	for (int which = 0; which < 2; which++)
	{
		std::fprintf(stderr, "  %s\n", which == 0 ? "an encrypted entry" : "an unreadable method");
		const std::string zip = MakeZip(work, Package("1.2.0"), {}, which == 0 ? t + "sce_module/libc.prx" : "",
			which == 1 ? t + "sce_module/libc.prx" : "");
		const Status status = ToReady(work, app, zip);
		CHECK(status.state == State::Failed && Contains(status.error, which == 0 ? "is encrypted" : "cannot be read"));
		OldFolder(work + "/reference");
		CHECK(Unchanged(app, Snapshot(work + "/reference")));
	}
	// One name twice (written as two names and patched into one): the `zip` crate keeps one entry
	// of them, or the archive is refused; either way the file is put in place once, whole.
	{
		std::fprintf(stderr, "  one name twice\n");
		auto items = Package("1.2.0");
		items.push_back({t + "NOTICE.tx2", "again"});
		const std::string zip = MakeZip(work, items, {{t + "NOTICE.tx2", t + "NOTICE.txt"}});
		const Status status = ToReady(work, app, zip);
		if (status.state == State::Ready)
		{
			const std::string plan = Get(app + "/.update/plan");
			CHECK(plan.find("NOTICE.txt") != std::string::npos && plan.find("NOTICE.txt") == plan.rfind("NOTICE.txt"));
			const std::string staged = Get(app + "/.update/stage/NOTICE.txt");
			CHECK(staged == "again" || staged == "legal 1.2.0");
			OldFolder(work + "/reference");
			CHECK(Unchanged(app, Snapshot(work + "/reference")));
		}
		else
			CHECK(status.state == State::Failed && (Contains(status.error, "refused") || Contains(status.error, "not a ZIP")));
	}
	// Too large, and too many entries (the limits made small for the test).
	{
		const std::string zip = MakeZip(work, Package("1.2.0"));
		Test::max_unpacked = 100000;
		Status status = ToReady(work, app, zip);
		Test::max_unpacked = 0;
		CHECK(status.state == State::Failed && Contains(status.error, "unpacks to more than"));
		Test::max_entries = 5;
		status = ToReady(work, app, zip);
		Test::max_entries = 0;
		CHECK(status.state == State::Failed && Contains(status.error, "too many"));
		OldFolder(work + "/reference");
		CHECK(Unchanged(app, Snapshot(work + "/reference")));
	}
	// Entries without a Unix mode (a ZIP made elsewhere) are taken as plain files and folders.
	{
		auto items = Package("1.2.0");
		for (Item& item : items)
			item.mode = 0;
		const Status status = ToReady(work, app, MakeZip(work, items));
		CHECK(status.state == State::Ready);
	}
}

// `links`: the drive keeps links (the console's own drive; the PC's), and the new files are renamed
// onto the old ones; otherwise as on exFAT, where each old file is moved aside first.
static void TestInterrupted(const std::string& work, const std::string& app, bool links)
{
	std::fprintf(stderr, "installs interrupted, renames that fail or say they worked when they did not (%s)\n",
		links ? "a drive with links" : "a drive without links");
	Test::no_links = links ? 0 : 1;
	const std::string zip = MakeZip(work, Package("1.2.0"));
	OldFolder(work + "/reference");
	const auto original = Snapshot(work + "/reference");

	// How many renames a whole install makes.
	CHECK(ToReady(work, app, zip).state == State::Ready);
	std::string plan = Get(app + "/.update/plan");
	int files = 0;
	for (char c : plan)
		files += c == '\n';
	CHECK(files == 12);
	int existing = 0;
	for (size_t start = 0; start < plan.size();)
	{
		const size_t end = plan.find('\n', start);
		existing += Exists(app + "/" + plan.substr(start, end - start));
		start = end + 1;
	}
	const int renames = files + existing;
	std::fprintf(stderr, "  %d files, %d renames\n", files, renames);

	int cuts = 0, undos = 0;
	for (int k = 0; k <= renames; k++)
	{
		CHECK(ToReady(work, app, zip).state == State::Ready);
		Test::stop_after = k;
		const bool done = Install();
		Test::stop_after = -1;
		CHECK(!done);
		CHECK(Exists(app + "/.update/journal"));
		CHECK(Exists(app + "/.update/linked") == links);
		// With links, no power cut leaves the title without a program to start.
		if (links)
			CHECK(Exists(app + "/eboot.bin"));
		// A few undos are themselves interrupted, at each of their steps.
		const bool nested = k == 1 || k == renames / 2 || k == renames;
		for (int j = 0; nested && j < 6 && Exists(app + "/.update/journal"); j++)
		{
			Test::undo_stop_after = j;
			CHECK(Recover(app));
			Test::undo_stop_after = -1;
			undos++;
		}
		// An undo that was let run to its end left no journal; one that was cut short did.
		const bool pending = Exists(app + "/.update/journal");
		CHECK(pending || nested);
		CHECK(Recover(app) == pending);
		CHECK(Unchanged(app, original));
		CHECK(!Recover(app)); // nothing left to do
		cuts++;
	}
	std::fprintf(stderr, "  %d power cuts and %d interrupted undos, each put back exactly\n", cuts, undos);

	// An interrupted install not undone yet (an undo that could not finish): a new download must
	// not take its backups away. It is refused, nothing moves, and the next start undoes it.
	for (const int k : {1, renames / 2, renames - 1})
	{
		CHECK(ToReady(work, app, zip).state == State::Ready);
		Test::stop_after = k;
		CHECK(!Install());
		Test::stop_after = -1;
		CHECK(Exists(app + "/.update/journal"));
		const auto pending = Snapshot(app);
		Init(MakeSetup(app, "1.0.0"));
		Check();
		CHECK(Wait().state == State::Available);
		Download();
		const Status refused = Wait();
		CHECK(refused.state == State::Failed && Contains(refused.error, "not undone"));
		CHECK(Unchanged(app, pending));
		CHECK(Recover(app));
		CHECK(Unchanged(app, original));
	}

	for (int k = 0; k < renames; k++)
	{
		CHECK(ToReady(work, app, zip).state == State::Ready);
		Test::fail_at = k;
		CHECK(!Install());
		Test::fail_at = -1;
		const Status status = GetStatus();
		CHECK(status.state == State::Failed && Contains(status.error, "could not be"));
		CHECK(Unchanged(app, original));
	}
	for (int k = 0; k < renames; k++)
	{
		CHECK(ToReady(work, app, zip).state == State::Ready);
		Test::lie_at = k;
		CHECK(!Install());
		Test::lie_at = -1;
		CHECK(GetStatus().state == State::Failed);
		CHECK(Unchanged(app, original));
	}

	// What stands where a program file goes is a folder: nothing is begun.
	CHECK(ToReady(work, app, zip).state == State::Ready);
	unlink((app + "/write-broker.elf").c_str());
	MakeDirs(app + "/write-broker.elf");
	Put(app + "/write-broker.elf/mine", "x");
	{
		const auto before = Snapshot(app, true);
		CHECK(!Install());
		CHECK(Contains(GetStatus().error, "not a file"));
		CHECK(Unchanged(app, before));
	}
	// The stage lost a file between Download() and Install().
	CHECK(ToReady(work, app, zip).state == State::Ready);
	unlink((app + "/.update/stage/licenses/GPL-3.0.txt").c_str());
	CHECK(!Install());
	CHECK(Contains(GetStatus().error, "incomplete"));
	CHECK(Unchanged(app, original));

	// What an earlier update left without a journal is removed at the start; nothing else moves.
	OldFolder(app);
	Put(app + "/.update/release.zip", "half");
	Put(app + "/.update/stage/eboot.bin", "new");
	CHECK(!Recover(app));
	CHECK(Unchanged(app, original));
	CHECK(!Recover(app));
	CHECK(!Recover(""));
	// A journal that names what is not the program's: those lines are passed over.
	OldFolder(app);
	Put(app + "/.update/journal", "config/psgopher.ini\n../outside\neboot.bin\n");
	Put(app + "/.update/backup/config/psgopher.ini", "an attacker's");
	CHECK(Recover(app));
	CHECK(Unchanged(app, original));
	Test::no_links = 0;
}

static void TestCancel(const std::string& work, const std::string& app)
{
	std::fprintf(stderr, "a download stopped half-way\n");
	const std::string zip = MakeZip(work, Package("1.2.0", 3000000));
	s_server.Clear();
	OldFolder(app);
	const auto before = Snapshot(app);
	const Rel rel = ServeRelease("1.2.0", zip);
	s_server.Serve(API, ReleasesJson({rel}));
	s_server.chunk = 16384;
	s_server.delay_ms = 3;
	Init(MakeSetup(app, "1.0.0"));
	Check();
	CHECK(Wait().state == State::Available);
	Download();
	Download(); // a second press while it runs: nothing more
	bool seen = false;
	for (int i = 0; i < 5000 && !seen; i++)
	{
		const Status status = GetStatus();
		seen = status.state == State::Downloading && status.done > 0 && status.done < status.total;
		if (!seen)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(seen);
	Check(); // ignored while downloading
	Cancel();
	const Status status = Wait();
	CHECK(status.state == State::Available);
	CHECK(status.done == 0);
	CHECK(!Exists(app + "/.update"));
	CHECK(Unchanged(app, before));
	// And then the whole of it.
	s_server.delay_ms = 0;
	Download();
	CHECK(Wait().state == State::Ready);
	CHECK(Install());
	s_server.chunk = 65536;
	(void)work;
}

static void TestFreeSpace(const std::string& work, const std::string& app)
{
	std::fprintf(stderr, "free space\n");
	const std::string zip = MakeZip(work, Package("1.2.0", 4000000));
	OldFolder(work + "/reference");
	const auto original = Snapshot(work + "/reference");
	for (int which = 0; which < 2; which++)
	{
		s_server.Clear();
		OldFolder(app);
		const Rel rel = ServeRelease("1.2.0", zip);
		s_server.Serve(API, ReleasesJson({rel}));
		Setup setup = MakeSetup(app, "1.0.0");
		// First too little for the download; then enough for it but not for what it unpacks to.
		// (the release's files fill about all of its ZIP here: the first question, for the
		// download, is answered with plenty, and the second, for the unpacking, with little)
		static std::atomic<int> asked;
		asked = 0;
		setup.free_space = [which](const std::string&) -> int64_t {
			return which == 0 || asked++ > 0 ? (1 << 20) : (int64_t(1) << 40);
		};
		Init(setup);
		Check();
		CHECK(Wait().state == State::Available);
		Download();
		const Status status = Wait();
		CHECK(status.state == State::Failed && Contains(status.error, "not enough free space"));
		CHECK(s_server.Asked(DOWNLOADS + "v1.2.0/PSGopher-v1.2.0.zip") == (which == 1));
		CHECK(Unchanged(app, original));
	}
	// statvfs() on the real folder: plenty.
	s_server.Clear();
	CHECK(ToReady(work, app, zip).state == State::Ready);
}

// 1.0.4: the title folder without the kept version (.previous/).
static std::map<std::string, std::string> WithoutKept(std::map<std::string, std::string> files)
{
	for (auto it = files.begin(); it != files.end();)
		it = it->first.compare(0, 10, ".previous/") == 0 ? files.erase(it) : std::next(it);
	return files;
}

// 1.0.4: from the old folder, v1.2.0 installed with v1.0.0 kept (the idea after ProsperoEden).
static Status Installed120(const std::string& work, const std::string& app, const std::string& zip)
{
	if (ToReady(work, app, zip).state != State::Ready || !Install())
		return GetStatus();
	// The new version starting: it reads what is kept.
	Init(MakeSetup(app, "1.2.0"));
	return GetStatus();
}

// 1.0.4: the version before an update kept, and put back (Settings > Updates > Go back).
static void TestKeepAndRollBack(const std::string& work, const std::string& app, bool links)
{
	std::fprintf(stderr, "the version before kept, and gone back to (%s)\n", links ? "a drive with links" : "a drive without links");
	Test::no_links = links ? 0 : 1;
	const std::string zip = MakeZip(work, Package("1.2.0"));
	OldFolder(work + "/reference");
	const auto original = Snapshot(work + "/reference");

	// Kept: the old files the install replaced, with their sizes and digests; the folder's own
	// files are the new ones.
	CHECK(!Recover(app));
	{
		const Status status = Installed120(work, app, zip);
		CHECK(status.previous == "1.0.0" && status.previous_label == "v1.0.0");
		CHECK(status.previous_bytes == std::string("EBOOT old").size() + std::string("legal old").size() +
			std::string("readme old").size() + std::string("symbols old").size() + std::string("elevator old").size() +
			Get(app + "/.previous/files/sce_sys/param.json").size() + std::string("icon old").size() +
			std::string("libc old").size() + std::string("games old").size() + std::string("font old").size() +
			std::string("GPL old").size());
	}
	CHECK(Get(app + "/.previous/files/eboot.bin") == "EBOOT old");
	CHECK(Get(app + "/.previous/files/licenses/GPL-3.0.txt") == "GPL old");
	CHECK(!Exists(app + "/.previous/files/sce_module/extra.prx")); // not replaced: not kept
	CHECK(!Exists(app + "/.previous/files/psgopher-boot.log"));
	{
		const std::string manifest = Get(app + "/.previous/manifest");
		CHECK(manifest.compare(0, 28, "PSGopher previous version 1\n") == 0);
		CHECK(Contains(manifest, "\nversion 1.0.0\n") && Contains(manifest, "\nlabel v1.0.0\n"));
		CHECK(Contains(manifest, ("file " + Sha256Hex("EBOOT old", 9) + " 9 eboot.bin\n").c_str()));
		CHECK(Contains(manifest, "\nadded art/new-in-1.2.0/\n") && Contains(manifest, "\nadded art/new-in-1.2.0/thing.txt\n"));
	}
	CHECK(Get(app + "/eboot.bin").compare(0, 12, "EBOOT 1.2.0\n") == 0);
	CHECK(!Exists(app + "/.update"));
	CHECK(Contains(Get(app + "/.previous/manifest"), "\ninstalled 1.2.0\n"));
	const auto installed = Snapshot(app);
	// Another version running (one copied over by hand): the kept copy is not offered.
	Init(MakeSetup(app, "1.2.1"));
	CHECK(GetStatus().previous.empty());
	RollBack();
	CHECK(GetStatus().state == State::Failed);
	CHECK(Snapshot(app) == installed);
	Init(MakeSetup(app, "1.2.0"));
	CHECK(GetStatus().previous == "1.0.0");

	// Going back: the old version's files exactly as they were, what the update added gone, and
	// the kept copy used up.
	RollBack();
	CHECK(GetStatus().rollback);
	{
		const Status status = Wait();
		CHECK(status.state == State::Installed && status.rollback && status.version == "1.0.0");
		CHECK(status.previous.empty());
	}
	CHECK(Unchanged(app, original));
	// Once Installed, nothing more is started; and nothing is kept to go back to.
	RollBack();
	CHECK(GetStatus().state == State::Installed);
	Init(MakeSetup(app, "1.0.0"));
	RollBack();
	CHECK(GetStatus().state == State::Failed && Contains(GetStatus().error, "No earlier version"));
	CHECK(Unchanged(app, original));

	// A going back stopped at every step, as by a power cut, and at some of the undo's steps too:
	// the next start puts the newer version back exactly, the kept copy with it, and it can still
	// go back afterwards.
	int cuts = 0;
	bool whole = false;
	for (int k = 0; k < 100 && !whole; k++)
	{
		CHECK(Installed120(work, app, zip).previous == "1.0.0");
		CHECK(Snapshot(app) == installed);
		Test::stop_after = k;
		RollBack();
		const Status status = Wait();
		Test::stop_after = -1;
		if (status.state == State::Installed)
		{
			whole = true;
			CHECK(Unchanged(app, original));
			break;
		}
		CHECK(status.state == State::Failed);
		CHECK(Exists(app + "/.update/journal") && Exists(app + "/.update/rollback"));
		if (links)
			CHECK(Exists(app + "/eboot.bin"));
		const bool nested = k == 1 || k == 7 || k == 12;
		for (int j = 0; nested && j < 6 && Exists(app + "/.update/journal"); j++)
		{
			Test::undo_stop_after = j;
			CHECK(Recover(app));
			Test::undo_stop_after = -1;
		}
		const bool pending = Exists(app + "/.update/journal");
		CHECK(Recover(app) == pending);
		CHECK(Unchanged(app, installed));
		cuts++;
		// The kept copy is whole again: going back now works.
		if (k % 3 == 0 || nested)
		{
			Init(MakeSetup(app, "1.2.0"));
			CHECK(GetStatus().previous == "1.0.0");
			RollBack();
			CHECK(Wait().state == State::Installed);
			CHECK(Unchanged(app, original));
		}
	}
	CHECK(whole);
	CHECK(cuts > 12);
	std::fprintf(stderr, "  %d going backs stopped, each put back exactly\n", cuts);
	// Stopped before the journal: once the marker is written, once the kept files are in the stage.
	for (const int stop : {1, 2})
	{
		CHECK(Installed120(work, app, zip).previous == "1.0.0");
		Test::rollback_stop = stop;
		RollBack();
		CHECK(Wait().state == State::Failed);
		Test::rollback_stop = -1;
		CHECK(Exists(app + "/.update/rollback"));
		CHECK(Exists(app + "/.previous/files") == (stop == 1));
		CHECK(!Recover(app));
		CHECK(Unchanged(app, installed));
	}
	// A rename that fails, or says it worked when it did not: undone at once, the kept copy whole.
	for (int k = 0; k < 24; k++)
	{
		CHECK(Installed120(work, app, zip).previous == "1.0.0");
		Test::fail_at = k % 2 == 0 ? k : -1;
		Test::lie_at = k % 2 == 1 ? k : -1;
		RollBack();
		const Status status = Wait();
		Test::fail_at = Test::lie_at = -1;
		if (status.state == State::Installed)
			continue; // past the last rename
		CHECK(status.state == State::Failed);
		CHECK(Unchanged(app, installed));
		CHECK(GetStatus().previous == "1.0.0");
	}

	// A kept copy that is not whole is not offered, and refused.
	CHECK(Installed120(work, app, zip).previous == "1.0.0");
	unlink((app + "/.previous/files/licenses/GPL-3.0.txt").c_str());
	Init(MakeSetup(app, "1.2.0"));
	CHECK(GetStatus().previous.empty());
	{
		const auto before = Snapshot(app);
		RollBack();
		CHECK(Wait().state == State::Failed);
		CHECK(Unchanged(app, before));
	}
	// One whose file was changed (the same size): offered, then refused at its digests, and removed;
	// the title's files are untouched.
	CHECK(Installed120(work, app, zip).previous == "1.0.0");
	Put(app + "/.previous/files/eboot.bin", "EBOOT OLD");
	Init(MakeSetup(app, "1.2.0"));
	CHECK(GetStatus().previous == "1.0.0");
	RollBack();
	{
		const Status status = Wait();
		CHECK(status.state == State::Failed && Contains(status.error, "damaged") && Contains(status.error, "eboot.bin"));
		CHECK(status.previous.empty());
	}
	CHECK(!Exists(app + "/.previous") && !Exists(app + "/.update"));
	CHECK(Snapshot(app) == WithoutKept(installed));
	// A damaged manifest, or one that names what is not the program's: nothing is offered.
	for (const char* manifest : {"garbage\n", "PSGopher previous version 1\nversion 1.0.0\nfile 00 9 eboot.bin\n",
			 "PSGopher previous version 1\nversion 1.0.0\n",
			 "PSGopher previous version 1\nversion 1.0.0\nfile 0000000000000000000000000000000000000000000000000000000000000000 3 psgopher-boot.log\n"})
	{
		CHECK(Installed120(work, app, zip).previous == "1.0.0");
		Put(app + "/.previous/manifest", manifest);
		Init(MakeSetup(app, "1.2.0"));
		CHECK(GetStatus().previous.empty());
		RollBack();
		CHECK(Wait().state == State::Failed);
		CHECK(Get(app + "/psgopher-boot.log") == "the user's patches");
		CHECK(WithoutKept(Snapshot(app)) == WithoutKept(installed));
	}

	// Too little room left beside it: not kept.
	{
		s_server.Clear();
		OldFolder(app);
		const Rel rel = ServeRelease("1.2.0", zip);
		s_server.Serve(API, ReleasesJson({rel}));
		Setup setup = MakeSetup(app, "1.0.0");
		setup.free_space = [](const std::string&) { return static_cast<int64_t>(100) << 20; };
		Init(setup);
		Check();
		CHECK(Wait().state == State::Available);
		Download();
		CHECK(Wait().state == State::Ready);
		CHECK(Install());
		CHECK(!Exists(app + "/.previous"));
		CHECK(GetStatus().previous.empty());
		CHECK(WithoutKept(installed) == Snapshot(app));
	}
	// A download that needs the room the kept version takes: it is removed first.
	{
		CHECK(Installed120(work, app, zip).previous == "1.0.0");
		const std::string zip13 = MakeZip(work, Package("1.3.0"));
		s_server.Clear();
		const Rel rel = ServeRelease("1.3.0", zip13);
		s_server.Serve(API, ReleasesJson({rel}));
		Setup setup = MakeSetup(app, "1.2.0");
		setup.free_space = [app](const std::string&) {
			return Exists(app + "/.previous") ? static_cast<int64_t>(1) << 20 : static_cast<int64_t>(1) << 40;
		};
		Init(setup);
		CHECK(GetStatus().previous == "1.0.0");
		Check();
		CHECK(Wait().state == State::Available);
		Download();
		CHECK(Wait().state == State::Ready);
		CHECK(!Exists(app + "/.previous"));
		CHECK(Install());
		// v1.2.0 kept now, and going back to it gives v1.2.0's files.
		Init(MakeSetup(app, "1.3.0"));
		CHECK(GetStatus().previous == "1.2.0");
		RollBack();
		CHECK(Wait().state == State::Installed);
		CHECK(Get(app + "/eboot.bin").compare(0, 12, "EBOOT 1.2.0\n") == 0);
		CHECK(!Exists(app + "/art/new-in-1.3.0"));
		CHECK(Exists(app + "/art/new-in-1.2.0/thing.txt"));
		CHECK(WithoutKept(Snapshot(app)) == WithoutKept(installed));
	}
	Test::no_links = 0;
}

// 1.0.4: "Skip this version": remembered; a newer release is offered again.
// A developer build that is asked to take a release (offer_developer): installed, its own files
// kept as the version before (labelled as given), and put back by RollBack(); the user's files
// are the same throughout.
static void TestDeveloperBuild(const std::string& work, const std::string& app)
{
	std::fprintf(stderr, "a developer build offered a release\n");
	s_server.Clear();
	OldFolder(app);
	OldFolder(work + "/reference");
	const auto original = Snapshot(work + "/reference");
	const Rel rel = ServeRelease("1.2.0", MakeZip(work, Package("1.2.0")));
	s_server.Serve(API, ReleasesJson({rel}));
	Setup setup = MakeSetup(app, "");
	setup.offer_developer = true;
	setup.running_label = "developer build";
	Init(setup);
	Check();
	CHECK(Wait().state == State::Available);
	Download();
	CHECK(Wait().state == State::Ready);
	CHECK(Install());
	CHECK(Get(app + "/eboot.bin").compare(0, 6, "EBOOT ") == 0 && Get(app + "/eboot.bin") != "EBOOT old");
	CHECK(Get(app + "/psgopher-boot.log") == "the user's patches");
	// Started again as the release it now is: the version before is the developer build.
	Init(MakeSetup(app, "1.2.0"));
	Status status = GetStatus();
	CHECK(status.previous_label == "developer build");
	if (!status.previous.empty())
	{
		RollBack();
		status = Wait();
		CHECK(status.state == State::Installed);
		CHECK(Get(app + "/eboot.bin") == "EBOOT old");
		CHECK(Get(app + "/psgopher-boot.log") == "the user's patches");
	}
	else
		CHECK(false);
	(void)original;
}

static void TestSkip(const std::string& work, const std::string& app)
{
	std::fprintf(stderr, "a version skipped\n");
	const std::string zip = MakeZip(work, Package("1.2.0"));
	const std::string skip = work + "/update-skipped.txt";
	unlink(skip.c_str());
	s_server.Clear();
	OldFolder(app);
	const Rel rel = ServeRelease("1.2.0", zip);
	s_server.Serve(API, ReleasesJson({rel}));
	Setup setup = MakeSetup(app, "1.0.0");
	setup.skip_file = skip;
	Init(setup);
	Skip(); // nothing offered yet: nothing skipped
	CHECK(!Exists(skip));
	Check();
	{
		const Status status = Wait();
		CHECK(status.state == State::Available && !status.skipped);
	}
	Skip();
	CHECK(GetStatus().skipped && GetStatus().state == State::Available);
	CHECK(Get(skip) == "1.2.0\n");
	// The next start: the same version is said to be skipped (still Available: it can be installed
	// by hand).
	Init(setup);
	Check();
	{
		const Status status = Wait();
		CHECK(status.state == State::Available && status.skipped && status.version == "1.2.0");
	}
	Download();
	CHECK(Wait().state == State::Ready);
	Init(setup);
	// A newer one: offered again.
	const std::string zip13 = MakeZip(work, Package("1.3.0"));
	s_server.Clear();
	const Rel rel13 = ServeRelease("1.3.0", zip13);
	const Rel rel12 = ServeRelease("1.2.0", zip);
	s_server.Serve(API, ReleasesJson({rel13, rel12}));
	Check();
	{
		const Status status = Wait();
		CHECK(status.state == State::Available && !status.skipped && status.version == "1.3.0");
		CHECK(status.newer.size() == 2);
	}
	// Without a file to remember it in: skipped for this run only.
	Init(MakeSetup(app, "1.0.0"));
	Check();
	CHECK(Wait().state == State::Available);
	Skip();
	CHECK(GetStatus().skipped);
	unlink(skip.c_str());
	OldFolder(app);
}

// The JSON reader, and the ZIP functions of the Rust core (psgopher_zip.rs) apart from the updater.
static void TestJsonAndZip(const std::string& work)
{
	std::fprintf(stderr, "the JSON reader and the ZIP functions\n");
	JsonValue v;
	const std::string text = "  {\"a\": [1, 2.5, -3, 1e3, true, false, null, \"x\\ny\\u00e9\\ud83d\\ude00\\/\"],\n"
	                         "\"big\": 18446744073709551615, \"huge\": 18446744073709551616, \"neg\": -1, \"o\": {}, \"e\": []} ";
	CHECK(ParseJson(text.data(), text.size(), v) && v.IsObject());
	const JsonValue* a = v.Find("a");
	CHECK(a && a->IsArray() && a->items.size() == 8);
	if (a && a->items.size() == 8)
	{
		CHECK(a->items[0].is_uint && a->items[0].unsigned_value == 1);
		CHECK(!a->items[1].is_uint && !a->items[2].is_uint && !a->items[3].is_uint);
		CHECK(a->items[4].IsBool() && a->items[4].boolean && !a->items[5].boolean);
		CHECK(a->items[7].IsString() && a->items[7].text == "x\ny\xc3\xa9\xf0\x9f\x98\x80/");
	}
	CHECK(v.Find("big") && v.Find("big")->is_uint && v.Find("big")->unsigned_value == UINT64_MAX);
	CHECK(v.Find("huge") && !v.Find("huge")->is_uint);
	CHECK(v.Find("neg") && !v.Find("neg")->is_uint);
	CHECK(v.Find("o") && v.Find("o")->IsObject() && v.Find("e") && v.Find("e")->IsArray());
	CHECK(!v.Find("missing"));
	for (const char* bad : {"", "{", "[1,]", "{\"a\" 1}", "{\"a\":1,}", "[1] x", "\"\\x\"", "\"\\ud800\"", "\"a\nb\"", "01", "1.", "-", "tru",
			 "{1:2}", "[1 2]", "\"\\u12\""})
	{
		JsonValue out;
		CHECK(!ParseJson(bad, std::strlen(bad), out));
	}
	{
		JsonValue out;
		const std::string deep64(64, '['), deep65(65, '[');
		CHECK(ParseJson((deep64 + std::string(64, ']')).c_str(), 128, out));
		CHECK(!ParseJson((deep65 + std::string(65, ']')).c_str(), 130, out));
	}

	// An archive of a deflated entry, a stored one, a folder and a link.
	const std::string big = Filler("big", 200000);
	const std::string path = work + "/zipfunctions.zip";
	Put(path, MakeZip(work, {{"d/", "", true}, {"d/big.bin", big}, {"d/small.txt", "small"}, {"d/link", "target", false, 0120777}}));
	char error[256] = "";
	PsgopherZip* zip = psgopher_zip_open(path.c_str(), error, sizeof(error));
	CHECK(zip != nullptr && psgopher_zip_count(zip) == 4);
	if (zip)
	{
		PsgopherZipEntry entry;
		uint8_t name[64];
		CHECK(psgopher_zip_entry(zip, 1, name, sizeof(name), &entry) == 9 && std::memcmp(name, "d/big.bin", 9) == 0);
		CHECK(entry.size == big.size() && (entry.flags & PSGOPHER_ZIP_UNIX_MODE) && (entry.unix_mode & 0170000) == 0100000);
		CHECK(!(entry.flags & (PSGOPHER_ZIP_ENCRYPTED | PSGOPHER_ZIP_UNREADABLE_METHOD)));
		CHECK(psgopher_zip_entry(zip, 3, name, sizeof(name), &entry) == 6 && (entry.unix_mode & 0170000) == 0120000);
		CHECK(psgopher_zip_entry(zip, 4, name, sizeof(name), &entry) < 0);
		// Read in blocks that do not divide it, the deflated entry whole, one reader at a time.
		PsgopherZipReader* reader = psgopher_zip_read_open(zip, 1);
		CHECK(reader != nullptr && psgopher_zip_read_open(zip, 2) == nullptr && psgopher_zip_close(zip) != 0);
		std::string got;
		uint8_t block[7001];
		for (;;)
		{
			const int64_t n = reader ? psgopher_zip_read(reader, block, sizeof(block)) : 0;
			CHECK(n >= 0);
			if (n <= 0)
				break;
			got.append(reinterpret_cast<const char*>(block), static_cast<size_t>(n));
		}
		CHECK(got == big);
		if (reader)
			CHECK(psgopher_zip_read_close(reader) == 0);
		reader = psgopher_zip_read_open(zip, 2);
		CHECK(reader != nullptr);
		if (reader)
		{
			CHECK(psgopher_zip_read(reader, block, sizeof(block)) == 5 && psgopher_zip_read(reader, block, sizeof(block)) == 0);
			psgopher_zip_read_close(reader);
		}
		CHECK(psgopher_zip_close(zip) == 0);
	}
	// A damaged entry (a byte of its data changed: the CRC says so), a truncated archive, a file that is no ZIP.
	{
		std::string bytes = Get(path);
		const size_t at = bytes.find("small", bytes.find("d/small.txt") + 11); // (the entry's data, after its name)
		CHECK(at != std::string::npos);
		bytes[at] = 'S';
		Put(path + ".bad", bytes);
		PsgopherZip* bad = psgopher_zip_open((path + ".bad").c_str(), error, sizeof(error));
		CHECK(bad != nullptr);
		if (bad)
		{
			PsgopherZipReader* reader = psgopher_zip_read_open(bad, 2);
			uint8_t block[64];
			bool failed = reader == nullptr;
			while (reader)
			{
				const int64_t n = psgopher_zip_read(reader, block, sizeof(block));
				if (n < 0)
					failed = true;
				if (n <= 0)
					break;
			}
			CHECK(failed);
			if (reader)
				psgopher_zip_read_close(reader);
			psgopher_zip_close(bad);
		}
		Put(path + ".cut", Get(path).substr(0, Get(path).size() / 2));
		CHECK(psgopher_zip_open((path + ".cut").c_str(), error, sizeof(error)) == nullptr && error[0] != 0);
		Put(path + ".txt", "this is not a ZIP, and is not long either");
		CHECK(psgopher_zip_open((path + ".txt").c_str(), error, sizeof(error)) == nullptr);
		CHECK(psgopher_zip_open((path + ".none").c_str(), error, sizeof(error)) == nullptr);
	}
}

// A real release ZIP of PSGopher (made by make-release-zip.py), installed over the older folder.
static void TestRealZip(const std::string& work, const std::string& app, const std::string& path)
{
	std::fprintf(stderr, "a real release: %s\n", path.c_str());
	const std::string zip = Get(path);
	CHECK(zip.size() > 100000);
	char open_error[256] = "";
	PsgopherZip* archive = psgopher_zip_open(path.c_str(), open_error, sizeof(open_error));
	CHECK(archive != nullptr);
	if (!archive)
		return;
	std::string eboot;
	for (int64_t i = 0; i < psgopher_zip_count(archive); i++)
	{
		PsgopherZipEntry entry;
		char name[1100];
		const int64_t length = psgopher_zip_entry(archive, static_cast<uint64_t>(i), reinterpret_cast<uint8_t*>(name),
			sizeof(name), &entry);
		if (length < 0 || std::string(name, static_cast<size_t>(length)) != "PPSA99280/eboot.bin")
			continue;
		eboot.resize(static_cast<size_t>(entry.size));
		PsgopherZipReader* file = psgopher_zip_read_open(archive, static_cast<uint64_t>(i));
		CHECK(file != nullptr);
		size_t have = 0;
		while (file && have < eboot.size())
		{
			const int64_t got = psgopher_zip_read(file, reinterpret_cast<uint8_t*>(eboot.data()) + have, eboot.size() - have);
			if (got <= 0)
				break;
			have += static_cast<size_t>(got);
		}
		CHECK(have == eboot.size());
		if (file)
			psgopher_zip_read_close(file);
	}
	psgopher_zip_close(archive);
	CHECK(!eboot.empty());

	const Status status = ToReady(work, app, zip);
	CHECK(status.state == State::Ready);
	if (status.state != State::Ready)
	{
		std::fprintf(stderr, "  said: %s\n", status.error.c_str());
		return;
	}
	OldFolder(work + "/reference");
	const auto original = Snapshot(work + "/reference");
	CHECK(Install());
	CHECK(Get(app + "/eboot.bin") == eboot);
	CHECK(Exists(app + "/sce_module/libc.prx") && Exists(app + "/sce_sys/param.json"));
	CHECK(Exists(app + "/NOTICE.txt") && Exists(app + "/sandbox-elevator.elf"));
	const auto after = Snapshot(app);
	for (const auto& [name, data] : original)
		if (!IsManaged(name) && name.back() != '/')
			CHECK(after.count(name) && after.at(name) == data);
	CHECK(!Exists(app + "/.update"));
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: update_test <work folder> [a release ZIP]\n");
		return 2;
	}
	const std::string work = argv[1];
	MakeDirs(work);
	const std::string app = work + "/PPSA99280";

	TestHelpers();
	TestCheck(work, app);
	TestInstall(work, app);
	TestChecksums(work, app);
	TestRefusedZips(work, app);
	TestInterrupted(work, app, true);
	TestInterrupted(work, app, false);
	TestCancel(work, app);
	TestFreeSpace(work, app);
	TestKeepAndRollBack(work, app, true);
	TestKeepAndRollBack(work, app, false);
	TestSkip(work, app);
	TestDeveloperBuild(work, app);
	TestJsonAndZip(work);
	if (argc >= 3)
		TestRealZip(work, app, argv[2]);

	// The log: one line each, "update: ...", and no address of any kind in it.
	{
		std::lock_guard<std::mutex> lock(s_log_mutex);
		CHECK(!s_log.empty());
		bool shaped = true, quiet = true;
		for (const std::string& line : s_log)
		{
			shaped = shaped && line.compare(0, 8, "update: ") == 0 && line.find('\n') == std::string::npos;
			quiet = quiet && !Contains(line, "http") && !Contains(line, "192.0.2.");
		}
		CHECK(shaped);
		CHECK(quiet);
	}

	if (s_failures != 0)
	{
		std::printf("update: %d checks, %d FAILED\n", s_checks, s_failures);
		return 1;
	}
	std::printf("update: %d checks passed\n", s_checks);
	return 0;
}
