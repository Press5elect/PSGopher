/*
 * PSGopher - romhack patch files: IPS, BPS (beat) and UPS applied to a ROM in memory.
 *
 * A patch file is untrusted data: every read and write is bounds checked, the patched ROM is at most
 * 256 MiB, and the BPS and UPS checksums (source, target, patch) are checked. A patch is made against
 * a ROM in one byte order (almost always .z64, big-endian): apply() tries the base as it is and then
 * converted to .z64 (from .v64: each 16-bit pair swapped; from .n64: each 32-bit word reversed), and
 * gives its result in .z64 order, which is what Gopher64 reads.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace psgopher::romhack {

constexpr size_t kMaxTarget = 256u << 20; // the largest patched ROM

enum class Format { None, Ips, Bps, Ups };
const char *formatName(Format format); // "IPS", "BPS", "UPS", ""

/* The format a file name's extension names (.ips .bps .ups, any case), or None */
Format formatOfName(const std::string &name);
/* The format the patch's own first bytes name, or None */
Format formatOfData(const uint8_t *patch, size_t size);

uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc = 0); // crc: what came before (0 at the start)

/* The byte order of a ROM by its first four bytes: 0 .z64 (80 37 12 40), 1 .v64 (37 80 40 12),
 * 2 .n64 (40 12 37 80), -1 none of them. */
int byteOrder(const uint8_t *first4);
/* ROM bytes of the given order (as byteOrder says) to .z64 order, in place; a trailing part short of a
 * pair (v64) or a word (n64) stays. For a ROM read in blocks, give blocks of a multiple of 4 bytes. */
void toZ64(uint8_t *data, size_t size, int order);

/* What a patch declares, read without applying it (the patch's own CRC32 is checked) */
struct Info {
	Format format{ Format::None };
	bool has_crc{ false };      // BPS and UPS; IPS declares neither size nor checksum
	uint64_t source_size{ 0 };  // the size of the ROM it was made against
	uint32_t source_crc{ 0 };
	uint64_t target_size{ 0 };  // the size of the ROM it makes
	uint32_t target_crc{ 0 };
	bool either{ false };       // UPS: it applies in both directions (the base may be the source or the target)
};
bool info(const uint8_t *patch, size_t size, Info *out, std::string *error);

/* The patch applied to `base` (a ROM as stored: any byte order). The result is in .z64 order. Returns
 * false with *error set (a plain sentence) when the patch is malformed, its checksums do not fit, or
 * the result would pass kMaxTarget. */
bool apply(const uint8_t *patch, size_t patch_size, const uint8_t *base, size_t base_size, std::vector<uint8_t> &out,
	std::string *error);

/* Only the first `limit` bytes of what apply() would give (fewer when the result is shorter), in .z64 order,
 * without making the whole patched ROM or a converted copy of the base: for a ROM's header. The base must fit
 * the patch (the source CRC32 is checked, as stored or in .z64 order) and the patch's own checksum is checked;
 * the target's CRC32 and the patch's later actions are not (apply() does that, when the game starts).
 * `limit` should be a multiple of 4 (the head is put in .z64 order by words and pairs). */
bool applyHead(const uint8_t *patch, size_t patch_size, const uint8_t *base, size_t base_size, size_t limit,
	std::vector<uint8_t> &out, std::string *error);

/* The formats alone, on the bytes as given (no byte order tried; BPS and UPS check the checksums) */
bool applyIps(const uint8_t *patch, size_t patch_size, const uint8_t *base, size_t base_size, std::vector<uint8_t> &out,
	std::string *error);
bool applyBps(const uint8_t *patch, size_t patch_size, const uint8_t *base, size_t base_size, std::vector<uint8_t> &out,
	std::string *error);
bool applyUps(const uint8_t *patch, size_t patch_size, const uint8_t *base, size_t base_size, std::vector<uint8_t> &out,
	std::string *error);

} // namespace psgopher::romhack
