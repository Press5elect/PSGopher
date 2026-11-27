/*
 * PSGopher - romhack patch files (romhack.h).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include "romhack.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace psgopher::romhack {

namespace {

bool fail(std::string *error, const std::string &text)
{
	if (error)
		*error = text;
	return false;
}

std::string hex8(uint32_t v)
{
	char text[16];
	snprintf(text, sizeof(text), "%08X", v);
	return text;
}

uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The bytes of a patch, read forward with every step checked against `end` */
struct Reader {
	const uint8_t *data;
	size_t pos, end;
	bool bad{ false };
	size_t left() const { return end - pos; }
	uint8_t byte()
	{
		if (pos >= end) {
			bad = true;
			return 0;
		}
		return data[pos++];
	}
	/* n bytes (at most 3), big-endian, read in order */
	size_t be(int n)
	{
		size_t x = 0;
		for (int i = 0; i < n; i++)
			x = x << 8 | byte();
		return x;
	}
	/* beat's variable-length number (BPS and UPS): seven bits a byte, the high bit ends it, and each
	 * continuation adds one; a number past 2^48 is a broken patch, not a size */
	uint64_t number()
	{
		uint64_t x = 0, shift = 1;
		for (;;) {
			const uint8_t b = byte();
			if (bad)
				return 0;
			x += (uint64_t)(b & 0x7f) * shift;
			if (b & 0x80)
				return x;
			shift <<= 7;
			x += shift;
			if (shift > (1ull << 48) || x > (1ull << 48)) {
				bad = true;
				return 0;
			}
		}
	}
};

uint32_t tableCrc(uint32_t crc, const uint8_t *data, size_t size)
{
	struct Table {
		uint32_t t[256];
	};
	static const Table table = [] { /* (a function-local static: made once, safely, whichever thread asks first) */
		Table made;
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			made.t[i] = c;
		}
		return made;
	}();
	crc = ~crc;
	for (size_t i = 0; i < size; i++)
		crc = table.t[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
	return ~crc;
}

} // namespace

const char *formatName(Format format)
{
	return format == Format::Ips ? "IPS" : format == Format::Bps ? "BPS" : format == Format::Ups ? "UPS" : "";
}

Format formatOfName(const std::string &name)
{
	if (name.size() < 4 || name[name.size() - 4] != '.')
		return Format::None;
	std::string e;
	for (size_t i = name.size() - 3; i < name.size(); i++)
		e += (char)std::tolower((unsigned char)name[i]);
	return e == "ips" ? Format::Ips : e == "bps" ? Format::Bps : e == "ups" ? Format::Ups : Format::None;
}

Format formatOfData(const uint8_t *patch, size_t size)
{
	if (size >= 5 && !memcmp(patch, "PATCH", 5))
		return Format::Ips;
	if (size >= 4 && !memcmp(patch, "BPS1", 4))
		return Format::Bps;
	if (size >= 4 && !memcmp(patch, "UPS1", 4))
		return Format::Ups;
	return Format::None;
}

uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc)
{
	return tableCrc(crc, data, size);
}

int byteOrder(const uint8_t *p)
{
	if (p[0] == 0x80 && p[1] == 0x37 && p[2] == 0x12 && p[3] == 0x40)
		return 0;
	if (p[0] == 0x37 && p[1] == 0x80 && p[2] == 0x40 && p[3] == 0x12)
		return 1;
	if (p[0] == 0x40 && p[1] == 0x12 && p[2] == 0x37 && p[3] == 0x80)
		return 2;
	return -1;
}

void toZ64(uint8_t *data, size_t size, int order)
{
	if (order == 1) {
		for (size_t i = 0; i + 1 < size; i += 2)
			std::swap(data[i], data[i + 1]);
	} else if (order == 2) {
		for (size_t i = 0; i + 3 < size; i += 4) {
			std::swap(data[i], data[i + 3]);
			std::swap(data[i + 1], data[i + 2]);
		}
	}
}

