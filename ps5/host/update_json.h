// PSGopher - a small JSON reader for the updater (update.cpp): GitHub's releases page and a
// title's param.json.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attributions "OGBox by Press5elect" and
// "Crux2 by Press5elect"; see NOTICE.
//
// PSGopher: from OGBox db63d46, which reads these with RapidJSON. PSGopher has no C++ JSON
// library and the updater reads a few members of two documents, so this is the whole of what it
// needs: a tree of values, nesting limited to 64, no extensions, the text read to its end.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace psgopher::update
{

struct JsonValue
{
	enum class Kind
	{
		Null,
		Bool,
		Number,
		String,
		Array,
		Object
	};
	Kind kind = Kind::Null;
	bool boolean = false;
	bool is_uint = false; // a number written as digits only that fits 64 bits
	uint64_t unsigned_value = 0;
	std::string text;
	std::vector<JsonValue> items;
	std::vector<std::pair<std::string, JsonValue>> members;

	bool IsObject() const { return kind == Kind::Object; }
	bool IsArray() const { return kind == Kind::Array; }
	bool IsString() const { return kind == Kind::String; }
	bool IsBool() const { return kind == Kind::Bool; }
	/// The member of an object (the first with that key), or nullptr.
	const JsonValue* Find(const char* key) const
	{
		if (kind != Kind::Object)
			return nullptr;
		for (const auto& member : members)
			if (member.first == key)
				return &member.second;
		return nullptr;
	}
};

namespace json_detail
{
class Reader
{
public:
	Reader(const char* data, size_t size) : m_at(data), m_end(data + size) {}

	bool Parse(JsonValue& out)
	{
		Space();
		if (!Value(out, 0))
			return false;
		Space();
		return m_at == m_end;
	}

private:
	static constexpr int MAX_DEPTH = 64;
	const char* m_at;
	const char* m_end;

	void Space()
	{
		while (m_at < m_end && (*m_at == ' ' || *m_at == '\t' || *m_at == '\n' || *m_at == '\r'))
			m_at++;
	}

	bool Literal(const char* word)
	{
		const char* p = m_at;
		for (; *word; word++, p++)
			if (p >= m_end || *p != *word)
				return false;
		m_at = p;
		return true;
	}

	static int Hex(char c)
	{
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	}

	bool Hex4(unsigned& out)
	{
		if (m_end - m_at < 4)
			return false;
		out = 0;
		for (int i = 0; i < 4; i++)
		{
			const int digit = Hex(m_at[i]);
			if (digit < 0)
				return false;
			out = out * 16 + static_cast<unsigned>(digit);
		}
		m_at += 4;
		return true;
	}

	static void Utf8(std::string& out, unsigned code)
	{
		if (code < 0x80)
			out += static_cast<char>(code);
		else if (code < 0x800)
		{
			out += static_cast<char>(0xc0 | (code >> 6));
			out += static_cast<char>(0x80 | (code & 0x3f));
		}
		else if (code < 0x10000)
		{
			out += static_cast<char>(0xe0 | (code >> 12));
			out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
			out += static_cast<char>(0x80 | (code & 0x3f));
		}
		else
		{
			out += static_cast<char>(0xf0 | (code >> 18));
			out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
			out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
			out += static_cast<char>(0x80 | (code & 0x3f));
		}
	}

	bool String(std::string& out)
	{
		if (m_at >= m_end || *m_at != '"')
			return false;
		m_at++;
		out.clear();
		while (m_at < m_end)
		{
			const unsigned char c = static_cast<unsigned char>(*m_at++);
			if (c == '"')
				return true;
			if (c < 0x20)
				return false;
			if (c != '\\')
			{
				out += static_cast<char>(c);
				continue;
			}
			if (m_at >= m_end)
				return false;
			const char e = *m_at++;
			switch (e)
			{
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
				{
					unsigned code;
					if (!Hex4(code))
						return false;
					if (code >= 0xd800 && code < 0xdc00)
					{
						unsigned low;
						if (m_end - m_at < 2 || m_at[0] != '\\' || m_at[1] != 'u')
							return false;
						m_at += 2;
						if (!Hex4(low) || low < 0xdc00 || low > 0xdfff)
							return false;
						code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
					}
					else if (code >= 0xdc00 && code < 0xe000)
						return false;
					Utf8(out, code);
					break;
				}
				default: return false;
			}
		}
		return false;
	}

	bool Number(JsonValue& out)
	{
		const char* start = m_at;
		const bool negative = m_at < m_end && *m_at == '-';
		if (negative)
			m_at++;
		if (m_at >= m_end || *m_at < '0' || *m_at > '9')
			return false;
		if (*m_at == '0')
			m_at++;
		else
			while (m_at < m_end && *m_at >= '0' && *m_at <= '9')
				m_at++;
		bool integer = true;
		if (m_at < m_end && *m_at == '.')
		{
			integer = false;
			m_at++;
			if (m_at >= m_end || *m_at < '0' || *m_at > '9')
				return false;
			while (m_at < m_end && *m_at >= '0' && *m_at <= '9')
				m_at++;
		}
		if (m_at < m_end && (*m_at == 'e' || *m_at == 'E'))
		{
			integer = false;
			m_at++;
			if (m_at < m_end && (*m_at == '+' || *m_at == '-'))
				m_at++;
			if (m_at >= m_end || *m_at < '0' || *m_at > '9')
				return false;
			while (m_at < m_end && *m_at >= '0' && *m_at <= '9')
				m_at++;
		}
		out.kind = JsonValue::Kind::Number;
		out.text.assign(start, m_at);
		if (integer && !negative)
		{
			uint64_t value = 0;
			bool fits = true;
			for (const char c : out.text)
			{
				const unsigned digit = static_cast<unsigned>(c - '0');
				if (value > (UINT64_MAX - digit) / 10)
				{
					fits = false;
					break;
				}
				value = value * 10 + digit;
			}
			out.is_uint = fits;
			out.unsigned_value = fits ? value : 0;
		}
		return true;
	}

	bool Value(JsonValue& out, int depth)
	{
		if (depth >= MAX_DEPTH || m_at >= m_end)
			return false;
		switch (*m_at)
		{
			case '{':
			{
				m_at++;
				out.kind = JsonValue::Kind::Object;
				Space();
				if (m_at < m_end && *m_at == '}')
				{
					m_at++;
					return true;
				}
				for (;;)
				{
					Space();
					std::string key;
					if (!String(key))
						return false;
					Space();
					if (m_at >= m_end || *m_at++ != ':')
						return false;
					Space();
					out.members.emplace_back(std::move(key), JsonValue());
					if (!Value(out.members.back().second, depth + 1))
						return false;
					Space();
					if (m_at >= m_end)
						return false;
					const char c = *m_at++;
					if (c == '}')
						return true;
					if (c != ',')
						return false;
				}
			}
			case '[':
			{
				m_at++;
				out.kind = JsonValue::Kind::Array;
				Space();
				if (m_at < m_end && *m_at == ']')
				{
					m_at++;
					return true;
				}
				for (;;)
				{
					Space();
					out.items.emplace_back();
					if (!Value(out.items.back(), depth + 1))
						return false;
					Space();
					if (m_at >= m_end)
						return false;
					const char c = *m_at++;
					if (c == ']')
						return true;
					if (c != ',')
						return false;
				}
			}
			case '"':
				out.kind = JsonValue::Kind::String;
				return String(out.text);
			case 't':
				out.kind = JsonValue::Kind::Bool;
				out.boolean = true;
				return Literal("true");
			case 'f':
				out.kind = JsonValue::Kind::Bool;
				out.boolean = false;
				return Literal("false");
			case 'n':
				out.kind = JsonValue::Kind::Null;
				return Literal("null");
			default:
				return Number(out);
		}
	}
};
} // namespace json_detail

/// Reads a whole JSON text. False when it is not one (or nests deeper than 64).
inline bool ParseJson(const char* data, size_t size, JsonValue& out)
{
	out = JsonValue();
	json_detail::Reader reader(data, size);
	if (reader.Parse(out))
		return true;
	out = JsonValue();
	return false;
}

} // namespace psgopher::update
