/*
 * PSGopher - host tests of the romhack patches (romhack.cpp): IPS, BPS and UPS, byte orders and
 * malformed patches. The game list's part is in test_library.cpp.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#include "romhack.h"
#include "romhack_encode.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace psgopher::romhack;

static int failures = 0;
#define CHECK(x)                                                                    \
	do {                                                                            \
		if (!(x)) {                                                                 \
			std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x);     \
			failures++;                                                             \
		}                                                                           \
	} while (0)

static bool run(const std::string &patch, const std::string &base, std::string &out, std::string *why = nullptr)
{
	std::vector<uint8_t> result;
	std::string error;
	const bool ok = apply((const uint8_t *)patch.data(), patch.size(), (const uint8_t *)base.data(), base.size(), result, &error);
	if (why)
		*why = error;
	out.assign(result.begin(), result.end());
	return ok;
}

/* a ROM-like image: .z64 magic then pseudo-random bytes */
static std::string rom(size_t size, unsigned seed = 1)
{
	std::string r(size, '\0');
	unsigned x = seed * 2654435761u + 12345u;
	for (size_t i = 0; i < size; i++) {
		x = x * 1103515245u + 12345u;
		r[i] = (char)(x >> 16);
	}
	r[0] = (char)0x80, r[1] = 0x37, r[2] = 0x12, r[3] = 0x40;
	return r;
}
static std::string asV64(std::string r)
{
	toZ64((uint8_t *)r.data(), r.size(), 1); /* (a swap of pairs is its own inverse) */
	return r;
}
static std::string asN64(std::string r)
{
	toZ64((uint8_t *)r.data(), r.size(), 2);
	return r;
}