/* ------------------------------------------------------------------ IPS */

bool applyIps(const uint8_t *patch, size_t size, const uint8_t *base, size_t base_size, std::vector<uint8_t> &out,
	std::string *error)
{
	if (size < 8 || memcmp(patch, "PATCH", 5) != 0)
		return fail(error, "this is not an IPS patch");
	if (base_size > kMaxTarget)
		return fail(error, "the ROM is too large");
	out.assign(base, base + base_size);
	Reader r{ patch, 5, size };
	while (r.left() > 0) {
		if (r.left() < 3)
			return fail(error, "the IPS patch is cut short");
		const size_t offset = r.be(3);
		if (offset == 0x454F46) // "EOF"
			break;
		if (r.left() < 2)
			return fail(error, "the IPS patch is cut short");
		size_t length = r.be(2);
		bool run = false;
		uint8_t value = 0;
		if (length == 0) { // a run of one byte
			if (r.left() < 3)
				return fail(error, "the IPS patch is cut short");
			length = r.be(2);
			value = r.byte();
			run = true;
			if (length == 0)
				return fail(error, "the IPS patch has an empty run");
		} else if (r.left() < length)
			return fail(error, "the IPS patch is cut short");
		if (offset + length > kMaxTarget)
			return fail(error, "the patched ROM would be too large");
		if (offset + length > out.size())
			out.resize(offset + length, 0);
		if (run)
			memset(out.data() + offset, value, length);
		else {
			memcpy(out.data() + offset, patch + r.pos, length);
			r.pos += length;
		}
	}
	/* (a patch that ends at a record without the marker is taken as ended) */
	if (r.left() == 3) { // the truncation extension: the length the ROM is cut (or grown) to
		const size_t length = r.be(3);
		if (length > kMaxTarget)
			return fail(error, "the patched ROM would be too large");
		out.resize(length, 0);
	} /* (any other bytes after the end marker are ignored, as most patchers do) */
	return true;
}

/* ------------------------------------------------------------------ BPS */

namespace {

/* the sizes, metadata and checksums of a BPS patch; actions_end is where its actions stop */
struct Bps {
	uint64_t source_size, target_size;
	size_t actions_begin, actions_end;
	uint32_t source_crc, target_crc;
};

bool parseBps(const uint8_t *patch, size_t size, Bps *b, std::string *error)
{
	if (size < 4 + 3 + 12 || memcmp(patch, "BPS1", 4) != 0)
		return fail(error, "this is not a BPS patch");
	if (crc32(patch, size - 4) != le32(patch + size - 4))
		return fail(error, "the BPS patch is damaged (its checksum does not match)");
	Reader r{ patch, 4, size - 12 };
	b->source_size = r.number();
	b->target_size = r.number();
	const uint64_t metadata = r.number();
	if (r.bad || metadata > r.left())
		return fail(error, "the BPS patch is cut short");
	r.pos += (size_t)metadata;
	b->actions_begin = r.pos;
	b->actions_end = size - 12;
	b->source_crc = le32(patch + size - 12);
	b->target_crc = le32(patch + size - 8);
	return true;
}

} // namespace

