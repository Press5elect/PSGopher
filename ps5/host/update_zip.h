// PSGopher - a ZIP archive for the updater (update.cpp), read by the Rust core's `zip` crate.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attributions "OGBox by Press5elect" and
// "Crux2 by Press5elect"; see NOTICE.
//
// PSGopher: from OGBox db63d46 (the updater reads its ZIP with libzip there; PSGopher has no C
// ZIP library, and Gopher64 already links the `zip` crate, which reads its ROMs). The functions
// are vendor/gopher64/src/psgopher_zip.rs's (ps5.patch); the updater's host test links the same
// file (ps5/tests/update_zip).

#pragma once

#include <cstddef>
#include <cstdint>

extern "C"
{
	struct PsgopherZip;
	struct PsgopherZipReader;

	struct PsgopherZipEntry
	{
		uint64_t size;      // unpacked
		uint32_t unix_mode; // the archive's, when flags has PSGOPHER_ZIP_UNIX_MODE
		uint32_t flags;
	};
	constexpr uint32_t PSGOPHER_ZIP_UNIX_MODE = 1;
	constexpr uint32_t PSGOPHER_ZIP_ENCRYPTED = 2;
	constexpr uint32_t PSGOPHER_ZIP_UNREADABLE_METHOD = 4;

	PsgopherZip* psgopher_zip_open(const char* path, char* error, size_t error_size);
	int64_t psgopher_zip_count(const PsgopherZip* zip);
	// The name's length (the raw bytes, not NUL-ended; at most name_size are copied), or < 0.
	int64_t psgopher_zip_entry(const PsgopherZip* zip, uint64_t index, uint8_t* name, size_t name_size,
		PsgopherZipEntry* entry);
	PsgopherZipReader* psgopher_zip_read_open(PsgopherZip* zip, uint64_t index);
	int64_t psgopher_zip_read(PsgopherZipReader* reader, uint8_t* buffer, size_t size); // 0: the end, < 0: damaged
	int psgopher_zip_read_close(PsgopherZipReader* reader);
	int psgopher_zip_close(PsgopherZip* zip);
}
