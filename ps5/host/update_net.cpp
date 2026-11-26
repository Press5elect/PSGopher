// PSGopher - the updater's network and set-up (update_net.h): HTTPS through the Rust core's rustls
// (vendor/gopher64/src/psgopher_http.rs), redirects followed to HTTPS only.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attributions "OGBox by Press5elect" and
// "Crux2 by Press5elect"; see NOTICE.
//
// PSGopher: from OGBox db63d46 (ps5/src/platform/ps5_update_net.cpp, curl and the title's
// certificates). The core has the HTTPS client the covers use (rustls, webpki-roots' certificates,
// IPv4), made to stream and to follow a redirect: a release's file is served from another host.

#include "update_net.h"
#include "update.h"
#include "platform.h"

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifndef PSGOPHER_VERSION
#define PSGOPHER_VERSION ""
#endif

extern "C"
{
	int32_t psgopher_http_get(const char* url, const char* accept, uint32_t seconds, uint8_t* buffer, size_t capacity,
		size_t* length);
	int32_t psgopher_http_download(const char* url, const char* path, uint64_t limit,
		int32_t (*progress)(uint64_t done, uint64_t total, void* user), void* user);
}

namespace psgopher::update
{
namespace
{

constexpr size_t MAX_ANSWER = (8u << 20) + 1; // the updater takes 8 MiB at most (and refuses a longer one)

int NetGet(const std::string& url, std::vector<uint8_t>& out, unsigned seconds)
{
	out.clear();
	std::unique_ptr<uint8_t[]> buffer(new uint8_t[MAX_ANSWER]);
	size_t length = 0;
	const char* accept = url.find("://api.github.com/") != std::string::npos ? "application/vnd.github+json" : "*/*";
	const int32_t code = psgopher_http_get(url.c_str(), accept, seconds, buffer.get(), MAX_ANSWER, &length);
	if (code == -3)
		say("update: an answer is longer than allowed");
	if (code < 0)
		return -1;
	out.assign(buffer.get(), buffer.get() + length);
	return code;
}

int32_t Progress(uint64_t done, uint64_t total, void* user)
{
	const auto* progress = static_cast<const std::function<bool(uint64_t, uint64_t)>*>(user);
	return progress && *progress && !(*progress)(done, total) ? 0 : 1;
}

int NetDownload(const std::string& url, const std::string& path, uint64_t limit,
	const std::function<bool(uint64_t, uint64_t)>& progress)
{
	const int32_t code = psgopher_http_download(url.c_str(), path.c_str(), limit, &Progress,
		const_cast<std::function<bool(uint64_t, uint64_t)>*>(&progress));
	if (code == -3)
		say("update: the download is longer than allowed");
	return code < 0 ? -1 : code;
}

bool Exists(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

void LogLine(const char* line)
{
	say("%s", line);
}

std::once_flag s_started;

} // namespace

bool RecoverTitle(const std::string& title_dir)
{
	return Recover(title_dir + "/", LogLine);
}

void StartTitle(const std::string& title_dir)
{
	std::call_once(s_started, [&] {
		Setup setup;
		setup.app_dir = title_dir;
		setup.version = PSGOPHER_VERSION; // "": a developer build
		setup.running_label = setup.version.empty() ? "developer build" : setup.version;
		setup.skip_file = "/data/psgopher/config/update-skipped.txt";
		setup.offer_developer = Exists("/data/psgopher/update-developer.txt");
		setup.http_get = NetGet;
		setup.http_download = NetDownload;
		setup.log = LogLine;
		Init(setup);
		say("update: the updater is ready (%s)",
			setup.version.empty() ? setup.offer_developer ? "a developer build: releases are offered" : "a developer build: no update is offered"
			                      : setup.version.c_str());
	});
}

} // namespace psgopher::update