bool applyBps(const uint8_t *patch, size_t size, const uint8_t *base, size_t base_size, std::vector<uint8_t> &out,
	std::string *error)
{
	Bps b;
	if (!parseBps(patch, size, &b, error))
		return false;
	if (b.target_size > kMaxTarget)
		return fail(error, "the patched ROM would be too large");
	if (b.source_size != base_size)
		return fail(error, "the base ROM is not the one this patch was made for (it is " + std::to_string(base_size) +
			" bytes, the patch wants " + std::to_string(b.source_size) + ")");
	if (crc32(base, base_size) != b.source_crc)
		return fail(error, "the base ROM is not the one this patch was made for (CRC32 " + hex8(crc32(base, base_size)) +
			", the patch wants " + hex8(b.source_crc) + ")");
	out.assign((size_t)b.target_size, 0);
	const size_t target = out.size();
	size_t at = 0;
	int64_t source_rel = 0, target_rel = 0;
	Reader r{ patch, b.actions_begin, b.actions_end };
	while (r.left() > 0) {
		const uint64_t data = r.number();
		if (r.bad)
			return fail(error, "the BPS patch is cut short");
		const uint64_t length = (data >> 2) + 1;
		if (length > target - at)
			return fail(error, "the BPS patch writes past the end of the ROM it makes");
		const size_t n = (size_t)length;
		switch (data & 3) {
		case 0: // SourceRead: the source at the same place
			if (at + n > base_size)
				return fail(error, "the BPS patch reads past the end of the base ROM");
			memcpy(out.data() + at, base + at, n);
			break;
		case 1: // TargetRead: bytes from the patch
			if (r.left() < n)
				return fail(error, "the BPS patch is cut short");
			memcpy(out.data() + at, patch + r.pos, n);
			r.pos += n;
			break;
		case 2:
		case 3: {
			const uint64_t x = r.number();
			if (r.bad)
				return fail(error, "the BPS patch is cut short");
			const int64_t delta = (x & 1) ? -(int64_t)(x >> 1) : (int64_t)(x >> 1);
			if ((data & 3) == 2) { // SourceCopy
				source_rel += delta;
				if (source_rel < 0 || (uint64_t)source_rel > base_size || n > base_size - (size_t)source_rel)
					return fail(error, "the BPS patch reads outside the base ROM");
				memcpy(out.data() + at, base + source_rel, n);
				source_rel += (int64_t)n;
			} else { // TargetCopy: what was written already (it may run over itself)
				target_rel += delta;
				if (target_rel < 0 || (uint64_t)target_rel >= at)
					return fail(error, "the BPS patch copies from outside what it has made");
				for (size_t i = 0; i < n; i++)
					out[at + i] = out[(size_t)target_rel + i];
				target_rel += (int64_t)n;
			}
			break;
		}
		}
		at += n;
	}
	if (at != target)
		return fail(error, "the BPS patch makes fewer bytes than it declares");
	if (crc32(out.data(), out.size()) != b.target_crc)
		return fail(error, "the patched ROM does not match the patch's checksum");
	return true;
}

/* ------------------------------------------------------------------ UPS */

namespace {

struct Ups {
	uint64_t in_size, out_size;
	size_t begin, end;
	uint32_t in_crc, out_crc;
};

bool parseUps(const uint8_t *patch, size_t size, Ups *u, std::string *error)
{
	if (size < 4 + 2 + 12 || memcmp(patch, "UPS1", 4) != 0)
		return fail(error, "this is not a UPS patch");
	if (crc32(patch, size - 4) != le32(patch + size - 4))
		return fail(error, "the UPS patch is damaged (its checksum does not match)");
	Reader r{ patch, 4, size - 12 };
	u->in_size = r.number();
	u->out_size = r.number();
	if (r.bad)
		return fail(error, "the UPS patch is cut short");
	u->begin = r.pos;
	u->end = size - 12;
	u->in_crc = le32(patch + size - 12);
	u->out_crc = le32(patch + size - 8);
	return true;
}

} // namespace

