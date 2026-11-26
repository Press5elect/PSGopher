// PSGopher - the updater's set-up (update.h) for PSGopher: its releases on GitHub
// (Press5elect/PSGopher, "PSGopher-v<version>.zip"), asked over HTTPS by the Rust core's rustls.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attributions "OGBox by Press5elect" and
// "Crux2 by Press5elect"; see NOTICE.
//
// PSGopher: from OGBox db63d46 (ps5/src/platform/ps5_update_net.h, which asks with curl). The
// steps are update.h's (Check, Download, Install, GetStatus); these two set them up.

#pragma once

#include <string>

namespace psgopher::update
{

/// Once at start-up, before the title's files are read: an install a power cut stopped is undone,
/// and what an earlier update left in <title folder>/.update/ is removed. True when one was undone.
bool RecoverTitle(const std::string& title_dir);

/// Gives the updater the title folder, this build's version (PSGOPHER_VERSION, "v1.0.0"; a build
/// made without one is a developer build, which is never offered an update unless
/// /data/psgopher/update-developer.txt exists) and the core's HTTPS. Asks nothing yet; once is
/// enough (later calls do nothing).
void StartTitle(const std::string& title_dir);

} // namespace psgopher::update
