/*
 * PSGopher - covers and descriptions (web.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include "web.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
/* the core's (psgopher.rs) */
int psgopher_cover(const char *name, const char *file);
int psgopher_about(const char *name, char *buffer, size_t size);
}

namespace psgopher {
namespace {

constexpr double kNoneDays = 30;

struct Entry {
	WebState cover = WebState::Pending;
	bool cover_asked = false, cover_taken = false;
	Image picture;
	bool about_asked = false, about_done = false;
	std::string about;
};

struct Web {
	std::mutex mutex;
	std::condition_variable cv;
	std::string folder;
	bool on = false;
	std::map<std::string, Entry> entries;
	std::deque<std::pair<std::string, bool>> queue; // (name, cover: else the description); newest at the front
	double offline_until = 0, wiki_until = 0;
};

Web &web()
{
	/* never destroyed: its thread waits on it to the end (a static's destructor at exit hung there) */
	static Web *w = new Web;
	return *w;
}

std::string safeName(const std::string &name)
{
	std::string out = name;
	for (char &c : out)
		if (strchr("&*/:`<>?\\|\"", c))
			c = '_';
	return out;
}

/* there, and (when when is given) its age in seconds */
bool exists(const std::string &path, double *age = nullptr)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return false;
	if (age)
		*age = difftime(time(nullptr), st.st_mtime);
	return true;
}

bool settledNone(const std::string &path)
{
	double age = 0;
	return exists(path, &age) && age < kNoneDays * 86400;
}

void writeEmpty(const std::string &path)
{
	if (FILE *f = fopen(path.c_str(), "wb"))
		fclose(f);
}

bool readText(const std::string &path, std::string &out)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	char chunk[4096];
	size_t n;
	out.clear();
	while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
		out.append(chunk, n);
	fclose(f);
	return true;
}

void doCover(Web &w, const std::string &name)
{
	const std::string base = w.folder + "/" + safeName(name);
	WebState state = WebState::Pending;
	Image picture;
	bool retry = false;
	if (exists(base + ".png")) {
		picture = decodeImage(base + ".png");
		state = picture.rgba.empty() ? WebState::None : WebState::Ready;
	} else if (settledNone(base + ".none")) {
		state = WebState::None;
	} else if ((double)time(nullptr) < w.offline_until) {
		retry = true;
	} else {
		const int status = psgopher_cover(name.c_str(), (base + ".png").c_str());
		if (status == 200) {
			picture = decodeImage(base + ".png");
			state = picture.rgba.empty() ? WebState::None : WebState::Ready;
			say("covers: %s from libretro (%dx%d)", name.c_str(), picture.width, picture.height);
		} else if (status == 404) {
			writeEmpty(base + ".none");
			state = WebState::None;
			say("covers: %s: libretro has none", name.c_str());
		} else {
			/* no answer: a minute's rest for the network, then asked again */
			w.offline_until = (double)time(nullptr) + (status == 429 ? 300 : 60);
			retry = true;
			say("covers: %s: %s: asking again in a minute", name.c_str(),
			    status <= 0 ? "no answer (the network?)" : ("HTTP " + std::to_string(status)).c_str());
		}
	}
	std::lock_guard lock(w.mutex);
	Entry &e = w.entries[name];
	if (retry) {
		e.cover_asked = false; // (asked again when it is next shown)
		return;
	}
	e.cover = state;
	e.picture = std::move(picture);
}

