/*
 * PSGopher - a cartridge's No-Intro name by its game ID and revision (nointro.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include "nointro.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

extern "C" void say(const char *format, ...) __attribute__((format(printf, 1, 2)));

namespace psgopher {
namespace {

struct Name {
	std::string name;
	bool retail;
	int revision; // "(Rev N)" in the name; 0 none
};

std::mutex g_mutex;
std::string g_path;
bool g_loaded = false;
std::map<std::string, std::vector<Name>> g_names;
bool g_crcs_loaded = false;
std::map<uint32_t, std::string> g_crcs;

/* "(Rev 1)" -> 1, "(Rev A)" -> 1; none 0 */
int revisionOf(const std::string &name)
{
	const size_t at = name.find("(Rev ");
	if (at == std::string::npos || at + 5 >= name.size())
		return 0;
	const char c = name[at + 5];
	if (c >= '0' && c <= '9')
		return atoi(name.c_str() + at + 5);
	if (c >= 'A' && c <= 'Z')
		return c - 'A' + 1;
	return 0;
}

void load()
{
	g_loaded = true;
	FILE *f = fopen(g_path.c_str(), "r");
	if (!f) {
		if (!g_path.empty())
			say("covers: %s could not be read: covers and descriptions go by the file's name", g_path.c_str());
		return;
	}
	char line[1024];
	size_t count = 0;
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#')
			continue;
		std::string text = line;
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
			text.pop_back();
		const size_t a = text.find('\t'), b = a == std::string::npos ? a : text.find('\t', a + 1);
		if (a != 4 || b == std::string::npos || b + 1 >= text.size())
			continue;
		Name n{ text.substr(b + 1), text.compare(a + 1, b - a - 1, "1") == 0, 0 };
		n.revision = revisionOf(n.name);
		g_names[text.substr(0, 4)].push_back(std::move(n));
		count++;
	}
	fclose(f);
	say("covers: %zu No-Intro names of %zu game IDs (%s)", count, g_names.size(), g_path.c_str());
}

} // namespace

void noIntroUseFile(const std::string &path)
{
	std::lock_guard lock(g_mutex);
	g_path = path;
	g_names.clear();
	g_loaded = false;
	g_crcs.clear();
	g_crcs_loaded = false;
}

std::string noIntroNameOfCrc(uint32_t crc)
{
	std::lock_guard lock(g_mutex);
	if (!g_crcs_loaded) {
		g_crcs_loaded = true;
		const size_t slash = g_path.rfind('/');
		const std::string file = (slash == std::string::npos ? std::string(".") : g_path.substr(0, slash)) + "/nointro-crc.tsv";
		if (FILE *f = fopen(file.c_str(), "r")) {
			char line[1024];
			while (fgets(line, sizeof(line), f)) {
				if (line[0] == '#' || strlen(line) < 10 || line[8] != '\t')
					continue;
				std::string name = line + 9;
				while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
					name.pop_back();
				g_crcs[(uint32_t)strtoul(std::string(line, 8).c_str(), nullptr, 16)] = name;
			}
			fclose(f);
		}
	}
	const auto it = g_crcs.find(crc);
	return it == g_crcs.end() ? "" : it->second;
}

std::string noIntroName(const std::string &id, int revision)
{
	if (id.size() != 4)
		return "";
	std::lock_guard lock(g_mutex);
	if (!g_loaded)
		load();
	const auto it = g_names.find(id);
	if (it == g_names.end())
		return "";
	const int want = revision < 0 ? 0 : revision;
	const Name *best = nullptr;
	/* the cartridge's own release before a re-release of the same ID (a console's online service, a
	 * collection disc) */
	auto plain = [](const Name &n) {
		for (const char *other : { "Virtual Console", "Switch Online", "Collection", "GameCube", "Wii", "iQue", "LodgeNet" })
			if (n.name.find(other) != std::string::npos)
				return false;
		return true;
	};
	auto score = [want, &plain](const Name &n) {
		return (n.retail ? 8 : 0) + (n.revision == want ? 4 : 0) + (plain(n) ? 2 : 0) + (n.revision == 0 ? 1 : 0);
	};
	for (const Name &n : it->second)
		if (!best || score(n) > score(*best) || (score(n) == score(*best) && n.name.size() < best->name.size()))
			best = &n;
	return best ? best->name : "";
}

} // namespace psgopher