int main()
{
	CHECK(crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
	CHECK(formatOfName("a.IPS") == Format::Ips && formatOfName("x.bps") == Format::Bps && formatOfName("x.Ups") == Format::Ups);
	CHECK(formatOfName("x.z64") == Format::None && formatOfName("ips") == Format::None && formatOfName("a.ipsx") == Format::None);

	const std::string base = rom(0x2000);
	std::string out, why;

	/* ---- IPS */
	{
		encode::Ips p;
		p.record(0x100, "HELLO");
		p.run(0x200, 300, 'Z');
		std::string want = base;
		want.replace(0x100, 5, "HELLO");
		want.replace(0x200, 300, std::string(300, 'Z'));
		CHECK(run(p.finish(), base, out) && out == want);
		/* past the end: the ROM grows */
		encode::Ips g;
		g.record(0x2000 + 4, "TAIL");
		CHECK(run(g.finish(), base, out) && out.size() == 0x2008 && out.substr(0x2004) == "TAIL" && out.substr(0x2000, 4) == std::string(4, '\0'));
		/* the truncation extension */
		CHECK(run(p.finish(0x1000), base, out) && out.size() == 0x1000 && out.substr(0x100, 5) == "HELLO");
		/* no records */
		CHECK(run(encode::Ips().finish(), base, out) && out == base);
		/* a patch made for .z64 applied to a .v64 and a .n64 base: the result is .z64 */
		CHECK(run(p.finish(), asV64(base), out) && out == want);
		CHECK(run(p.finish(), asN64(base), out) && out == want);
		/* malformed */
		const std::string good = p.finish();
		CHECK(!run("PATCH", base, out, &why));
		CHECK(!run("PATC", base, out));
		CHECK(!run(good.substr(0, 9), base, out, &why)); /* cut in a record's size */
		CHECK(!run(good.substr(0, 12), base, out, &why)); /* cut in a record's bytes */
		CHECK(!run(good.substr(0, 17), base, out)); /* cut in the run */
		CHECK(run(good + "xy", base, out, &why) && out.substr(0x100, 5) == "HELLO"); /* other bytes after the end are ignored */
		encode::Ips big;
		big.record(0xFFFFFF - 1, "the last bytes IPS can reach");
		CHECK(run(big.finish(), base, out) && out.size() == 0xFFFFFF - 1 + 28); /* 16 MiB: under the cap */
		encode::Ips empty_run;
		empty_run.data += std::string("\x00\x00\x10\x00\x00\x00\x00\x41", 8);
		CHECK(!run(empty_run.finish(), base, out, &why)); /* a run of nothing */
		CHECK(run(encode::Ips().finish(0), base, out) && out.empty());
		Info i;
		CHECK(info((const uint8_t *)good.data(), good.size(), &i, &why) && i.format == Format::Ips && !i.has_crc);
	}

	/* ---- BPS: SourceRead, TargetRead, SourceCopy, TargetCopy */
	{
		encode::Bps b(base);
		b.sourceRead(0x100);
		b.targetRead("NEW DATA HERE");
		b.sourceCopy(0x80, 0x1000);         /* from elsewhere in the source */
		b.targetCopy(40, 0x100);            /* from the target made already (the new data and on) */
		b.sourceCopy(0x20, 0x10);           /* a backward offset */
		b.targetRead("ab");
		b.targetCopy(12, b.target.size() - 2); /* over itself: "ab" repeated */
		b.sourceRead(base.size() - b.target.size() > 0 ? 0x20 : 1); /* (source at the same place) */
		const std::string patch = b.finish("metadata is skipped");
		CHECK(run(patch, base, out, &why) && out == b.target);
		CHECK(out.substr(0x100, 13) == "NEW DATA HERE");
		CHECK(out.substr(out.size() - 0x20 - 12, 12) == "abababababab");
		Info i;
		CHECK(info((const uint8_t *)patch.data(), patch.size(), &i, &why) && i.format == Format::Bps && i.has_crc && !i.either &&
			i.source_size == base.size() && i.source_crc == encode::crc(base) && i.target_size == b.target.size() &&
			i.target_crc == encode::crc(b.target));
		/* a base made for the other byte orders */
		CHECK(run(patch, asV64(base), out) && out == b.target);
		CHECK(run(patch, asN64(base), out) && out == b.target);
		/* failures */
		CHECK(!run(patch, base.substr(1), out, &why));
		std::string wrong = base;
		wrong[0x500] ^= 1;
		CHECK(!run(patch, wrong, out, &why) && why.find("CRC32") != std::string::npos);
		std::string damaged = patch;
		damaged[10] ^= 0x40; /* the patch's own CRC */
		CHECK(!run(damaged, base, out, &why) && why.find("damaged") != std::string::npos);
		damaged = patch;
		damaged[patch.size() - 9] ^= 1; /* the target CRC, patch CRC made right: the target check fails */
		CHECK(!run(encode::resealed(damaged), base, out, &why));
		CHECK(!run(patch.substr(0, patch.size() - 1), base, out));
		CHECK(!run("BPS1", base, out));
		/* a patch whose actions go outside: SourceRead past the source's end */
		encode::Bps past(base);
		past.sourceRead(base.size());
		past.body.clear(); /* then a read of one more byte than there is */
		encode::number(past.body, (base.size() + 1 - 1) << 2 | 0);
		past.target += 'x';
		CHECK(!run(past.finish(), base, out, &why));
		/* a SourceCopy from outside */
		encode::Bps outside(base);
		outside.body.clear();
		encode::number(outside.body, (0x10 - 1) << 2 | 2);
		encode::number(outside.body, (uint64_t)(base.size()) << 1); /* offset at the end: no room for 16 */
		outside.target = std::string(0x10, '\0');
		CHECK(!run(outside.finish(), base, out, &why));
		/* a TargetCopy from what is not made yet */
		encode::Bps ahead(base);
		ahead.body.clear();
		encode::number(ahead.body, (4 - 1) << 2 | 3);
		encode::number(ahead.body, 0); /* from 0: nothing is made yet */
		ahead.target = std::string(4, '\0');
		CHECK(!run(ahead.finish(), base, out, &why));
		/* more written than the target holds, and less */
		encode::Bps sm(base);
		sm.sourceRead(0x20);
		sm.target.resize(0x40, 'q'); /* it declares 0x40 and makes 0x20 */
		CHECK(!run(sm.finish(), base, out, &why));
		encode::Bps big(base);
		big.sourceRead(0x20);
		big.target.resize(0x10); /* it declares 0x10 and writes 0x20 */
		CHECK(!run(big.finish(), base, out, &why));
		/* a huge declared target (a size bomb) is refused before anything is made */
		{
			std::string bomb = "BPS1";
			encode::number(bomb, base.size());
			encode::number(bomb, (uint64_t)1 << 40);
			encode::number(bomb, 0);
			encode::le32(bomb, encode::crc(base));
			encode::le32(bomb, 0);
			encode::le32(bomb, encode::crc(bomb));
			CHECK(!run(bomb, base, out, &why) && why.find("too large") != std::string::npos);
		}
		/* a metadata size past the patch */
		{
			std::string meta = "BPS1";
			encode::number(meta, base.size());
			encode::number(meta, 4);
			encode::number(meta, 100000);
			encode::le32(meta, encode::crc(base));
			encode::le32(meta, 0);
			encode::le32(meta, encode::crc(meta));
			CHECK(!run(meta, base, out, &why));
		}
		/* a number that never ends */
		{
			std::string endless = "BPS1" + std::string(40, '\x01');
			encode::le32(endless, 0), encode::le32(endless, 0);
			encode::le32(endless, encode::crc(endless));
			CHECK(!run(endless, base, out, &why));
		}
	}
	/* a BPS made against a .v64 base: matched in that order, the result converted to .z64 */
	{
		const std::string v64 = asV64(base);
		encode::Bps b(v64);
		b.sourceRead(0x30);
		std::string patched = std::string("PATCHED!");
		b.targetRead(patched);
		b.sourceRead(v64.size() - b.target.size());
		std::string want = b.target; /* .v64 order */
		toZ64((uint8_t *)want.data(), want.size(), 1);
		CHECK(run(b.finish(), v64, out, &why) && out == want);
		/* the .z64 form of the base is tried too: this patch's source CRC is the .v64's, and a .z64 base's CRC is not it,
		 * but the .v64 conversion of a .z64 is not tried (only to .z64): so this is refused */
		CHECK(!run(b.finish(), base, out));
		CHECK(!run(b.finish(), base.substr(0, 100), out));
	}

	/* ---- UPS, both directions */
	{
		std::string patched = base;
		patched.replace(0x300, 7, "CHANGED");
		patched[0x1fff] ^= 0x55;
		patched += "extra tail"; /* a longer target */
		const std::string patch = encode::ups(base, patched);
		CHECK(run(patch, base, out, &why) && out == patched);
		CHECK(run(patch, patched, out, &why) && out == base); /* backwards: the target gives the source */
		Info i;
		CHECK(info((const uint8_t *)patch.data(), patch.size(), &i, &why) && i.format == Format::Ups && i.either && i.source_size == base.size() &&
			i.target_size == patched.size() && i.source_crc == encode::crc(base) && i.target_crc == encode::crc(patched));
		CHECK(run(patch, asV64(base), out) && out == patched);
		CHECK(run(patch, asN64(base), out) && out == patched);
		std::string other = base;
		other[5] ^= 1;
		CHECK(!run(patch, other, out, &why) && why.find("CRC32") != std::string::npos);
		CHECK(!run(patch, base.substr(0, 100), out));
		CHECK(!run(patch.substr(0, patch.size() - 2), base, out));
		std::string damaged = patch;
		damaged[8] ^= 1;
		CHECK(!run(damaged, base, out, &why) && why.find("damaged") != std::string::npos);
		/* a hunk past the end of both ROMs */
		std::string far = "UPS1";
		encode::number(far, base.size());
		encode::number(far, base.size());
		encode::number(far, base.size() + 100);
		far += '\x01';
		far += '\0';
		encode::le32(far, encode::crc(base));
		encode::le32(far, encode::crc(base));
		encode::le32(far, encode::crc(far));
		CHECK(!run(far, base, out, &why));
		/* a hunk with no closing zero */
		std::string open = "UPS1";
		encode::number(open, base.size());
		encode::number(open, base.size());
		encode::number(open, 3);
		open += "\x01\x02";
		encode::le32(open, encode::crc(base));
		encode::le32(open, encode::crc(base));
		encode::le32(open, encode::crc(open));
		CHECK(!run(open, base, out, &why));
		/* equal ROMs: a patch with no hunks */
		CHECK(run(encode::ups(base, base), base, out) && out == base);
		/* a bomb */
		std::string bomb = "UPS1";
		encode::number(bomb, base.size());
		encode::number(bomb, (uint64_t)1 << 40);
		encode::le32(bomb, encode::crc(base));
		encode::le32(bomb, 0);
		encode::le32(bomb, encode::crc(bomb));
		CHECK(!run(bomb, base, out, &why));
	}

	/* ---- byte order helpers */
	{
		const uint8_t z[4] = { 0x80, 0x37, 0x12, 0x40 }, v[4] = { 0x37, 0x80, 0x40, 0x12 }, n[4] = { 0x40, 0x12, 0x37, 0x80 }, x[4] = { 1, 2, 3, 4 };
		CHECK(byteOrder(z) == 0 && byteOrder(v) == 1 && byteOrder(n) == 2 && byteOrder(x) == -1);
		uint8_t odd[7] = { 0x37, 0x80, 0x40, 0x12, 0x55, 0x66, 0x77 };
		toZ64(odd, sizeof(odd), 1);
		CHECK(odd[0] == 0x80 && odd[1] == 0x37 && odd[4] == 0x66 && odd[5] == 0x55 && odd[6] == 0x77);
		uint8_t word[6] = { 0x40, 0x12, 0x37, 0x80, 9, 8 };
		toZ64(word, sizeof(word), 2);
		CHECK(word[0] == 0x80 && word[3] == 0x40 && word[4] == 9 && word[5] == 8);
		/* an unknown order is left alone (an IPS on a base with no header is applied as it is) */
		encode::Ips p;
		p.record(0, "A");
		CHECK(run(p.finish(), std::string("xyzw"), out) && out == "Ayzw");
		CHECK(!run("", base, out) && !run("NOPE", base, out, &why) && why.find("not an IPS") != std::string::npos);
	}

	if (failures) {
		std::fprintf(stderr, "test_romhack: %d check(s) failed\n", failures);
		return 1;
	}
	std::printf("test_romhack: all checks passed\n");
	return 0;
}