void doAbout(Web &w, const std::string &name)
{
	const std::string base = w.folder + "/about4-" + safeName(name); /* (about4: matched by title, any word order, of the N64's years) */
	std::string text;
	bool done = true;
	if (readText(base + ".txt", text)) {
	} else if (settledNone(base + ".none")) {
	} else if ((double)time(nullptr) < w.wiki_until) {
		done = false;
	} else {
		/* Wikipedia's search matches the name better without No-Intro's tags: "(Europe) (En,Fr,De)" */
		std::string plain = name.substr(0, name.find(" ("));
		static char buffer[16384];
		const int status = psgopher_about(plain.c_str(), buffer, sizeof(buffer));
		if (status == 200) {
			text = buffer;
			if (FILE *f = fopen((base + ".txt").c_str(), "wb")) {
				fwrite(text.data(), 1, text.size(), f);
				fclose(f);
			}
			say("about: %s: Wikipedia's \"%s\" (%zu characters)", name.c_str(), text.substr(0, text.find('\n')).c_str(), text.size());
		} else if (status == 404) {
			writeEmpty(base + ".none");
			say("about: %s: no Wikipedia article about the game", name.c_str());
		} else {
			done = false;
			w.wiki_until = (double)time(nullptr) + (status == 429 ? 300 : 60);
			say("about: %s: Wikipedia: %s", name.c_str(),
			    status <= 0 ? "no answer (the network?)" : ("HTTP " + std::to_string(status)).c_str());
		}
		/* Wikipedia's API asked politely: one lookup (two requests) every 1.5 s at most; at one
		 * every half second it answered 429 after about ten (boot logs 2026-11-25) */
		std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	}
	std::lock_guard lock(w.mutex);
	Entry &e = w.entries[name];
	if (!done) {
		e.about_asked = false;
		return;
	}
	e.about_done = true;
	e.about = text;
}

void run()
{
	Web &w = web();
	for (;;) {
		std::pair<std::string, bool> job;
		{
			std::unique_lock lock(w.mutex);
			w.cv.wait(lock, [&] { return !w.queue.empty(); });
			job = w.queue.front();
			w.queue.pop_front();
		}
		if (job.second)
			doCover(w, job.first);
		else
			doAbout(w, job.first);
	}
}

/* asked: to the front of the queue (what is on the screen now first); the queue kept short */
void ask(Web &w, const std::string &name, bool cover)
{
	w.queue.emplace_front(name, cover);
	while (w.queue.size() > 64) {
		Entry &e = w.entries[w.queue.back().first];
		(w.queue.back().second ? e.cover_asked : e.about_asked) = false;
		w.queue.pop_back();
	}
	w.cv.notify_one();
}

} // namespace

void webStart(const std::string &folder)
{
	Web &w = web();
	std::lock_guard lock(w.mutex);
	if (w.on)
		return;
	mkdir(folder.c_str(), 0777);
	/* the earlier descriptions go and are asked again: about- took any game's article the search
	 * offered (some were another game's), about2- missed titles with the words in another order,
	 * about3- took a later game of the same name */
	if (DIR *dir = opendir(folder.c_str())) {
		std::vector<std::string> old;
		while (struct dirent *e = readdir(dir))
			if (strncmp(e->d_name, "about-", 6) == 0 || strncmp(e->d_name, "about2-", 7) == 0 || strncmp(e->d_name, "about3-", 7) == 0)
				old.push_back(folder + "/" + e->d_name);
		closedir(dir);
		for (const std::string &path : old)
			unlink(path.c_str());
		if (!old.empty())
			say("about: %zu description(s) of an earlier matching let go: asked again", old.size());
	}
	w.folder = folder;
	w.on = true;
	std::thread(run).detach();
}

WebState webCover(const std::string &name, Image *picture)
{
	Web &w = web();
	std::lock_guard lock(w.mutex);
	if (!w.on || name.empty())
		return WebState::None;
	Entry &e = w.entries[name];
	if (e.cover == WebState::Pending && !e.cover_asked) {
		e.cover_asked = true;
		ask(w, name, true);
	}
	if (e.cover == WebState::Ready && !e.cover_taken) {
		e.cover_taken = true;
		if (picture)
			*picture = std::move(e.picture);
		e.picture = Image();
		return WebState::Ready;
	}
	return e.cover == WebState::Ready ? WebState::Pending : e.cover; // (handed over already: the caller keeps it)
}

void webForget(const std::string &name)
{
	Web &w = web();
	std::lock_guard lock(w.mutex);
	auto it = w.entries.find(name);
	if (it != w.entries.end() && it->second.cover == WebState::Ready) {
		it->second.cover = WebState::Pending;
		it->second.cover_asked = it->second.cover_taken = false;
	}
}

std::string webAbout(const std::string &name)
{
	Web &w = web();
	std::lock_guard lock(w.mutex);
	if (!w.on || name.empty())
		return std::string();
	Entry &e = w.entries[name];
	if (!e.about_done && !e.about_asked) {
		e.about_asked = true;
		ask(w, name, false);
	}
	return e.about;
}

} // namespace psgopher