bool applyUps(const uint8_t *patch, size_t size, const uint8_t *base, size_t base_size, std::vector<uint8_t> &out,
	std::string *error)
{
	Ups u;
	if (!parseUps(patch, size, &u, error))
		return false;
	if (u.in_size > kMaxTarget || u.out_size > kMaxTarget)
		return fail(error, "the patched ROM would be too large");
	/* either direction: the base is the patch's input (forward) or its output (back) */
	const uint32_t crc = crc32(base, base_size);
	uint64_t to_size;
	uint32_t to_crc;
	if (base_size == u.in_size && crc == u.in_crc)
		to_size = u.out_size, to_crc = u.out_crc;
	else if (base_size == u.out_size && crc == u.out_crc)
		to_size = u.in_size, to_crc = u.in_crc;
	else
		return fail(error, "the base ROM is not the one this patch was made for (CRC32 " + hex8(crc) + ", the patch wants " +
			hex8(u.in_crc) + " or " + hex8(u.out_crc) + ")");
	const size_t length = (size_t)std::max<uint64_t>(u.in_size, u.out_size);
	std::vector<uint8_t> work(length, 0);
	memcpy(work.data(), base, std::min(base_size, length));
	Reader r{ patch, u.begin, u.end };
	size_t at = 0;
	while (r.left() > 0) {
		const uint64_t skip = r.number();
		if (r.bad)
			return fail(error, "the UPS patch is cut short");
		if (skip > length - std::min(at, length) && skip > 0)
			return fail(error, "the UPS patch writes outside the ROM");
		at += (size_t)skip;
		for (;;) {
			const uint8_t b = r.byte();
			if (r.bad)
				return fail(error, "the UPS patch is cut short");
			if (b != 0) {
				if (at >= length)
					return fail(error, "the UPS patch writes outside the ROM");
				work[at] ^= b;
			}
			at++; // (the closing zero byte takes a place too)
			if (b == 0)
				break;
		}
	}
	work.resize((size_t)to_size);
	if (crc32(work.data(), work.size()) != to_crc)
		return fail(error, "the patched ROM does not match the patch's checksum");
	out = std::move(work);
	return true;
}

/* ------------------------------------------------------------------ both byte orders */

bool info(const uint8_t *patch, size_t size, Info *out, std::string *error)
{
	Info i;
	i.format = formatOfData(patch, size);
	if (i.format == Format::Bps) {
		Bps b;
		if (!parseBps(patch, size, &b, error))
			return false;
		i.has_crc = true;
		i.source_size = b.source_size, i.source_crc = b.source_crc;
		i.target_size = b.target_size, i.target_crc = b.target_crc;
	} else if (i.format == Format::Ups) {
		Ups u;
		if (!parseUps(patch, size, &u, error))
			return false;
		i.has_crc = true, i.either = true;
		i.source_size = u.in_size, i.source_crc = u.in_crc;
		i.target_size = u.out_size, i.target_crc = u.out_crc;
	} else if (i.format != Format::Ips)
		return fail(error, "this is not an IPS, BPS or UPS patch");
	if (out)
		*out = i;
	return true;
}

bool apply(const uint8_t *patch, size_t patch_size, const uint8_t *base, size_t base_size, std::vector<uint8_t> &out,
	std::string *error)
{
	Info i;
	if (!info(patch, patch_size, &i, error))
		return false;
	const int order = base_size >= 4 ? byteOrder(base) : -1;
	std::vector<uint8_t> converted;
	const uint8_t *use = base;
	if (i.format == Format::Ips) {
		if (order > 0) { // (no checksum to say: a patch is made against .z64)
			converted.assign(base, base + base_size);
			toZ64(converted.data(), converted.size(), order);
			use = converted.data();
		}
		if (!applyIps(patch, patch_size, use, base_size, out, error))
			return false;
	} else {
		auto fits = [&](const uint8_t *data) {
			const uint32_t crc = crc32(data, base_size);
			return (base_size == i.source_size && crc == i.source_crc) ||
				(i.either && base_size == i.target_size && crc == i.target_crc);
		};
		if (!fits(use) && order > 0) { // as stored it does not fit: the .z64 order may
			converted.assign(base, base + base_size);
			toZ64(converted.data(), converted.size(), order);
			if (fits(converted.data()))
				use = converted.data();
		}
		/* (when neither fits, the format's own check says so, with the checksums) */
		if (!(i.format == Format::Bps ? applyBps : applyUps)(patch, patch_size, use, base_size, out, error))
			return false;
	}
	/* the result in .z64 order: a patch made against a .v64 gives a .v64 */
	if (out.size() >= 4)
		toZ64(out.data(), out.size(), byteOrder(out.data()));
	return true;
}

} // namespace psgopher::romhack
