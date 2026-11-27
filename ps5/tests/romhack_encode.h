/*
 * PSGopher - small encoders for IPS, BPS and UPS, for the host tests of romhack.cpp and the game list.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#pragma once

#include <cstdint>
#include <string>

#include "romhack.h"

namespace encode {

inline uint32_t crc(const std::string &s)
{
	return psgopher::romhack::crc32((const uint8_t *)s.data(), s.size());
}

inline void le32(std::string &o, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		o += (char)(v >> (8 * i));
}

/* beat's variable-length number */
inline void number(std::string &o, uint64_t x)
{
	for (;;) {
		const uint8_t b = x & 0x7f;
		x >>= 7;
		if (x == 0) {
			o += (char)(b | 0x80);
			return;
		}
		o += (char)b;
		x--;
	}
}

/* IPS: records appended, then the end */
struct Ips {
	std::string data = "PATCH";
	void record(uint32_t offset, const std::string &bytes)
	{
		data += (char)(offset >> 16), data += (char)(offset >> 8), data += (char)offset;
		data += (char)(bytes.size() >> 8), data += (char)bytes.size();
		data += bytes;
	}
	void run(uint32_t offset, uint16_t length, char value)
	{
		data += (char)(offset >> 16), data += (char)(offset >> 8), data += (char)offset;
		data += std::string(2, '\0');
		data += (char)(length >> 8), data += (char)length, data += value;
	}
	std::string finish() const { return data + "EOF"; }
	std::string finish(uint32_t truncate) const
	{
		return data + "EOF" + (char)(truncate >> 16) + (char)(truncate >> 8) + (char)truncate;
	}
};

/* BPS: the actions, with the target they make kept beside */
struct Bps {
	std::string source, target, body;
	int64_t source_rel = 0, target_rel = 0;
	explicit Bps(const std::string &src) : source(src) {}
	void sourceRead(size_t n)
	{
		number(body, (n - 1) << 2 | 0);
		target += source.substr(target.size(), n);
	}
	void targetRead(const std::string &bytes)
	{
		number(body, (bytes.size() - 1) << 2 | 1);
		body += bytes;
		target += bytes;
	}
	void sourceCopy(size_t n, size_t from)
	{
		number(body, (n - 1) << 2 | 2);
		const int64_t delta = (int64_t)from - source_rel;
		number(body, (uint64_t)(delta < 0 ? -delta : delta) << 1 | (delta < 0));
		source_rel = (int64_t)(from + n);
		target += source.substr(from, n);
	}
	void targetCopy(size_t n, size_t from)
	{
		number(body, (n - 1) << 2 | 3);
		const int64_t delta = (int64_t)from - target_rel;
		number(body, (uint64_t)(delta < 0 ? -delta : delta) << 1 | (delta < 0));
		target_rel = (int64_t)(from + n);
		for (size_t i = 0; i < n; i++)
			target += target[from + i];
	}
	std::string finish(const std::string &metadata = "") const
	{
		std::string o = "BPS1";
		number(o, source.size());
		number(o, target.size());
		number(o, metadata.size());
		o += metadata;
		o += body;
		le32(o, crc(source));
		le32(o, crc(target));
		le32(o, crc(o));
		return o;
	}
};

/* UPS: the XOR of in and out where they differ */
inline std::string ups(const std::string &in, const std::string &out)
{
	std::string o = "UPS1";
	number(o, in.size());
	number(o, out.size());
	const size_t n = std::max(in.size(), out.size());
	auto at = [](const std::string &s, size_t i) { return i < s.size() ? (uint8_t)s[i] : (uint8_t)0; };
	size_t last = 0;
	for (size_t i = 0; i < n;) {
		if (at(in, i) == at(out, i)) {
			i++;
			continue;
		}
		number(o, i - last);
		while (i < n && at(in, i) != at(out, i))
			o += (char)(at(in, i) ^ at(out, i)), i++;
		o += '\0';
		last = i + 1;
		i++;
	}
	le32(o, crc(in));
	le32(o, crc(out));
	le32(o, crc(o));
	return o;
}

/* a patch's last four bytes made right again after its body was changed (a damaged patch the checks
 * past the patch's own CRC must catch) */
inline std::string resealed(std::string patch)
{
	patch.resize(patch.size() - 4);
	le32(patch, crc(patch));
	return patch;
}

} // namespace encode
