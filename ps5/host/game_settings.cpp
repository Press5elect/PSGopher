/*
 * PSGopher - a game's own settings (game_settings.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "game_settings.h"

#include "library.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>
#include <unistd.h>

extern "C" void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
extern "C" {
bool psgopher_setting_set(const char *name, int64_t value);
int64_t psgopher_setting_get(const char *name);
}

namespace psgopher {

namespace {

std::string folder() { return libraryRoot() + "/psgopher/config/games"; }
std::string journal() { return folder() + "/restore.txt"; }

bool startsWith(const std::string &s, const char *head) { return s.rfind(head, 0) == 0; }

bool gopher64Row(const std::string &key)
{
	if (startsWith(key, "video.") || startsWith(key, "emulation."))
		return key != "video.fullscreen" && key != "emulation.usb"; // (always on; not supported on the console)
	if (startsWith(key, "input.controller_enabled.") || startsWith(key, "input.transfer_pak."))
		return key.size() > 0 && key.back() >= '0' && key.back() <= '3';
	return key == "input.emulate_vru";
}

/* "key = number" lines */
std::vector<std::pair<std::string, int64_t>> readLines(const std::string &path)
{
	std::vector<std::pair<std::string, int64_t>> out;
	FILE *f = fopen(path.c_str(), "r");
	if (!f)
		return out;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char key[96] = {};
		long long value = 0;
		if (line[0] == '#' || sscanf(line, " %95[A-Za-z_0-9.] = %lld", key, &value) != 2)
			continue;
		out.emplace_back(key, (int64_t)value);
	}
	fclose(f);
	return out;
}

bool writeFile(const std::string &path, const std::string &text)
{
	const std::string temporary = path + ".new";
	FILE *f = fopen(temporary.c_str(), "w");
	if (!f)
		return false;
	const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
	fclose(f);
	if (!ok || rename(temporary.c_str(), path.c_str()) != 0) {
		unlink(temporary.c_str());
		return false;
	}
	chmod(path.c_str(), 0666);
	return true;
}

void ensureFolder()
{
	for (const std::string &dir : { libraryRoot() + "/psgopher", libraryRoot() + "/psgopher/config", folder() }) {
		if (mkdir(dir.c_str(), 0777) != 0 && errno != EEXIST)
			return;
		chmod(dir.c_str(), 0777);
	}
}

} // namespace

const int64_t *GameSettings::find(const std::string &key) const
{
	for (const auto &kv : values)
		if (kv.first == key)
			return &kv.second;
	return nullptr;
}

void GameSettings::set(const std::string &key, int64_t value)
{
	for (auto &kv : values)
		if (kv.first == key) {
			kv.second = value;
			return;
		}
	values.emplace_back(key, value);
}

void GameSettings::clear(const std::string &key)
{
	for (size_t i = 0; i < values.size(); i++)
		if (values[i].first == key) {
			values.erase(values.begin() + (long)i);
			return;
		}
}

std::string GameSettings::text() const
{
	std::string out;
	for (const auto &kv : values)
		out += (out.empty() ? "" : ", ") + kv.first + "=" + std::to_string(kv.second);
	return out;
}

bool gameSettingEligible(const std::string &row_key) { return gopher64Row(row_key) || TitleSettings::isField(row_key); }

std::string gameSettingsFile(const std::string &key) { return folder() + "/" + key + ".ini"; }

GameSettings loadGameSettings(const std::string &key)
{
	GameSettings out;
	for (const auto &kv : readLines(gameSettingsFile(key)))
		if (gameSettingEligible(kv.first))
			out.set(kv.first, kv.second);
	return out;
}

bool saveGameSettings(const std::string &key, const GameSettings &settings)
{
	const std::string path = gameSettingsFile(key);
	if (settings.empty()) {
		unlink(path.c_str());
		return true;
	}
	ensureFolder();
	std::string text = "# PSGopher's settings for one game (" + key + "): BENCH's rows set on the game's page, each a number as the row's\n"
	                   "# choices are numbered. They are applied over the global settings when the game starts and put back when it ends.\n"
	                   "# Taking a line out (or choosing Use Global Setting on the page) gives the game the global setting again.\n";
	for (const auto &kv : settings.values)
		text += kv.first + " = " + std::to_string(kv.second) + "\n";
	return writeFile(path, text);
}

GameScope::GameScope(const std::string &key, bool key_from_header, const TitleSettings &global) : effective_(global)
{
	if (key.empty())
		return;
	if (!key_from_header)
		say("settings: the cartridge's header could not be read: its per-game settings are kept by the file's name (%s)", key.c_str());
	const GameSettings game = loadGameSettings(key);
	if (game.empty()) {
		say("settings: no per-game settings for %s", key.c_str());
		return;
	}
	say("settings: per-game for %s: %s", key.c_str(), game.text().c_str());
	for (const auto &kv : game.values) {
		if (TitleSettings::isField(kv.first)) {
			effective_.assign(kv.first, kv.second);
			continue;
		}
		const int64_t was = psgopher_setting_get(kv.first.c_str());
		if (was < 0) {
			say("settings: Gopher64 has no %s: left out", kv.first.c_str());
			continue;
		}
		if (was == kv.second)
			continue;
		if (before_.empty()) {
			/* the journal first: the globals to come back to, whatever happens to the game */
			std::string text;
			for (const auto &other : game.values)
				if (!TitleSettings::isField(other.first)) {
					const int64_t value = psgopher_setting_get(other.first.c_str());
					if (value >= 0)
						text += other.first + " = " + std::to_string(value) + "\n";
				}
			ensureFolder();
			writeFile(journal(), text);
		}
		if (psgopher_setting_set(kv.first.c_str(), kv.second))
			before_.emplace_back(kv.first, was);
		else
			say("settings: Gopher64 did not take %s = %lld", kv.first.c_str(), (long long)kv.second);
	}
}

void GameScope::restore()
{
	if (before_.empty())
		return;
	for (const auto &kv : before_)
		if (!psgopher_setting_set(kv.first.c_str(), kv.second))
			say("settings: Gopher64 did not take back %s = %lld", kv.first.c_str(), (long long)kv.second);
	before_.clear();
	unlink(journal().c_str());
	say("settings: the global settings are back");
}

GameScope::~GameScope() { restore(); }

void recoverGameSettings()
{
	const auto lines = readLines(journal());
	if (lines.empty()) {
		unlink(journal().c_str());
		return;
	}
	for (const auto &kv : lines)
		if (gopher64Row(kv.first))
			psgopher_setting_set(kv.first.c_str(), kv.second);
	unlink(journal().c_str());
	say("settings: a game did not end: its per-game settings are taken out of Gopher64's config again (%zu)", lines.size());
}

} // namespace psgopher
