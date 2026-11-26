/*
 * PSGopher - which games use the Expansion Pak (expansion_pak.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "expansion_pak.h"

#include <cctype>
#include <cstdio>
#include <mutex>

namespace psgopher {

const char *pakStatusName(PakStatus s)
{
	switch (s) {
	case PakStatus::Required:
		return "required";
	case PakStatus::Enhanced:
		return "enhanced";
	case PakStatus::Incompatible:
		return "incompatible";
	default:
		return "not listed";
	}
}

const char *pakFactText(PakStatus s)
{
	switch (s) {
	case PakStatus::Required:
		return "Required";
	case PakStatus::Enhanced:
		return "Enhanced";
	case PakStatus::Incompatible:
		return "Must be off";
	default:
		return "Not listed";
	}
}

bool pakIsOn(int setting, PakStatus status)
{
	if (setting == PakOn)
		return true;
	if (setting == PakOff)
		return false;
	return status != PakStatus::Incompatible;
}

namespace {

std::string upper(std::string s)
{
	for (char &c : s)
		c = (char)toupper((unsigned char)c);
	return s;
}

bool hex(const std::string &s)
{
	for (char c : s)
		if (!isxdigit((unsigned char)c))
			return false;
	return !s.empty();
}

/* the shapes of a key: "NXXE", "NXXE-01", "01234567-89ABCDEF" (upper case); "" is none */
std::string normalKey(const std::string &raw)
{
	const std::string k = upper(raw);
	if (k.size() == 4 && k.find('-') == std::string::npos)
		return k;
	if (k.size() == 7 && k[4] == '-' && hex(k.substr(5)))
		return k;
	if (k.size() == 17 && k[8] == '-' && hex(k.substr(0, 8)) && hex(k.substr(9)))
		return k;
	return "";
}

PakStatus statusOf(const std::string &word)
{
	if (word == "required")
		return PakStatus::Required;
	if (word == "enhanced")
		return PakStatus::Enhanced;
	if (word == "incompatible")
		return PakStatus::Incompatible;
	return PakStatus::NotListed;
}

} // namespace

size_t PakTable::parse(const std::string &text)
{
	size_t added = 0, at = 0;
	while (at < text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos)
			end = text.size();
		std::string line = text.substr(at, end - at);
		at = end + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;
		std::vector<std::string> f;
		size_t from = 0;
		for (;;) {
			const size_t tab = line.find('\t', from);
			f.push_back(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
			if (tab == std::string::npos)
				break;
			from = tab + 1;
		}
		if (f.size() < 3)
			continue;
		const std::string key = normalKey(f[0]);
		const PakStatus status = statusOf(f[1]);
		if (key.empty() || status == PakStatus::NotListed || f[2].empty())
			continue;
		bool dup = false;
		for (const Entry &e : entries_)
			dup = dup || e.key == key;
		if (dup)
			continue;
		entries_.push_back({ key, status, f[2] });
		added++;
	}
	return added;
}

bool PakTable::loadFile(const std::string &path)
{
	FILE *f = fopen(path.c_str(), "r");
	if (!f)
		return false;
	std::string text;
	char buffer[4096];
	size_t n;
	while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
		text.append(buffer, n);
	fclose(f);
	parse(text);
	return true;
}

PakInfo PakTable::find(const std::string &key, const std::string &crc) const
{
	auto get = [this](const std::string &k) -> const Entry * {
		for (const Entry &e : entries_)
			if (e.key == k)
				return &e;
		return nullptr;
	};
	const Entry *e = nullptr;
	if (crc.size() >= 17)
		e = get(upper(crc.substr(0, 17)));
	const bool header = key.size() == 7 && key[4] == '-' && key.rfind("file-", 0) != 0;
	if (!e && header)
		e = get(upper(key));
	if (!e && header)
		e = get(upper(key.substr(0, 4)));
	return e ? PakInfo{ e->status, e->source } : PakInfo{};
}

namespace {
std::mutex g_mutex;
std::string g_path;
PakTable g_table;
bool g_loaded = false;
} // namespace

void pakUseFile(const std::string &path)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_path = path;
	g_table = PakTable();
	g_loaded = false;
}

const PakTable &pakTable()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_loaded) {
		g_loaded = true;
		g_table.loadFile(g_path);
	}
	return g_table;
}

PakInfo pakInfoOf(const std::string &key, const std::string &crc) { return pakTable().find(key, crc); }

} // namespace psgopher
