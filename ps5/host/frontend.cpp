/*
 * PSGopher - the front end, the Live Board (frontend.h). Measures, colours, type and motion are the
 * design's (ps5/art/live-board.html), in its stage pixels.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "frontend.h"

#include "imgui_internal.h" /* ImTextCharFromUtf8 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <set>

#include <dirent.h>
#include <sys/stat.h>

extern "C" {
#include "platform.h"
}

namespace psgopher {

namespace {

/* the design's colours */
const ImU32 kText = IM_COL32(0xe6, 0xf4, 0xf2, 255);
const ImU32 kDim = IM_COL32(0xa3, 0xc2, 0xbe, 255);
const ImU32 kLine = IM_COL32(140, 230, 220, 61);
const ImU32 kPanel = IM_COL32(7, 17, 21, 219);
const ImU32 kCellBg = IM_COL32(0x07, 0x12, 0x16, 255);
const ImU32 kAmber = IM_COL32(0xff, 0xb6, 0x48, 255);
const ImU32 kInk = IM_COL32(0x03, 0x10, 0x14, 255);

ImU32 withAlpha(ImU32 c, float a)
{
	const int base = (int)((c >> IM_COL32_A_SHIFT) & 0xff);
	return (c & ~IM_COL32_A_MASK) | ((ImU32)std::clamp((int)(base * a), 0, 255) << IM_COL32_A_SHIFT);
}

/* the stage's units for drawing helpers */
struct Ink {
	ImDrawList *dl;
	float u;
	ImVec2 at(float x, float y) const { return ImVec2(x * u, y * u); }
	/* text with the design's letter-spacing (em): drawn glyph by glyph when spaced */
	float text(ImFont *font, float size, float x, float y, ImU32 col, const std::string &s, float spacing = 0) const
	{
		if (!font || s.empty())
			return 0;
		if (spacing == 0) {
			dl->AddText(font, size * u, at(x, y), col, s.c_str());
			return font->CalcTextSizeA(size * u, FLT_MAX, 0, s.c_str()).x / u;
		}
		float cx = x;
		const char *p = s.c_str(), *end = p + s.size();
		while (p < end) {
			unsigned c = 0;
			const int n = ImTextCharFromUtf8(&c, p, end);
			const std::string ch(p, p + std::max(1, n));
			dl->AddText(font, size * u, at(cx, y), col, ch.c_str());
			cx += font->CalcTextSizeA(size * u, FLT_MAX, 0, ch.c_str()).x / u + spacing * size;
			p += std::max(1, n);
		}
		return cx - x - spacing * size;
	}
	float width(ImFont *font, float size, const std::string &s, float spacing = 0) const
	{
		if (!font || s.empty())
			return 0;
		float w = font->CalcTextSizeA(size * u, FLT_MAX, 0, s.c_str()).x / u;
		if (spacing != 0) {
			int glyphs = 0;
			for (const char *p = s.c_str(); *p;) {
				unsigned c = 0;
				p += std::max(1, ImTextCharFromUtf8(&c, p, nullptr));
				glyphs++;
			}
			w += spacing * size * (glyphs - 1);
		}
		return w;
	}
	void rect(float x, float y, float w, float h, ImU32 col, float r = 0) const { dl->AddRectFilled(at(x, y), at(x + w, y + h), col, r * u); }
	void frame(float x, float y, float w, float h, ImU32 col, float r = 0, float t = 1) const
	{
		dl->AddRect(at(x, y), at(x + w, y + h), col, r * u, t * u);
	}
	/* a soft glow around a rectangle (the design's drop-shadow) */
	void glow(float x, float y, float w, float h, ImU32 col, float spread, float r) const
	{
		for (int i = 6; i >= 1; i--) {
			const float g = spread * i / 6;
			dl->AddRectFilled(at(x - g, y - g), at(x + w + g, y + h + g), withAlpha(col, 0.05f), (r + g) * u);
		}
	}
};

/* "41 h played", "25 min played" */
std::string playedText(double seconds)
{
	char t[48];
	if (seconds >= 3600)
		snprintf(t, sizeof(t), "%.0f h played", seconds / 3600);
	else if (seconds >= 60)
		snprintf(t, sizeof(t), "%.0f min played", seconds / 60);
	else
		snprintf(t, sizeof(t), "never played");
	return t;
}

/* "today 21:08", "yesterday", "Sunday 16:40", "2 weeks ago", "12 Nov" */
std::string agoText(int64_t when, bool with_time)
{
	if (when <= 0)
		return "never";
	const time_t now = time(nullptr), t = (time_t)when;
	struct tm a = {}, b = {};
	localtime_r(&t, &a);
	localtime_r(&now, &b);
	char hm[16], text[48];
	strftime(hm, sizeof(hm), "%H:%M", &a);
	const long days = (long)((now - t) / 86400);
	if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year)
		snprintf(text, sizeof(text), with_time ? "today %s" : "today", hm);
	else if (days <= 1)
		snprintf(text, sizeof(text), with_time ? "yesterday %s" : "yesterday", hm);
	else if (days < 7) {
		char day[16];
		strftime(day, sizeof(day), "%A", &a);
		snprintf(text, sizeof(text), with_time ? "%s %s" : "%s", day, hm);
	} else if (days < 35)
		snprintf(text, sizeof(text), "%ld weeks ago", days / 7);
	else
		strftime(text, sizeof(text), "%d %b %Y", &a);
	return text;
}

/* the name a person reads: the file's name without its tags at the end ("(USA)", "[!]") */
std::string titleOf(const Game &g)
{
	std::string s = g.name;
	for (;;) {
		while (!s.empty() && s.back() == ' ')
			s.pop_back();
		if (s.size() > 2 && (s.back() == ')' || s.back() == ']')) {
			const char open = s.back() == ')' ? '(' : '[';
			const size_t at = s.rfind(open);
			if (at == std::string::npos || at == 0)
				break;
			s.erase(at);
		} else
			break;
	}
	return s.empty() ? g.name : s;
}

char letterOf(const Game &g)
{
	const std::string t = titleOf(g);
	for (const char c : t)
		if (std::isalpha((unsigned char)c))
			return (char)std::toupper((unsigned char)c);
		else if (std::isdigit((unsigned char)c))
			return '#';
	return '#';
}

std::string where(const Game &g)
{
	return g.tab == TabUsb ? "USB drive" : g.tab == TabNetwork ? "Network share" : "This console";
}

/* the game's region: the header's letter, else the file name's tag ("(USA)", "(Europe)"...) */
char regionOf(const Game &g)
{
	if (g.id.size() >= 4 && !regionText(g.id).empty())
		return g.id[3];
	static const struct {
		const char *tag;
		char letter;
	} tags[] = { { "(USA", 'E' }, { "(U)", 'E' }, { "(Europe", 'P' }, { "(E)", 'P' }, { "(Japan", 'J' }, { "(J)", 'J' },
		         { "(Germany", 'D' }, { "(France", 'F' }, { "(Italy", 'I' }, { "(Spain", 'S' }, { "(Australia", 'U' },
		         { "(Brazil", 'B' }, { "(Korea", 'K' }, { "(China", 'C' }, { "(Asia", 'A' } };
	for (const auto &t : tags)
		if (g.name.find(t.tag) != std::string::npos)
			return t.letter;
	return 0;
}

std::string regionName(char letter)
{
	const char id[5] = { 'N', 'X', 'X', letter, 0 };
	return letter ? regionText(id) : std::string();
}

/* the region's flag, drawn (stage units: x, y, w; 3:2) */
void flag(const Ink &ink, char region, float x, float y, float w)
{
	const float h = w * 2 / 3;
	ImDrawList *dl = ink.dl;
	const float u = ink.u;
	auto band = [&](float x0, float y0, float x1, float y1, ImU32 c) { dl->AddRectFilled(ink.at(x + x0 * w, y + y0 * h), ink.at(x + x1 * w, y + y1 * h), c); };
	auto star = [&](float cx, float cy, float r, ImU32 c) {
		ImVec2 p[10];
		for (int i = 0; i < 10; i++) {
			const float a = -1.5708f + i * 3.14159265f / 5, rr = (i % 2 ? r * .42f : r);
			p[i] = ink.at(cx + std::cos(a) * rr, cy + std::sin(a) * rr);
		}
		dl->AddConcavePolyFilled(p, 10, c);
	};
	const ImU32 white = IM_COL32(246, 246, 246, 255), red = IM_COL32(0xd2, 0x21, 0x2b, 255), blue = IM_COL32(0x1f, 0x3a, 0x93, 255);
	switch (region) {
	case 'E': /* the United States: stripes and the canton */
		for (int i = 0; i < 7; i++)
			band(0, i * 2 / 13.0f, 1, (i * 2 + 1) / 13.0f, red);
		for (int i = 0; i < 6; i++)
			band(0, (i * 2 + 1) / 13.0f, 1, (i * 2 + 2) / 13.0f, white);
		band(0, 0, .4f, 7 / 13.0f, IM_COL32(0x3c, 0x3b, 0x6e, 255));
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 4; c++)
				dl->AddCircleFilled(ink.at(x + (.06f + c * .095f) * w, y + (.09f + r * .17f) * h), .02f * w * u, white);
		break;
	case 'J':
		band(0, 0, 1, 1, white);
		dl->AddCircleFilled(ink.at(x + w / 2, y + h / 2), h * .3f * u, IM_COL32(0xbc, 0x00, 0x2d, 255), 32);
		break;
	case 'P': case 'X': case 'Y': /* Europe: twelve stars in a circle */
		band(0, 0, 1, 1, IM_COL32(0x00, 0x33, 0x99, 255));
		for (int i = 0; i < 12; i++) {
			const float a = i * 3.14159265f / 6;
			star(x + w / 2 + std::cos(a) * h * .33f, y + h / 2 + std::sin(a) * h * .33f, h * .06f, IM_COL32(0xff, 0xcc, 0x00, 255));
		}
		break;
	case 'D':
		band(0, 0, 1, 1 / 3.0f, IM_COL32(0, 0, 0, 255));
		band(0, 1 / 3.0f, 1, 2 / 3.0f, IM_COL32(0xdd, 0x00, 0x00, 255));
		band(0, 2 / 3.0f, 1, 1, IM_COL32(0xff, 0xce, 0x00, 255));
		break;
	case 'F':
		band(0, 0, 1 / 3.0f, 1, IM_COL32(0x00, 0x23, 0x95, 255));
		band(1 / 3.0f, 0, 2 / 3.0f, 1, white);
		band(2 / 3.0f, 0, 1, 1, IM_COL32(0xed, 0x29, 0x39, 255));
		break;
	case 'I':
		band(0, 0, 1 / 3.0f, 1, IM_COL32(0x00, 0x92, 0x46, 255));
		band(1 / 3.0f, 0, 2 / 3.0f, 1, white);
		band(2 / 3.0f, 0, 1, 1, IM_COL32(0xce, 0x2b, 0x37, 255));
		break;
	case 'S':
		band(0, 0, 1, .25f, IM_COL32(0xaa, 0x15, 0x1b, 255));
		band(0, .25f, 1, .75f, IM_COL32(0xf1, 0xbf, 0x00, 255));
		band(0, .75f, 1, 1, IM_COL32(0xaa, 0x15, 0x1b, 255));
		break;
	case 'U': /* Australia: the field and the Southern Cross */
		band(0, 0, 1, 1, IM_COL32(0x01, 0x21, 0x69, 255));
		band(0, 0, .5f, .5f, IM_COL32(0x01, 0x21, 0x69, 255));
		dl->AddLine(ink.at(x, y), ink.at(x + w * .5f, y + h * .5f), white, .05f * w * u);
		dl->AddLine(ink.at(x + w * .5f, y), ink.at(x, y + h * .5f), white, .05f * w * u);
		band(.22f, 0, .28f, .5f, red);
		band(0, .22f, .5f, .28f, red);
		star(x + w * .25f, y + h * .75f, h * .1f, white);
		for (const auto &p : { ImVec2(.75f, .2f), ImVec2(.62f, .48f), ImVec2(.88f, .42f), ImVec2(.75f, .8f) })
			star(x + w * p.x, y + h * p.y, h * .06f, white);
		break;
	case 'B':
		band(0, 0, 1, 1, IM_COL32(0x00, 0x9c, 0x3b, 255));
		{
			const ImVec2 d[4] = { ink.at(x + w * .5f, y + h * .1f), ink.at(x + w * .92f, y + h * .5f), ink.at(x + w * .5f, y + h * .9f),
				                  ink.at(x + w * .08f, y + h * .5f) };
			dl->AddConvexPolyFilled(d, 4, IM_COL32(0xff, 0xdf, 0x00, 255));
		}
		dl->AddCircleFilled(ink.at(x + w / 2, y + h / 2), h * .22f * u, IM_COL32(0x00, 0x27, 0x76, 255), 24);
		break;
	case 'K':
		band(0, 0, 1, 1, white);
		dl->PathArcTo(ink.at(x + w / 2, y + h / 2), h * .25f * u, 3.14159265f, 2 * 3.14159265f, 16);
		dl->PathFillConvex(IM_COL32(0xcd, 0x2e, 0x3a, 255));
		dl->PathArcTo(ink.at(x + w / 2, y + h / 2), h * .25f * u, 0, 3.14159265f, 16);
		dl->PathFillConvex(IM_COL32(0x00, 0x47, 0xa0, 255));
		break;
	case 'C':
		band(0, 0, 1, 1, IM_COL32(0xee, 0x1c, 0x25, 255));
		star(x + w * .2f, y + h * .28f, h * .17f, IM_COL32(0xff, 0xff, 0x00, 255));
		break;
	default:
		return;
	}
	dl->AddRect(ink.at(x, y), ink.at(x + w, y + h), IM_COL32(255, 255, 255, 60), 2 * u, 1 * u);
}

/* the design's text-wrap: words to lines of at most `width` */
std::vector<std::string> wrap(const Ink &ink, ImFont *font, float size, const std::string &s, float width, int max_lines)
{
	std::vector<std::string> lines;
	std::string line;
	size_t at = 0;
	while (at < s.size()) {
		size_t end = s.find(' ', at);
		if (end == std::string::npos)
			end = s.size();
		const std::string word = s.substr(at, end - at);
		const std::string tried = line.empty() ? word : line + " " + word;
		if (!line.empty() && ink.width(font, size, tried) > width) {
			lines.push_back(line);
			line = word;
		} else
			line = tried;
		at = end + 1;
	}
	if (!line.empty())
		lines.push_back(line);
	if ((int)lines.size() > max_lines) {
		lines.resize(max_lines);
		lines.back() += "\xE2\x80\xA6";
	}
	return lines;
}

/* a probe's picture: its save state's bytes as pixels (56 x 21), in the game's hues */
void fingerprint(const Ink &ink, const std::string &path, const board::Look &look, float x, float y, float w, float h)
{
	static std::map<std::string, std::vector<uint8_t>> cache;
	auto &bytes = cache[path];
	if (bytes.empty()) {
		bytes.assign(56 * 21, 0);
		if (FILE *f = fopen(path.c_str(), "rb")) {
			fseek(f, 4096, SEEK_SET);
			if (fread(bytes.data(), 1, bytes.size(), f) < bytes.size())
				fseek(f, 0, SEEK_SET), (void)!fread(bytes.data(), 1, bytes.size(), f);
			fclose(f);
		}
	}
	const float pw = w / 56, ph = h / 21;
	for (int i = 0; i < 56; i++)
		for (int j = 0; j < 21; j++) {
			const uint8_t b = bytes[j * 56 + i];
			const float v = b / 255.0f;
			const ImU32 c = b < 24 ? IM_COL32(0, 0, 0, 255)
			                       : board::hsl(look.hue + (b >> 5) * 47, .5f + v * .4f, .10f + v * (i % 9 < 3 ? .6f : .3f));
			ink.rect(x + i * pw, y + j * ph, pw + .3f, ph + .3f, c);
		}
	ink.frame(x, y, w, h, kLine, 5, 1);
}

} // namespace

bool PadInput::repeat(uint32_t button) const
{
	const int bit = __builtin_ctz(button);
	if (pressed & button)
		return true;
	auto *self = const_cast<PadInput *>(this);
	if ((held & button) && now - held_since[bit] > 0.38f && now - last_repeat[bit] > 0.09f) {
		self->last_repeat[bit] = now;
		return true;
	}
	return false;
}

Frontend::Frontend(FrontendHost host) : host_(std::move(host))
{
	refresh();
}

void Frontend::setView(board::View v)
{
	if (v == view_)
		return;
	from_ = view_;
	if (v == board::View::Bench && view_ != board::View::Bench)
		rack_chosen_ = view_ == board::View::Rack;
	view_ = v;
	blend_ = 0;
}

/* a game's page: its file's settings, BENCH's place kept and the pose of BENCH's board taken */
void Frontend::openGamePage(const Game &g)
{
	game_page_ = true;
	page_game_ = g;
	page_key_ = gameKey(g);
	page_overrides_ = loadGameSettings(page_key_);
	page_saved_section_ = bench_section_, page_saved_sel_ = bench_sel_;
	bench_section_ = bench_sel_ = 0, bench_scroll_ = 0;
	setView(board::View::Bench);
}

const GameSettings &Frontend::markersOf(const Game &g)
{
	if (g.path != marker_path_ || marker_version_ != version_ || marker_key_.empty()) {
		marker_path_ = g.path, marker_version_ = version_;
		marker_key_ = gameKey(g);
		marker_overrides_ = loadGameSettings(marker_key_);
	}
	return marker_overrides_;
}

/* the games, and the play time and probes they have */
void Frontend::refresh()
{
	unsigned version = 0;
	std::vector<Game> games = library().games(&version);
	if (version == version_)
		return;
	const std::string chosen = selected() ? selected()->path : std::string();
	version_ = version;
	games_ = std::move(games);
	played_.assign(games_.size(), {});
	const auto all = playedAll();
	std::map<std::string, ::psgopher::Played> by_name(all.begin(), all.end());
	for (size_t i = 0; i < games_.size(); i++) {
		const auto it = by_name.find(games_[i].name);
		if (it != by_name.end())
			played_[i] = { it->second.seconds, it->second.last };
	}
	/* the strip: recently played first (newest), then every other by name */
	strip_.clear();
	for (size_t i = 0; i < games_.size(); i++)
		if (played_[i].last > 0)
			strip_.push_back((int)i);
	std::sort(strip_.begin(), strip_.end(), [this](int a, int b) { return played_[a].last > played_[b].last; });
	for (size_t i = 0; i < games_.size(); i++)
		if (played_[i].last == 0)
			strip_.push_back((int)i);
	order();
	strip_sel_ = 0;
	for (size_t k = 0; k < strip_.size(); k++)
		if (games_[strip_[k]].path == chosen)
			strip_sel_ = (int)k;
}

/* the rack's order and what its filter shows */
void Frontend::order()
{
	rack_.resize(games_.size());
	for (size_t i = 0; i < games_.size(); i++)
		rack_[i] = (int)i;
	auto byName = [this](int a, int b) {
		const char la = letterOf(games_[a]), lb = letterOf(games_[b]);
		if (la != lb)
			return la == '#' ? false : lb == '#' ? true : la < lb;
		return titleOf(games_[a]) < titleOf(games_[b]);
	};
	switch (sort_) {
	case 1: std::stable_sort(rack_.begin(), rack_.end(), [&](int a, int b) { return played_[a].last != played_[b].last ? played_[a].last > played_[b].last : byName(a, b); }); break;
	case 2: std::stable_sort(rack_.begin(), rack_.end(), [&](int a, int b) { return played_[a].seconds != played_[b].seconds ? played_[a].seconds > played_[b].seconds : byName(a, b); }); break;
	case 3: std::stable_sort(rack_.begin(), rack_.end(), [&](int a, int b) { return games_[a].size != games_[b].size ? games_[a].size > games_[b].size : byName(a, b); }); break;
	default: std::stable_sort(rack_.begin(), rack_.end(), byName); break;
	}
	rack_sel_ = std::clamp(rack_sel_, 0, std::max(0, (int)rack_.size() - 1));
}

const Game *Frontend::selected() const
{
	if ((view_ == board::View::Rack || (view_ == board::View::Bench && rack_chosen_ && !game_page_)) && rack_sel_ < (int)rack_.size())
		return &games_[rack_[rack_sel_]];
	if (strip_sel_ < (int)strip_.size())
		return &games_[strip_[strip_sel_]];
	return nullptr;
}

board::Look Frontend::lookFor(const Game &g) const
{
	board::Look look = board::lookOf(g.name, g.id);
	if (host_.cover) {
		const auto c = host_.cover(g);
		look.cover = c.first;
		look.cover_aspect = c.second;
	}
	return look;
}

void Frontend::probeThumb(ImDrawList *dl, const std::string &stateFile, const board::Look &look, float x, float y, float w, float h)
{
	if (host_.probe_picture && !stateFile.empty()) {
		const auto pic = host_.probe_picture(stateFile);
		if (pic.first != ImTextureID_Invalid) {
			Ink ink{ dl, u_ };
			ink.rect(x, y, w, h, IM_COL32(0, 0, 0, 255), 5);
			dl->AddImageRounded(ImTextureRef(pic.first), S(x, y), S(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, L(5));
			ink.frame(x, y, w, h, kLine, 5, 1);
			return;
		}
	}
	fingerprint(Ink{ dl, u_ }, stateFile, look, x, y, w, h); /* (a plain stand-in: this probe has no picture) */
}

/* ---------------------------------------------------------------- the frame */

void Frontend::frame(ImDrawList *dl, const PadInput &pad_in, float w, float h, float dt, ImTextureRef boardTex, ImTextureRef logo,
                     const FrontendFonts &f)
{
	/* "Close PSGopher?" has the pad while it is up: Cross closes (the title's clean exit), Circle stays */
	PadInput quiet;
	quiet.now = pad_in.now;
	const PadInput &pad = close_open_ || update_ask_ ? quiet : pad_in;
	/* "Install vX.Y.Z?" and "Roll back to ...?" (BENCH, Console) are asked the same way: Cross does it,
	 * Circle stays. An install that is in place closes the title the clean way, as the exit row does;
	 * a roll back runs on the updater's thread and closes it when it is done */
	if (update_ask_) {
		if (pad_in.pressed & PAD_CIRCLE)
			update_ask_ = 0;
		else if (pad_in.pressed & PAD_CROSS) {
			const int ask = update_ask_;
			update_ask_ = 0;
			if (ask == 1) {
				if (host_.update_install && host_.update_install() && host_.close)
					host_.close();
			} else if (host_.update_rollback) {
				rollback_wanted_ = true;
				host_.update_rollback();
			}
		}
	}
	if (rollback_wanted_ && host_.update_status) {
		const update::Status st = host_.update_status();
		if (st.state == update::State::Installed && st.rollback) {
			rollback_wanted_ = false;
			if (host_.close)
				host_.close();
		} else if (st.state == update::State::Failed || st.state == update::State::Idle)
			rollback_wanted_ = false;
	}
	if (close_open_) {
		if (pad_in.pressed & PAD_CIRCLE)
			close_open_ = false;
		else if (pad_in.pressed & PAD_CROSS) {
			close_open_ = false;
			if (host_.close)
				host_.close();
		}
	}
	refresh();
	u_ = h / board::kStageH;
	const float stage_w = w / u_; // (16:9: 1920)
	(void)stage_w;
	Ink ink{ dl, u_ };
	const Game *g = selected();
	const board::Look look = g ? lookFor(*g) : board::Look{};

	/* the views: L1/R1 (not while the probe picker is up) */
	if (probes_open_ < 0 && !game_page_) {
		static const board::View order_of_views[3] = { board::View::Home, board::View::Rack, board::View::Bench };
		int at = view_ == board::View::Rack ? 1 : view_ == board::View::Bench ? 2 : 0;
		if (pad.pressed & PAD_R1)
			setView(order_of_views[(at + 1) % 3]);
		if (pad.pressed & PAD_L1)
			setView(order_of_views[(at + 2) % 3]);
	}
	blend_ = std::min(1.0f, blend_ + dt / 0.7f);
	projector_.pose = board::mix(board::poseOf(from_), board::poseOf(view_), board::ease(blend_));
	projector_.unit = u_;

	/* the board's machine: the seated cartridge, the settings it shows */
	machine_.expansion = host_.get ? host_.get("emulation.disable_expansion_pak") == 0 : true;
	machine_.overclock = host_.get ? host_.get("emulation.overclock") == 1 : false;
	machine_.upscale = host_.get ? (int)std::max<int64_t>(1, host_.get("video.upscale")) : 1;
	machine_.crt = host_.get ? host_.get("video.crt") == 1 : false;
	machine_.step(dt, g ? &look : nullptr);

	/* the stage's light: the game's colour mixed into the dark at 38% 42% */
	{
		const ImVec2 c = S(1920 * .38f, 1080 * .42f);
		const ImU32 inner = g ? withAlpha(board::colourOf(look, .30f), .55f) : IM_COL32(6, 20, 26, 255);
		dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(3, 8, 11, 255));
		const int rings = 28;
		for (int i = rings; i >= 1; i--) {
			const float t = (float)i / rings;
			dl->AddEllipseFilled(c, ImVec2(L(1200 * .7f * t), L(700 * .7f * t)), withAlpha(inner, 0.07f * (1 - t * 0.6f)), 0, 64);
		}
	}

	board::Fonts bf{ f.mono_bold, f.display };
	board::drawBoard(dl, boardTex, projector_, machine_, bf);

	switch (view_) {
	case board::View::Rack: rack(dl, pad, f); break;
	case board::View::Bench: bench(dl, pad, f); break;
	default: home(dl, pad, f); break;
	}
	header(dl, logo, f, w);
	if (close_open_) {
		const float pw = 760, ph = 260, px = (1920 - pw) / 2, py = (1080 - ph) / 2;
		ink.rect(0, 0, 1920, 1080, IM_COL32(1, 5, 7, 170));
		ink.glow(px, py, pw, ph, IM_COL32(79, 240, 224, 64), 24, 20);
		ink.rect(px, py, pw, ph, IM_COL32(7, 17, 21, 245), 20);
		ink.frame(px, py, pw, ph, kLine, 20, 2);
		ink.text(f.mono, 13, px + 40, py + 36, kDim, "PSGOPHER", .2f);
		ink.text(f.display, 44, px + 40, py + 64, kText, "Close PSGopher?", -.02f);
		ink.text(f.mono, 17, px + 40, py + 128, kDim, "The title ends and the console returns to its home screen.");
		float bx = px + 40;
		int which = 0;
		for (const char *label : { "\xE2\x9C\x95  CLOSE", "\xE2\x97\x8B  STAY" }) {
			const bool on = which++ == 0; /* (the first is the one that acts) */
			const float bw = ink.width(f.mono_bold, 18, label, .08f) + 48;
			ink.rect(bx, py + 176, bw, 50, on ? kText : IM_COL32(16, 34, 38, 255), 25);
			if (!on)
				ink.frame(bx, py + 176, bw, 50, kLine, 25, 1);
			ink.text(f.mono_bold, 18, bx + 24, py + 191, on ? kInk : kText, label, .08f);
			bx += bw + 14;
		}
	}
	if (update_ask_) {
		const float pw = 760, ph = 292, px = (1920 - pw) / 2, py = (1080 - ph) / 2;
		ink.rect(0, 0, 1920, 1080, IM_COL32(1, 5, 7, 170));
		ink.glow(px, py, pw, ph, IM_COL32(255, 182, 72, 64), 24, 20);
		ink.rect(px, py, pw, ph, IM_COL32(7, 17, 21, 245), 20);
		ink.frame(px, py, pw, ph, kAmber, 20, 2);
		ink.text(f.mono, 13, px + 40, py + 36, kDim, update_ask_ == 1 ? "INSTALL" : "ROLL BACK", .2f);
		ink.text(f.display, 44, px + 40, py + 64, kText, update_ask_text_, -.02f);
		ink.text(f.mono, 17, px + 40, py + 128, kDim, "The title closes when it is done.");
		ink.text(f.mono, 17, px + 40, py + 154, kDim, "Games, saves and settings stay.");
		float bx = px + 40;
		int which = 0;
		for (const char *label : { update_ask_ == 1 ? "\xE2\x9C\x95  INSTALL" : "\xE2\x9C\x95  ROLL BACK", "\xE2\x97\x8B  CANCEL" }) {
			const bool on = which++ == 0;
			const float bw = ink.width(f.mono_bold, 18, label, .08f) + 48;
			ink.rect(bx, py + 208, bw, 50, on ? kText : IM_COL32(16, 34, 38, 255), 25);
			if (!on)
				ink.frame(bx, py + 208, bw, 50, kLine, 25, 1);
			ink.text(f.mono_bold, 18, bx + 24, py + 223, on ? kInk : kText, label, .08f);
			bx += bw + 14;
		}
	}
	if (host_.notice) {
		const std::string n = host_.notice();
		if (!n.empty()) {
			const float tw = ink.width(f.mono, 16, n) + 40;
			ink.rect((1920 - tw) / 2, 128, tw, 40, kPanel, 20);
			ink.frame((1920 - tw) / 2, 128, tw, 40, kLine, 20, 1);
			ink.text(f.mono, 16, (1920 - tw) / 2 + 20, 138, kText, n);
		}
	}
}

void Frontend::header(ImDrawList *dl, ImTextureRef logo, const FrontendFonts &f, float w)
{
	Ink ink{ dl, u_ };
	/* the mark: the gopher, PSGopher, where you are */
	ink.glow(64, 46, 64, 64, IM_COL32(79, 240, 224, 64), 14, 20);
	if (logo.GetTexID() != ImTextureID_Invalid)
		dl->AddImage(logo, S(64, 42), S(128, 106));
	ink.text(f.display, 34, 144, 44, kText, "PSGopher", -.02f);
	std::string small;
	char t[96];
	if (view_ == board::View::Rack)
		snprintf(t, sizeof(t), "RACK \xC2\xB7 %zu CARTRIDGES", games_.size());
	else if (view_ == board::View::Bench)
		snprintf(t, sizeof(t), "BENCH \xC2\xB7 THE MACHINE'S PARTS");
	else {
		size_t recent = 0;
		for (const auto &p : played_)
			recent += p.last > 0;
		snprintf(t, sizeof(t), "HOME \xC2\xB7 %zu RECENT \xC2\xB7 %zu IN THE RACK", recent, games_.size());
	}
	ink.text(f.mono, 14, 144, 88, kDim, t, .18f);

	/* the views */
	static const char *const names[3] = { "HOME", "RACK", "BENCH" };
	float widths[3], total = 6;
	for (int i = 0; i < 3; i++) {
		widths[i] = ink.width(f.mono_bold, 17, names[i], .12f) + 44;
		total += widths[i] + (i ? 6 : 0);
	}
	total += 6;
	const float x0 = 960 - total / 2, y0 = 54;
	ink.rect(x0, y0, total, 54, kPanel, 27);
	ink.frame(x0, y0, total, 54, kLine, 27, 1);
	float x = x0 + 6;
	const int at = view_ == board::View::Rack ? 1 : view_ == board::View::Bench ? 2 : 0;
	for (int i = 0; i < 3; i++) {
		if (i == at)
			ink.rect(x, y0 + 6, widths[i], 42, kText, 21);
		ink.text(f.mono_bold, 17, x + 22, y0 + 17, i == at ? kInk : kDim, names[i], .12f);
		x += widths[i] + 6;
	}
	/* the clock */
	const time_t now = time(nullptr);
	struct tm tmv = {};
	localtime_r(&now, &tmv);
	char day[8], clock[24];
	strftime(day, sizeof(day), "%a", &tmv);
	for (char *p = day; *p; p++)
		*p = (char)std::toupper((unsigned char)*p);
	snprintf(clock, sizeof(clock), "%s %02d:%02d", day, tmv.tm_hour, tmv.tm_min);
	ink.text(f.mono, 17, 1856 - ink.width(f.mono, 17, clock, .12f), 70, kDim, clock, .12f);
	(void)w;
}

/* ---------------------------------------------------------------- the panel */

void Frontend::panel(ImDrawList *dl, const FrontendFonts &f, const Game &g, bool rack_panel, float x, float y, float alpha)
{
	Ink ink{ dl, u_ };
	const board::Look look = lookFor(g);
	const ImU32 game = board::colourOf(look);
	const float w = 470, pad_x = 30;
	const float inner = w - pad_x * 2;
	dl->ChannelsSplit(2);
	dl->ChannelsSetCurrent(1);
	float cy = y + 28;
	if (rack_panel) {
		/* the label, large */
		const float lx = x + pad_x, ly = cy, k = 1.0f;
		ink.glow(lx, ly, 236 * k * .78f, 208 * k * .78f, game, 18, 10);
		const float s = .78f * u_;
		board::drawLabel(dl, look, f.mono_heavy, [lx, ly, s, this](ImVec2 v) { return ImVec2(lx * u_ + v.x * s, ly * u_ + v.y * s); });
		cy += 208 * .78f + 18;
	}
	ink.text(f.mono_bold, 13, x + pad_x, cy, withAlpha(game, alpha), rack_panel ? "ON THE HOME STRIP" : "SEATED IN SLOT", .2f);
	cy += 13 + 16;
	for (const std::string &line : wrap(ink, f.display, 50, titleOf(g), inner, 3)) {
		ink.text(f.display, 50, x + pad_x - 2, cy - 4, withAlpha(kText, alpha), line, -.03f);
		cy += 50 * .98f;
	}
	cy += 16;
	const size_t gi = (size_t)(&g - games_.data());
	const auto pl = gi < played_.size() ? played_[gi] : Played{};
	std::string sub = playedText(pl.seconds);
	if (pl.last > 0)
		sub += " \xC2\xB7 last " + agoText(pl.last, false);
	sub += " \xC2\xB7 " + where(g);
	ink.text(f.mono, 16, x + pad_x, cy, withAlpha(kDim, alpha), sub);
	cy += 16 + 16;
	/* its description (Wikipedia's), three lines of it */
	if (host_.about) {
		const std::string about = host_.about(g);
		const size_t nl = about.find('\n');
		const std::string text = nl == std::string::npos ? about : about.substr(nl + 1);
		if (!text.empty()) {
			for (const std::string &line : wrap(ink, f.mono, 14, text, inner, 3)) {
				ink.text(f.mono, 14, x + pad_x, cy, withAlpha(kDim, alpha), line);
				cy += 19;
			}
			ink.text(f.mono, 11, x + pad_x, cy + 2, withAlpha(kDim, alpha * .7f), "WIKIPEDIA \xC2\xB7 CC BY-SA", .14f);
			cy += 11 + 16;
		}
	}

	/* the facts, two by three */
	char rom_size[24];
	if (g.size >= (1u << 20))
		snprintf(rom_size, sizeof(rom_size), "%.0f MB", g.size / 1048576.0);
	else if (g.size)
		snprintf(rom_size, sizeof(rom_size), "%llu KB", (unsigned long long)(g.size / 1024));
	else
		snprintf(rom_size, sizeof(rom_size), "-");
	const std::string facts[6][2] = {
		{ "GAME ID", g.facts && !g.id.empty() ? g.id : "-" },
		{ "ROM", rom_size },
		{ "LOCKOUT CHIP", g.facts ? cicText(g.cic) : "-" },
		{ "SAVE MEMORY", g.facts ? saveText(g.save) : "-" },
		{ "PAK", g.facts ? (g.rumble ? "Rumble" : "Controller Pak") : "-" },
		{ "REGION", regionName(regionOf(g)).empty() ? "-" : regionName(regionOf(g)) },
	};
	const float cell_w = (inner - 1) / 2, cell_h = 62;
	const float gx = x + pad_x, gy = cy;
	ink.rect(gx, gy, inner, cell_h * 3 + 2, kLine, 10);
	for (int i = 0; i < 6; i++) {
		const float cx = gx + (i % 2) * (cell_w + 1), cyy = gy + (i / 2) * (cell_h + 1);
		const ImDrawFlags corners = i == 0 ? ImDrawFlags_RoundTopLeft : i == 1 ? ImDrawFlags_RoundTopRight
		                            : i == 4 ? ImDrawFlags_RoundBottomLeft : i == 5 ? ImDrawFlags_RoundBottomRight
		                                     : ImDrawFlags_RoundNone;
		dl->AddRectFilled(S(cx + .5f, cyy + .5f), S(cx + cell_w - .5f, cyy + cell_h - .5f), kCellBg, L(9), corners);
		ink.text(f.mono, 12, cx + 14, cyy + 10, withAlpha(kDim, alpha), facts[i][0], .14f);
		std::string value = facts[i][1];
		while (value.size() > 3 && ink.width(f.mono_bold, 18, value) > cell_w - 24)
			value = value.substr(0, value.size() - 4) + "\xE2\x80\xA6";
		float vx = cx + 14;
		if (i == 5 && regionOf(g)) { /* the region's flag before its name */
			flag(ink, regionOf(g), vx, cyy + 31, 27);
			vx += 36;
		}
		ink.text(f.mono_bold, 18, vx, cyy + 30, withAlpha(kText, alpha), value);
	}
	cy += cell_h * 3 + 2 + 18;

	/* the probes */
	ink.text(f.mono_bold, 13, x + pad_x, cy, withAlpha(game, alpha), "MEMORY PROBES \xC2\xB7 YOUR SAVE STATES", .2f);
	cy += 13 + 14;
	if (probes_for_ != g.path) {
		probes_ = probesOf(g);
		probes_for_ = g.path;
		/* each probe's file, for its fingerprint (data/states/<name>-<hash>.state<slot>): looked up once */
		probe_files_.assign(probes_.size(), std::string());
		const std::string states = library().statesFolder(), stem = saveName(g.header) + "-";
		if (DIR *d = opendir(states.c_str())) {
			while (dirent *e = readdir(d)) {
				const std::string name = e->d_name;
				if (name.rfind(stem, 0) != 0 || name.find(".state") == std::string::npos || name.back() == 'g')
					continue;
				for (size_t i = 0; i < probes_.size(); i++)
					if (name.back() == (char)('0' + probes_[i].slot))
						probe_files_[i] = states + "/" + name;
			}
			closedir(d);
		}
	}
	if (probes_.empty()) {
		ink.text(f.mono, 15, x + pad_x, cy, withAlpha(kDim, alpha), g.facts ? "None yet: tap the touch pad in a game" : "None yet");
		cy += 15 + 12;
	}
	for (size_t i = 0; i < probes_.size() && i < 3; i++) {
		char path[512];
		const std::string &file = probe_files_[i];
		probeThumb(dl, file, look, x + pad_x, cy, 56, 42);
		snprintf(path, sizeof(path), "Probe %d", probes_[i].slot);
		ink.text(f.mono_bold, 15, x + pad_x + 70, cy + 2, withAlpha(kText, alpha), path);
		ink.text(f.mono, 15, x + pad_x + 70, cy + 22, withAlpha(kDim, alpha), agoText(probes_[i].time, true));
		cy += 42 + 8;
	}
	cy += 10;

	/* the actions */
	struct Act {
		const char *text;
		bool first;
	};
	const Act acts[3] = { { "\xE2\x9C\x95 Power on", true }, { "\xE2\x96\xA1 Probes", false },
		                  { rack_panel ? "\xE2\x96\xB3 Seat it" : "\xE2\x96\xB3 Cheats", false } };
	float ax = x + pad_x;
	for (const Act &a : acts) {
		const float aw = ink.width(f.mono_bold, 16, a.text) + 32;
		if (a.first)
			ink.rect(ax, cy, aw, 40, withAlpha(game, alpha), 20);
		else
			ink.frame(ax, cy, aw, 40, kLine, 20, 1);
		ink.text(f.mono_bold, 16, ax + 16, cy + 11, a.first ? kInk : withAlpha(kText, alpha), a.text);
		ax += aw + 10;
	}
	cy += 40 + 28;
	dl->ChannelsSetCurrent(0);
	ink.rect(x, y, w, cy - y, withAlpha(kPanel, alpha), 18);
	ink.frame(x, y, w, cy - y, withAlpha(kLine, alpha), 18, 1);
	dl->ChannelsMerge();
}

/* ---------------------------------------------------------------- HOME */

/* An N64 cartridge seen from the front, as the owner's reference photo shows one: a grey shell wider
 * than tall with an arched top, a raised centre panel set off by grooves, steps at the bottom of the
 * side panels, and the label recessed into the centre panel (the box art, or the drawn label) */
static void cartridge(ImDrawList *dl, const Ink &ink, float x0, float top, float sw, float sh, const board::Look &look, ImFont *font, bool on)
{
	auto P = [&](float x, float y) { return ink.at(x0 + x * sw, top + y * sh); };
	auto arc = [](float x) { const float d = 2 * x - 1; return .13f * d * d; }; /* the top's curve: 0 at the middle */
	/* the shell */
	constexpr int kArc = 16;
	ImVec2 shell[kArc + 1 + 4];
	int n = 0;
	for (int i = 0; i <= kArc; i++) {
		const float x = (float)i / kArc;
		shell[n++] = P(x, arc(x));
	}
	shell[n++] = P(1, .96f);
	shell[n++] = P(.985f, 1);
	shell[n++] = P(.015f, 1);
	shell[n++] = P(0, .96f);
	const ImU32 grey = IM_COL32(0x8e, 0x90, 0x95, 255), light = IM_COL32(0xa2, 0xa4, 0xa9, 255), groove = IM_COL32(0x6c, 0x6e, 0x73, 255),
	            shadow = IM_COL32(0x5c, 0x5e, 0x63, 255);
	dl->AddConvexPolyFilled(shell, n, grey);
	/* the centre panel, raised: lighter, on the same arch */
	{
		ImVec2 panel[kArc + 1 + 2];
		int m = 0;
		for (int i = 0; i <= kArc; i++) {
			const float x = .19f + .62f * i / kArc;
			panel[m++] = P(x, arc(x) - .005f);
		}
		panel[m++] = P(.81f, 1);
		panel[m++] = P(.19f, 1);
		dl->AddConvexPolyFilled(panel, m, light);
	}
	/* light from above: the lower part a little darker */
	dl->AddRectFilledMultiColor(P(0, .45f), P(1, 1), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 46), IM_COL32(0, 0, 0, 46));
	/* the grooves either side of the centre panel */
	for (float x : { .19f, .81f })
		dl->AddLine(P(x, arc(x) + .01f), P(x, 1), groove, ink.u * 1.6f);
	/* the steps at the bottom of the side panels */
	for (int side = 0; side < 2; side++) {
		auto X = [side](float x) { return side ? 1 - x : x; };
		const ImVec2 step[4] = { P(X(.035f), 1), P(X(.035f), .70f), P(X(.13f), .70f), P(X(.13f), 1) };
		dl->AddPolyline(step, 4, groove, ink.u * 1.3f);
	}
	/* the label's recess: a rounded inset, its edge in shadow; the label inside, filling it */
	const float lx = x0 + .245f * sw, ly = top + .085f * sh, lw = .51f * sw, lh = .83f * sh;
	ink.rect(lx - 3, ly - 3, lw + 6, lh + 6, shadow, 7);
	dl->PushClipRect(ink.at(lx, ly), ink.at(lx + lw, ly + lh), true);
	{
		const float k = std::max(lw / 236, lh / 208), ox = lx + (lw - 236 * k) / 2, oy = ly;
		board::drawLabelPart(dl, look, font, [&](ImVec2 v) { return ink.at(ox + v.x * k, oy + v.y * k); }, std::max(208.0f, lh / k));
	}
	dl->PopClipRect();
	ink.frame(lx - 1, ly - 1, lw + 2, lh + 2, IM_COL32(0x4a, 0x4c, 0x50, 255), 6, 1.5f);
	/* the shell's edge */
	dl->AddPolyline(shell, n, on ? board::colourOf(look) : IM_COL32(0x55, 0x57, 0x5c, 255), ink.u * (on ? 2.2f : 1.2f), ImDrawFlags_Closed);
}

void Frontend::home(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f)
{
	Ink ink{ dl, u_ };
	const int n = (int)strip_.size();
	const Game *g = selected();
	if (probes_open_ >= 0 && g) {
		probePicker(dl, pad, f, *g);
		return;
	}
	if (cheats_open_ >= 0 && g) {
		cheatPicker(dl, pad, f, *g);
		return;
	}
	if (n > 0) {
		if (pad.repeat(PAD_RIGHT))
			strip_sel_ = std::min(n - 1, strip_sel_ + 1);
		if (pad.repeat(PAD_LEFT))
			strip_sel_ = std::max(0, strip_sel_ - 1);
	}
	if (pad.pressed & PAD_CIRCLE)
		close_open_ = true;
	g = selected();
	if (g && (pad.pressed & PAD_CROSS) && host_.play && !(host_.busy && host_.busy()))
		host_.play(*g, -1);
	if (g && (pad.pressed & PAD_SQUARE)) {
		probes_for_.clear(); /* (the panel looks them up again, with their files) */
		probes_ = probesOf(*g);
		if (probes_.empty())
			host_.tell("No probes of this game yet: tap the touch pad in a game to take one");
		else
			probes_open_ = 0;
	}
	if (g && (pad.pressed & PAD_TRIANGLE))
		openGamePage(*g);

	/* the line from the strip's cartridge to the slot */
	if (g) {
		const board::Look look = board::lookOf(g->name, g->id); /* (its colour only: no cover asked for) */
		const ImVec2 slot = projector_(board::anchor("slot"));
		const ImVec2 from = S(960, 842);
		ImVec2 pts[40];
		for (int i = 0; i < 40; i++) {
			const float t = i / 39.0f, a = 1 - t;
			const ImVec2 c1 = S(960, 640), c2 = ImVec2(slot.x + L(200), slot.y + L(200));
			pts[i] = ImVec2(a * a * a * from.x + 3 * a * a * t * c1.x + 3 * a * t * t * c2.x + t * t * t * slot.x,
			                a * a * a * from.y + 3 * a * a * t * c1.y + 3 * a * t * t * c2.y + t * t * t * slot.y);
		}
		for (int i = 0; i < 39; i += 2)
			dl->AddLine(pts[i], pts[i + 1], withAlpha(board::colourOf(look), .9f), L(2.5f));
	}

	/* the panel */
	if (g)
		panel(dl, f, *g, false, 1920 - 64 - 470, 150, 1);

	/* the strip */
	const float pitch = 214, target = 960 - 98 - strip_sel_ * pitch;
	strip_x_ += (target - strip_x_) * std::min(1.0f, 1.0f / 60 * 9);
	if (n == 0) {
		ink.text(f.mono, 18, 64, 900, kDim,
		         library().scanning() ? "Looking for cartridges\xE2\x80\xA6" : "No cartridges yet: ROMs (.z64 .n64 .v64, .zip or .7z of one) in /data, on USB drives or network shares");
	}
	for (int k = std::max(0, strip_sel_ - 7); k < std::min(n, strip_sel_ + 8); k++) {
		const Game &cg = games_[strip_[k]];
		const bool on = k == strip_sel_;
		const float scale = on ? 1.08f : .9f, lift = on ? 0 : 22;
		const float sw = 196 * scale, sh = 126 * scale, cx = strip_x_ + k * pitch + 98, top = 846 + lift + (1 - scale) * 80;
		const float x0 = cx - sw / 2;
		const board::Look look = lookFor(cg);
		if (on)
			ink.glow(x0, top, sw, sh, board::colourOf(look), 22, 10);
		cartridge(dl, ink, x0, top, sw, sh, look, f.mono_heavy, on);
		if (const char r = regionOf(cg))
			flag(ink, r, x0 + sw * .86f - 12 * scale, top + sh * .30f, 22 * scale);
		std::string name = titleOf(cg);
		while (name.size() > 4 && ink.width(f.mono, 16, name) > 200)
			name = name.substr(0, name.size() - 4) + "\xE2\x80\xA6";
		const float nw = ink.width(f.mono, 16, name);
		ink.text(f.mono, 16, cx - nw / 2, top + sh + 10, on ? kText : kDim, name);
	}
	ink.text(f.mono, 14, 64, 1046, kDim, "L1 \xC2\xB7 R1  VIEWS      \xE2\x97\x84 \xE2\x96\xBA  CARTRIDGE      \xE2\x9C\x95  POWER ON      \xE2\x96\xA1  PROBES      \xE2\x96\xB3  GAME PAGE      \xE2\x97\x8B  CLOSE", .08f);
}

/* the probes of the chosen game, to start it from one */
void Frontend::probePicker(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f, const Game &g)
{
	Ink ink{ dl, u_ };
	const int n = (int)probes_.size();
	if (pad.repeat(PAD_DOWN))
		probes_open_ = (probes_open_ + 1) % std::max(1, n);
	if (pad.repeat(PAD_UP))
		probes_open_ = (probes_open_ + std::max(1, n) - 1) % std::max(1, n);
	if (pad.pressed & (PAD_CIRCLE | PAD_SQUARE)) {
		probes_open_ = -1;
		return;
	}
	if ((pad.pressed & PAD_CROSS) && n > 0 && host_.play) {
		const int slot = probes_[probes_open_].slot;
		probes_open_ = -1;
		host_.play(g, slot);
		return;
	}
	/* the same scene at 1x, 2x, 4x and 8x (benchmark mode, docs/dev/performance.md): back to the board after */
	if ((pad.pressed & PAD_TRIANGLE) && n > 0 && host_.benchmark && !(host_.busy && host_.busy())) {
		const int slot = probes_[probes_open_].slot;
		probes_open_ = -1;
		host_.benchmark(g, slot);
		return;
	}
	const board::Look look = lookFor(g);
	const float x = 1920 - 64 - 470, y = 150, w = 470;
	const float h = 28 + 13 + 16 + 50 + 20 + n * 62 + 40;
	ink.rect(x, y, w, h, kPanel, 18);
	ink.frame(x, y, w, h, kLine, 18, 1);
	ink.text(f.mono_bold, 13, x + 30, y + 28, board::colourOf(look), "POWER ON FROM A PROBE", .2f);
	ink.text(f.display, 40, x + 28, y + 52, kText, titleOf(g), -.03f);
	float cy = y + 28 + 13 + 16 + 50 + 10;
	for (int i = 0; i < n; i++) {
		const bool on = i == probes_open_;
		ink.rect(x + 30, cy, w - 60, 54, on ? kText : IM_COL32(16, 34, 38, 255), 10);
		char t[32];
		snprintf(t, sizeof(t), "Probe %d", probes_[i].slot);
		ink.text(f.mono_bold, 18, x + 118, cy + 16, on ? kInk : kText, t);
		const std::string when = agoText(probes_[i].time, true);
		probeThumb(dl, i < (int)probe_files_.size() ? probe_files_[i] : std::string(), look, x + 30, cy, 72, 54);
		ink.text(f.mono, 15, x + w - 48 - ink.width(f.mono, 15, when), cy + 18, on ? kInk : kDim, when);
		cy += 62;
	}
	ink.text(f.mono, 14, 64, 1046, kDim,
	         "\xE2\x96\xB2 \xE2\x96\xBC  PROBE      \xE2\x9C\x95  POWER ON FROM IT      \xE2\x96\xB3  BENCHMARK 1\xC3\x97 2\xC3\x97 4\xC3\x97 8\xC3\x97      \xE2\x97\x8B  BACK", .08f);
}

/* the game's cheats, as Gopher64 has them */
void Frontend::readCheats(const Game &g)
{
	cheats_.clear();
	if (g.crc.empty() || !host_.cheats_of)
		return;
	const std::string text = host_.cheats_of(g.crc);
	auto split = [](const std::string &s, char sep) {
		std::vector<std::string> out;
		size_t p = 0;
		for (;;) {
			const size_t at = s.find(sep, p);
			out.push_back(s.substr(p, at == std::string::npos ? std::string::npos : at - p));
			if (at == std::string::npos)
				return out;
			p = at + 1;
		}
	};
	for (const std::string &line : split(text, '\n')) {
		const auto fields = split(line, '\t');
		if (fields.size() < 5)
			continue;
		Cheat c;
		c.name = fields[0];
		c.note = fields[1];
		if (!fields[2].empty())
			c.options = split(fields[2], '|');
		c.on = fields[3] == "1";
		c.option = fields[4];
		cheats_.push_back(c);
	}
}

/* a cheat's name as a path: Gopher64's groups ("Infinite\Lives") shown as "Infinite › Lives" */
static std::string cheatLabel(const std::string &name)
{
	std::string out;
	for (const char c : name)
		if (c == '\\')
			out += " \xE2\x80\xBA ";
		else
			out += c;
	return out;
}

void Frontend::cheatPicker(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f, const Game &g)
{
	Ink ink{ dl, u_ };
	const int n = (int)cheats_.size();
	if (pad.repeat(PAD_DOWN))
		cheats_open_ = (cheats_open_ + 1) % n;
	if (pad.repeat(PAD_UP))
		cheats_open_ = (cheats_open_ + n - 1) % n;
	if (pad.pressed & (PAD_CIRCLE | PAD_TRIANGLE)) {
		cheats_open_ = -1;
		return;
	}
	Cheat &c = cheats_[cheats_open_];
	auto save = [&]() {
		if (host_.cheat_set && !host_.cheat_set(g.crc, c.name, c.on, c.option))
			host_.tell("Gopher64 did not take that cheat");
	};
	if (pad.pressed & PAD_CROSS) {
		c.on = !c.on;
		if (c.on && !c.options.empty() && c.option.empty())
			c.option = c.options.front();
		save();
	}
	const bool right = pad.repeat(PAD_RIGHT), left = !right && pad.repeat(PAD_LEFT);
	if (c.on && !c.options.empty() && (right || left)) {
		int at = 0;
		for (size_t i = 0; i < c.options.size(); i++)
			if (c.options[i] == c.option)
				at = (int)i;
		const int m = (int)c.options.size();
		at = right ? (at + 1) % m : (at + m - 1) % m;
		c.option = c.options[at];
		save();
	}
	const board::Look look = lookFor(g);
	const float x = 1920 - 64 - 560, y = 150, w = 560, rh = 48;
	const int rows = 11;
	const float h = 28 + 13 + 16 + 50 + 16 + rows * rh + 20 + 76 + 24;
	ink.rect(x, y, w, h, kPanel, 18);
	ink.frame(x, y, w, h, kLine, 18, 1);
	int on = 0;
	for (const Cheat &k : cheats_)
		on += k.on;
	char t[80];
	snprintf(t, sizeof(t), "CHEATS \xC2\xB7 %d OF %d ON \xC2\xB7 FROM THE NEXT START", on, n);
	ink.text(f.mono_bold, 13, x + 30, y + 28, board::colourOf(look), t, .2f);
	ink.text(f.display, 40, x + 28, y + 52, kText, titleOf(g), -.03f);
	float cy = y + 28 + 13 + 16 + 50 + 6;
	const int first = std::clamp(cheats_open_ - rows / 2, 0, std::max(0, n - rows));
	for (int i = first; i < std::min(n, first + rows); i++) {
		const Cheat &k = cheats_[i];
		const bool here = i == cheats_open_;
		ink.rect(x + 24, cy, w - 48, rh - 6, here ? kText : IM_COL32(16, 34, 38, 255), 9);
		const std::string state = k.on ? (k.option.empty() ? std::string("ON") : "\xE2\x97\x84 " + k.option + " \xE2\x96\xBA") : std::string("off");
		const float state_w = ink.width(f.mono_bold, 13, state);
		std::string label = cheatLabel(k.name);
		while (label.size() > 4 && ink.width(f.mono_bold, 15, label) > w - 48 - 48 - state_w)
			label = label.substr(0, label.size() - 4) + "\xE2\x80\xA6";
		ink.text(f.mono_bold, 15, x + 40, cy + 12, here ? kInk : kText, label);
		ink.text(f.mono_bold, 13, x + w - 40 - state_w, cy + 13, k.on ? (here ? kInk : kAmber) : (here ? kInk : kDim), state);
		cy += rh;
	}
	/* the chosen one's note */
	cy += 8;
	for (const std::string &line : wrap(ink, f.mono, 13, c.note.empty() ? "No note." : c.note, w - 60, 4)) {
		ink.text(f.mono, 13, x + 30, cy, kDim, line);
		cy += 18;
	}
	ink.text(f.mono, 14, 64, 1046, kDim,
	         "\xE2\x96\xB2 \xE2\x96\xBC  CHEAT      \xE2\x9C\x95  ON / OFF      \xE2\x97\x84 \xE2\x96\xBA  ITS OPTION      \xE2\x97\x8B  BACK", .08f);
}

/* ---------------------------------------------------------------- RACK */

void Frontend::rack(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f)
{
	Ink ink{ dl, u_ };
	constexpr float RX = 70, RY = 178, PITCH = 23.6f, SW = 19, SH = 108, SHELF = 150;
	constexpr int COLS = 52, ROWS_SHOWN = 5;
	static const char *const sorts[4] = { "A-Z", "Recently played", "Most played", "Size" };
	static const char *const shows[7] = { "All", "Played", "Never played", "Has probes", "Internal", "USB", "Network" };
	const int n = (int)rack_.size();

	/* what the filter shows */
	static std::set<std::string> with_probes;
	static unsigned probes_version = ~0u;
	if (show_ == 3 && probes_version != version_) {
		with_probes.clear();
		if (DIR *d = opendir(library().statesFolder().c_str())) {
			while (dirent *e = readdir(d)) {
				const std::string name = e->d_name;
				const size_t dash = name.rfind('-');
				/* (a probe is "<name>-<hash>.state<digit>": its "<state>.png" picture is not one) */
				const size_t dot = name.rfind(".state");
				if (dash != std::string::npos && dot != std::string::npos && dot + 7 == name.size() && std::isdigit((unsigned char)name.back()))
					with_probes.insert(name.substr(0, dash));
			}
			closedir(d);
		}
		probes_version = version_;
	}
	auto visible = [&](int k) {
		const int i = rack_[k];
		const Game &g = games_[i];
		switch (show_) {
		case 1: return played_[i].last > 0;
		case 2: return played_[i].last == 0;
		case 3: return with_probes.count(saveName(g.header)) > 0;
		case 4: return g.tab == TabInternal;
		case 5: return g.tab == TabUsb;
		case 6: return g.tab == TabNetwork;
		default: return true;
		}
	};
	auto step = [&](int from, int d) {
		for (int k = from + d; k >= 0 && k < n; k += d)
			if (visible(k))
				return k;
		return from;
	};
	if (n > 0) {
		if (!visible(rack_sel_))
			rack_sel_ = step(rack_sel_, 1) != rack_sel_ ? step(rack_sel_, 1) : step(rack_sel_, -1);
		if (pad.repeat(PAD_RIGHT))
			rack_sel_ = step(rack_sel_, 1);
		if (pad.repeat(PAD_LEFT))
			rack_sel_ = step(rack_sel_, -1);
		if (pad.repeat(PAD_DOWN) || pad.repeat(PAD_UP)) {
			/* a shelf down or up: the nearest shown cartridge in that column, then outward */
			const int d = (pad.held & PAD_DOWN) ? 1 : -1, row = rack_sel_ / COLS + d, col = rack_sel_ % COLS;
			int best = -1;
			for (int off = 0; off < COLS && best < 0; off++)
				for (int s : { -1, 1 }) {
					const int c = col + s * off, k = row * COLS + c;
					if (c >= 0 && c < COLS && k >= 0 && k < n && visible(k)) {
						best = k;
						break;
					}
				}
			if (best >= 0)
				rack_sel_ = best;
		}
		/* L2/R2 (held: repeating) in A-Z order: R2 to the first cartridge of the next letter (at the
		 * last letter, its last), L2 to the first of this letter and, from there, of the one before;
		 * only the cartridges the filter shows count; the letter is shown large for a moment */
		if (sort_ == 0 && (pad.repeat(PAD_R2_DIGITAL) || pad.repeat(PAD_L2_DIGITAL))) {
			const int d = pad.repeat(PAD_R2_DIGITAL) ? 1 : -1;
			auto letter = [&](int k) { return letterOf(games_[rack_[k]]); };
			const char now = letter(rack_sel_);
			int to = rack_sel_;
			if (d > 0) {
				for (int k = rack_sel_ + 1; k < n; k++)
					if (visible(k) && letter(k) != now) {
						to = k;
						break;
					}
				if (to == rack_sel_)
					for (int k = n - 1; k > rack_sel_; k--)
						if (visible(k)) {
							to = k;
							break;
						}
			} else {
				int first = rack_sel_; /* the first shown cartridge of this letter */
				for (int k = rack_sel_ - 1; k >= 0 && letter(k) == now; k--)
					if (visible(k))
						first = k;
				to = first;
				if (first == rack_sel_) {
					int prev = -1;
					for (int k = rack_sel_ - 1; k >= 0; k--)
						if (visible(k) && letter(k) != now) {
							prev = k;
							break;
						}
					if (prev >= 0) {
						to = prev;
						for (int k = prev - 1; k >= 0 && letter(k) == letter(prev); k--)
							if (visible(k))
								to = k;
					}
				}
			}
			rack_sel_ = to;
			letter_ = letter(to);
			letter_at_ = pad.now;
		}
	}
	if (pad.pressed & PAD_SQUARE) {
		const int was = n ? rack_[rack_sel_] : -1;
		sort_ = (sort_ + 1) % 4;
		order();
		for (int k = 0; k < n; k++)
			if (rack_[k] == was)
				rack_sel_ = k;
	}
	if (pad.pressed & PAD_OPTIONS)
		show_ = (show_ + 1) % 7;
	if (pad.pressed & PAD_CIRCLE)
		setView(board::View::Home);
	const Game *g = n ? &games_[rack_[rack_sel_]] : nullptr;
	if (g && (pad.pressed & PAD_CROSS) && host_.play && !(host_.busy && host_.busy()))
		host_.play(*g, -1);
	if (g && (pad.pressed & PAD_TRIANGLE)) {
		/* seat it: to the home strip, chosen there */
		for (size_t k = 0; k < strip_.size(); k++)
			if (strip_[k] == rack_[rack_sel_])
				strip_sel_ = (int)k;
		setView(board::View::Home);
	}

	/* the shelves in view: the chosen one's row kept between the second and the fourth */
	const int rows = (n + COLS - 1) / COLS, sel_row = rack_sel_ / COLS;
	const float want = (float)std::clamp(sel_row - 2, 0, std::max(0, rows - ROWS_SHOWN));
	rack_scroll_ += (want - rack_scroll_) * std::min(1.0f, 1.0f / 60 * 10);
	const float top = RY - 40, bottom = RY + ROWS_SHOWN * SHELF - 22;
	ink.rect(44, top, 1268, bottom - RY + 70, IM_COL32(4, 12, 15, 235), 18);
	ink.frame(44, top, 1268, bottom - RY + 70, IM_COL32(140, 230, 220, 56), 18, 2);
	dl->PushClipRect(S(44, top), S(1312, top + bottom - RY + 70), true);
	for (int r = 0; r < ROWS_SHOWN + 1; r++) {
		const float y = RY + r * SHELF - std::fmod(rack_scroll_, 1.0f) * SHELF;
		dl->AddRectFilledMultiColor(S(56, y - 18), S(1300, y + SH + 6), IM_COL32(4, 11, 14, 255), IM_COL32(4, 11, 14, 255),
		                            IM_COL32(10, 28, 34, 255), IM_COL32(10, 28, 34, 255));
	}
	const board::Look sel_look = g ? board::lookOf(g->name, g->id) : board::Look{}; /* (colours only) */
	if (g) {
		const float sx = RX + (rack_sel_ % COLS) * PITCH + 10, sy = RY + (sel_row - rack_scroll_) * SHELF + 50;
		for (int i = 10; i >= 1; i--)
			dl->AddCircleFilled(S(sx, sy), L(300 * i / 10.0f), withAlpha(board::colourOf(sel_look, .6f), .028f), 48);
	}
	const int first_row = std::max(0, (int)rack_scroll_ - 1), last_row = std::min(rows, (int)rack_scroll_ + ROWS_SHOWN + 1);
	char prev_letter = 0;
	if (first_row * COLS > 0 && first_row * COLS < n)
		prev_letter = letterOf(games_[rack_[first_row * COLS - 1]]);
	for (int k = first_row * COLS; k < std::min(n, last_row * COLS); k++) {
		const Game &cg = games_[rack_[k]];
		/* a spine draws its hue and code, never a cover: none is asked for (FE-003) */
		const board::Look look = board::lookOf(cg.name, cg.id);
		const bool on = k == rack_sel_;
		const float x = RX + (k % COLS) * PITCH, y = RY + (k / COLS - rack_scroll_) * SHELF - (on ? 34 : 0);
		const float a = visible(k) ? 1 : .13f;
		ink.rect(x, y, SW, SH, withAlpha(IM_COL32(0x20, 0x29, 0x2f, 255), a));
		const ImVec2 top_face[4] = { S(x, y), S(x + 4, y - 5), S(x + SW + 4, y - 5), S(x + SW, y) };
		dl->AddConvexPolyFilled(top_face, 4, withAlpha(IM_COL32(0x32, 0x3d, 0x45, 255), a));
		const ImVec2 side[4] = { S(x + SW, y), S(x + SW + 4, y - 5), S(x + SW + 4, y + SH - 5), S(x + SW, y + SH) };
		dl->AddConvexPolyFilled(side, 4, withAlpha(IM_COL32(0x0e, 0x14, 0x18, 255), a));
		ink.rect(x + 3, y + 8, SW - 6, SH - 30, withAlpha(board::hsl(look.hue, on ? .85f : .62f, on ? .52f : .36f), a));
		/* the code, up the spine */
		if (f.mono_bold) {
			const int start = dl->VtxBuffer.Size;
			dl->AddText(f.mono_bold, L(10), ImVec2(0, 0), withAlpha(IM_COL32(255, 255, 255, 217), a), look.code.c_str());
			const ImVec2 pivot = S(x + SW / 2 + 1, y + SH - 26);
			for (int vi = start; vi < dl->VtxBuffer.Size; vi++) {
				const ImVec2 v = dl->VtxBuffer[vi].pos;
				/* rotated -90 degrees about the pivot, as the design's rotate(-PI/2), from (4, -6) */
				const float lx = v.x + L(4), ly = v.y - L(6);
				dl->VtxBuffer[vi].pos = ImVec2(pivot.x + ly, pivot.y - lx);
			}
		}
		/* its probes, a tick each; played in the last week: a light */
		const size_t gi = (size_t)rack_[k];
		if (played_[gi].last > 0 && time(nullptr) - played_[gi].last < 7 * 86400)
			dl->AddCircleFilled(S(x + SW / 2, y + SH - 4), L(2.4f), withAlpha(board::hsl(look.hue, .95f, .64f), a));
		if (played_[gi].last == 0) { /* still in its wrap: a sheen drifting past */
			const float p = std::fmod(machine_.time * 70 + k * 37, 260.0f) - 40;
			if (p > -10 && p < SH + 10)
				dl->AddRectFilledMultiColor(S(x, std::max(y, y + p - 14)), S(x + SW, y + p + 14), IM_COL32(255, 255, 255, 0),
				                            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, (int)(60 * a)), IM_COL32(255, 255, 255, (int)(60 * a)));
			ink.frame(x + .5f, y + .5f, SW - 1, SH - 1, IM_COL32(255, 255, 255, (int)(31 * a)));
		}
		if (on) {
			ink.glow(x - 2, y - 7, SW + 8, SH + 9, board::colourOf(look), 14, 2);
			ink.frame(x - 2, y - 7, SW + 8, SH + 9, board::colourOf(look), 0, 2.5f);
		}
		/* the letter's marker at its first cartridge */
		if (sort_ == 0) {
			const char lt = letterOf(cg);
			if (lt != prev_letter) {
				const bool hit = g && lt == letterOf(*g);
				ink.rect(x - 3, y - 26 + (on ? 34 : 0), 18, 17, hit ? board::colourOf(sel_look) : IM_COL32(214, 236, 232, 217), 3);
				const char s[2] = { lt, 0 };
				ink.text(f.mono_heavy, 12, x + 6 - ink.width(f.mono_heavy, 12, s) / 2, y - 24 + (on ? 34 : 0), kInk, s);
			}
			prev_letter = lt;
		}
	}
	/* the shelves' lips, in front */
	for (int r = 0; r < ROWS_SHOWN + 1; r++) {
		const float y = RY + r * SHELF + SH - std::fmod(rack_scroll_, 1.0f) * SHELF;
		dl->AddRectFilledMultiColor(S(52, y), S(1304, y + 14), IM_COL32(0x2c, 0x5c, 0x62, 255), IM_COL32(0x2c, 0x5c, 0x62, 255),
		                            IM_COL32(0x0b, 0x22, 0x28, 255), IM_COL32(0x0b, 0x22, 0x28, 255));
		ink.rect(52, y, 1252, 1.5f, IM_COL32(160, 255, 240, 89));
	}
	dl->PopClipRect();

	/* the letter index */
	int counts[27] = {};
	for (int k = 0; k < n; k++)
		if (visible(k)) {
			const char lt = letterOf(games_[rack_[k]]);
			counts[lt == '#' ? 26 : lt - 'A']++;
		}
	int most = 1;
	for (int c : counts)
		most = std::max(most, c);
	const char now_letter = g ? letterOf(*g) : 0;
	for (int i = 0; i < 26; i++) {
		const char lt = (char)('A' + i);
		const float x = 64 + i * 47.5f, hgt = std::round(30.0f * counts[i] / most);
		ink.rect(x + 12, 950 - hgt, 22, hgt,
		         lt == now_letter ? board::colourOf(sel_look) : counts[i] ? IM_COL32(214, 236, 232, 128) : IM_COL32(214, 236, 232, 31));
		const char s[2] = { lt, 0 };
		ink.text(f.mono_bold, 15, x + 23 - ink.width(f.mono_bold, 15, s) / 2, 958,
		         lt == now_letter ? IM_COL32(255, 255, 255, 255) : counts[i] ? IM_COL32(214, 236, 232, 204) : IM_COL32(214, 236, 232, 77), s);
	}
	/* sort and show */
	float bx = 64;
	const float by = 990;
	ink.text(f.mono, 13, bx, by + 10, kDim, "\xE2\x96\xA1 SORT", .16f);
	bx += ink.width(f.mono, 13, "\xE2\x96\xA1 SORT", .16f) + 16;
	auto chip = [&](const char *text, bool on) {
		const float cw = ink.width(f.mono_bold, 15, text) + 28;
		ink.rect(bx, by, cw, 34, on ? kText : IM_COL32(7, 18, 22, 230), 17);
		if (!on)
			ink.frame(bx, by, cw, 34, kLine, 17, 1);
		ink.text(f.mono_bold, 15, bx + 14, by + 8, on ? kInk : kText, text);
		bx += cw + 6;
	};
	for (int i = 0; i < 4; i++)
		chip(sorts[i], i == sort_);
	bx += 18;
	ink.text(f.mono, 13, bx, by + 10, kDim, "OPTIONS SHOW", .16f);
	bx += ink.width(f.mono, 13, "OPTIONS SHOW", .16f) + 16;
	chip(shows[show_], true);
	int shown = 0;
	for (int k = 0; k < n; k++)
		shown += visible(k);
	char count[40];
	snprintf(count, sizeof(count), "%d cartridges", shown);
	ink.text(f.mono, 15, 1300 - ink.width(f.mono, 15, count), by + 9, kDim, count);
	ink.text(f.mono, 14, 64, 1046, kDim,
	         "\xE2\x97\x84 \xE2\x96\xBA \xE2\x96\xB2 \xE2\x96\xBC  CARTRIDGE      L2 \xC2\xB7 R2  LETTER      \xE2\x9C\x95  POWER ON      \xE2\x96\xB3  SEAT IT      \xE2\x97\x8B  HOME",
	         .08f);

	if (g)
		panel(dl, f, *g, true, 1920 - 64 - 470, 150, 1);

	/* the letter just jumped to, large, for a moment */
	if (const float since = pad.now - letter_at_; since >= 0 && since < 0.9f && letter_ != 0) {
		const float a = since < 0.6f ? 1.0f : 1.0f - (since - 0.6f) / 0.3f, size = 190, lx = 1920 - 64 - size - 470 - 24, ly = 190;
		ink.glow(lx, ly, size, size, withAlpha(kText, .35f * a), 28, 14);
		ink.rect(lx, ly, size, size, withAlpha(IM_COL32(7, 17, 21, 255), .94f * a), 28);
		ink.frame(lx, ly, size, size, withAlpha(kText, a), 28, 3);
		const char s[2] = { letter_, 0 };
		const float sw = ink.width(f.mono_heavy, 120, s);
		ink.text(f.mono_heavy, 120, lx + (size - sw) / 2, ly + 22, withAlpha(kText, a), s);
	}
}

/* ---------------------------------------------------------------- BENCH */

namespace {

/* BENCH: sections (L2 and R2), each a panel of labelled rows (up and down), each row one setting */
struct Option {
	std::string label;
	int64_t value;
};
struct Row {
	enum Kind { Choice, Stepper, Action, File, Bind, Info } kind = Choice;
	std::string key, label, help;               // key: Gopher64's config key, the ini's, or this row's name (settings-words.py)
	bool has_def = false;                       // def: the value the row has by default (its choice is marked)
	int64_t def = 0;
	std::vector<Option> options;                 // Choice
	std::function<int64_t()> get;                // Choice, Stepper
	std::function<void(int64_t)> set;
	int64_t lo = 0, hi = 0, step = 1;            // Stepper
	std::string unit;
	std::function<std::string()> value;          // Action, File, Bind, Info: what is shown
	std::function<void()> run;                   // Action, File (the picker), Bind (press a button)
	std::function<void()> clear;                 // File, Bind: left clears
	std::function<bool()> shown;                 // empty: always; a row that is not shown is left out of the panel (its words are still dumped)
	std::function<int64_t()> inherited;          // a game's page: the global setting's value, shown beside Use Global Setting
};
constexpr int64_t kUseGlobal = -(1LL << 40);     // a game's page: the option that leaves the row to the global setting
struct Section {
	std::string name, part, text;
	std::vector<Row> rows;
};

/* the standard choice carries "(Default)", except on a plain Off/On switch */
void markDefault(Row &r)
{
	if (!r.has_def || r.options.empty())
		return;
	if (r.options.size() == 2 && r.options[0].label == "Off" && r.options[1].label == "On")
		return;
	for (Option &o : r.options)
		if (o.value == r.def && o.label.find("(Default)") == std::string::npos)
			o.label += " (Default)";
}

std::string jsonText(const std::string &s)
{
	std::string out = "\"";
	for (const char c : s)
		out += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
	return out + "\"";
}

/* a DualSense control by SDL's gamepad numbering, as Gopher64's profiles keep it */
std::string bindingLabel(const Binding &b)
{
	static const char *const buttons[21] = { "Cross", "Circle", "Square", "Triangle", "Touch Pad", "PS", "Options", "L3", "R3", "L1", "R1",
		                                     "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right", "Mute", "", "", "", "", "Touch Pad" };
	switch (b.kind) {
	case 1:
		return b.id >= 0 && b.id < 21 && buttons[b.id][0] ? buttons[b.id] : "Button " + std::to_string(b.id);
	case 2:
		switch (b.id) {
		case 0: return b.axis < 0 ? "Left Stick \xE2\x86\x90" : "Left Stick \xE2\x86\x92";
		case 1: return b.axis < 0 ? "Left Stick \xE2\x86\x91" : "Left Stick \xE2\x86\x93";
		case 2: return b.axis < 0 ? "Right Stick \xE2\x86\x90" : "Right Stick \xE2\x86\x92";
		case 3: return b.axis < 0 ? "Right Stick \xE2\x86\x91" : "Right Stick \xE2\x86\x93";
		case 4: return "L2";
		case 5: return "R2";
		default: return "Axis " + std::to_string(b.id);
		}
	case 3: return "Joystick Input (DirectInput)";
	default: return "\xE2\x80\x94 None";
	}
}

/* what the pad does now, as a binding (the first button down, or a stick or trigger past 60%) */
bool padBinding(const PadInput &pad, Binding *out)
{
	static const struct {
		uint32_t bit;
		int id;
	} buttons[] = { { PAD_CROSS, 0 }, { PAD_CIRCLE, 1 }, { PAD_SQUARE, 2 }, { PAD_TRIANGLE, 3 }, { PAD_TOUCH_PAD, 4 }, { PAD_OPTIONS, 6 },
		            { PAD_L3, 7 },    { PAD_R3, 8 },     { PAD_L1, 9 },      { PAD_R1, 10 },      { PAD_UP, 11 },       { PAD_DOWN, 12 },
		            { PAD_LEFT, 13 }, { PAD_RIGHT, 14 } };
	for (const auto &b : buttons)
		if (pad.pressed & b.bit) {
			*out = { 1, b.id, 0 };
			return true;
		}
	const float axes[4] = { pad.left_x, pad.left_y, pad.right_x, pad.right_y };
	for (int a = 0; a < 4; a++)
		if (std::fabs(axes[a]) > .6f) {
			*out = { 2, a, axes[a] < 0 ? -1 : 1 };
			return true;
		}
	if (pad.l2 > .6f) {
		*out = { 2, 4, 1 };
		return true;
	}
	if (pad.r2 > .6f) {
		*out = { 2, 5, 1 };
		return true;
	}
	return false;
}

bool padAtRest(const PadInput &pad)
{
	return pad.held == 0 && std::fabs(pad.left_x) < .3f && std::fabs(pad.left_y) < .3f && std::fabs(pad.right_x) < .3f &&
	       std::fabs(pad.right_y) < .3f && pad.l2 < .3f && pad.r2 < .3f;
}

/* ---- the updater's words on BENCH (update.h) ---- */

std::string fmtBytes(uint64_t bytes)
{
	char t[32];
	if (bytes >= 1024ull * 1024)
		snprintf(t, sizeof(t), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
	else
		snprintf(t, sizeof(t), "%.0f KB", (double)bytes / 1024.0);
	return t;
}

/* the release's tag as shown ("v1.2.0") */
std::string releaseTag(const update::Status &st)
{
	if (!st.tag.empty())
		return st.tag;
	return st.version.empty() ? std::string("the release") : "v" + st.version;
}

/* the Check for Updates row's value: the result in a few words */
std::string updateValue(const update::Status &st)
{
	switch (st.state) {
	case update::State::Checking: return "Checking";
	case update::State::UpToDate: return st.developer_build ? "Developer Build" : "Up to Date";
	case update::State::Available: return releaseTag(st) + (st.skipped ? " Skipped" : " Available");
	case update::State::NoRelease: return "No Release Yet";
	case update::State::Downloading:
		return st.total ? "Downloading " + std::to_string((int)(100.0 * (double)st.done / (double)st.total)) + "%" : std::string("Downloading");
	case update::State::Verifying: return st.rollback ? "Checking" : "Verifying";
	case update::State::Ready: return "Ready to Install";
	case update::State::Installed: return "Installed";
	case update::State::Failed: return "Failed";
	default: return "Check";
	}
}

/* what the check may be asked again in: not while something is going on or waits */
bool updateIdle(const update::Status &st)
{
	switch (st.state) {
	case update::State::Checking:
	case update::State::Downloading:
	case update::State::Verifying:
	case update::State::Ready:
	case update::State::Installed: return false;
	default: return true;
	}
}

/* the notes of every newer release, newest first: a heading and the plain text of each */
std::string updateNotesText(const update::Status &st)
{
	std::string out;
	for (const update::ReleaseNotes &r : st.newer) {
		if (!out.empty())
			out += "\n";
		out += r.tag + (r.name.empty() || r.name == r.tag ? "" : "  " + r.name) + (r.date.empty() ? "" : "  (" + r.date + ")") + "\n" + r.text + "\n";
	}
	return out.empty() ? st.notes : out;
}

/* text with its own line breaks, wrapped; blank lines kept once; cut with an ellipsis after max_lines */
std::vector<std::string> wrapParagraphs(const Ink &ink, ImFont *font, float size, const std::string &text, float width, int max_lines)
{
	std::vector<std::string> lines;
	size_t at = 0;
	while (at <= text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos)
			end = text.size();
		std::string para = text.substr(at, end - at);
		while (!para.empty() && (para.back() == '\r' || para.back() == ' '))
			para.pop_back();
		if (para.empty()) {
			if (!lines.empty() && !lines.back().empty())
				lines.push_back("");
		} else {
			for (const std::string &l : wrap(ink, font, size, para, width, 1000))
				lines.push_back(l);
		}
		at = end + 1;
	}
	while (!lines.empty() && lines.back().empty())
		lines.pop_back();
	if ((int)lines.size() > max_lines && max_lines > 0) {
		lines.resize(max_lines);
		lines.back() += "\xE2\x80\xA6";
	}
	return lines;
}

std::string baseName(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

/* the host tests' dump of every BENCH row's words, one JSON line each (ps5/tools/settings-words.py reads it) */
FILE *g_settings_words_dump = nullptr;

static void dumpWords(FILE *out, const std::vector<Section> &sections)
{
	for (const Section &s : sections) {
		fprintf(out, "{\"section\":%s,\"text\":%s}\n", jsonText(s.name).c_str(), jsonText(s.text).c_str());
		for (const Row &r : s.rows) {
			static const char *const kinds[] = { "choice", "stepper", "action", "file", "bind", "info" };
			fprintf(out, "{\"section\":%s,\"key\":%s,\"kind\":\"%s\",\"label\":%s,\"help\":%s,\"value\":%s,\"default\":%s,\"choices\":[",
			        jsonText(s.name).c_str(), jsonText(r.key).c_str(), kinds[r.kind], jsonText(r.label).c_str(), jsonText(r.help).c_str(),
			        jsonText(r.value ? r.value() : "").c_str(), r.has_def ? std::to_string(r.def).c_str() : "null");
			for (size_t o = 0; o < r.options.size(); o++)
				fprintf(out, "%s[%s,%lld]", o ? "," : "", jsonText(r.options[o].label).c_str(), (long long)r.options[o].value);
			fprintf(out, "]}\n");
		}
	}
}

void Frontend::bench(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f)
{
	Ink ink{ dl, u_ };
	if (game_page_ && cheats_open_ >= 0 && !cheats_.empty()) {
		cheatPicker(dl, pad, f, page_game_); /* (the game's cheats, from its page) */
		return;
	}
	cheats_open_ = -1;
	TitleSettings *ts = host_.title;
	auto choose = [this](const char *key, const std::string &label, const std::string &help, std::vector<Option> options, int64_t def) {
		Row r;
		r.key = key, r.label = label, r.help = help, r.options = std::move(options), r.has_def = true, r.def = def;
		markDefault(r);
		r.get = [this, key] { return host_.get ? host_.get(key) : 0; };
		r.set = [this, key](int64_t v) { if (host_.set) host_.set(key, v); };
		return r;
	};
	auto title_row = [ts](const std::string &key, const std::string &label, const std::string &help, std::vector<Option> options,
	                      std::function<int64_t(TitleSettings &)> get, std::function<void(TitleSettings &, int64_t)> set, int64_t def) {
		Row r;
		r.key = key, r.label = label, r.help = help, r.options = std::move(options), r.has_def = true, r.def = def;
		markDefault(r);
		r.get = [ts, get] { return ts ? get(*ts) : 0; };
		r.set = [ts, set](int64_t v) {
			if (ts) {
				set(*ts, v);
				ts->save();
			}
		};
		return r;
	};
	/* a title setting that a game can have of its own (TitleSettings::value and assign: one table) */
	auto field_row = [&title_row](const std::string &key, const std::string &label, const std::string &help, std::vector<Option> options, int64_t def) {
		return title_row(key, label, help, std::move(options), [key](TitleSettings &t) -> int64_t { return t.value(key); },
		                 [key](TitleSettings &t, int64_t v) { t.assign(key, v); }, def);
	};
	auto info = [](const std::string &key, const std::string &label, const std::string &value, const std::string &help) {
		Row r;
		r.kind = Row::Info, r.key = key, r.label = label, r.help = help;
		r.value = [value] { return value; };
		return r;
	};
	auto action = [](const std::string &key, const std::string &label, std::function<std::string()> value, const std::string &help, std::function<void()> run) {
		Row r;
		r.kind = Row::Action, r.key = key, r.label = label, r.help = help, r.value = std::move(value), r.run = std::move(run);
		return r;
	};
	const std::vector<std::string> profiles = host_.profiles ? host_.profiles() : std::vector<std::string>{ "default" };
	if (bench_profile_.empty() || std::find(profiles.begin(), profiles.end(), bench_profile_) == profiles.end())
		bench_profile_ = profiles.empty() ? "default" : profiles.front();
	auto bind_row = [this, &pad](const std::string &key, const std::string &profile, int input, const std::string &label, const std::string &help) {
		Row r;
		r.kind = Row::Bind, r.key = key, r.label = label, r.help = help;
		r.value = [this, profile, input] {
			if (capture_input_ == input && capture_profile_ == profile)
				return std::string("press a button or move a stick\xE2\x80\xA6");
			return bindingLabel(host_.binding ? host_.binding(profile, input) : Binding{});
		};
		r.run = [this, profile, input, &pad] {
			capture_input_ = input, capture_profile_ = profile, capture_since_ = pad.now, capture_released_ = false;
		};
		r.clear = [this, profile, input] { if (host_.bind) host_.bind(profile, input, Binding{}); };
		return r;
	};
	const std::string port1_profile = host_.text ? host_.text("input.input_profile_binding.0") : "default";

	std::vector<Section> sections;
	/* Video */
	{
		Section s{ "Video", "rdp", "Controls how the picture is drawn: its resolution, filters and smoothing, and how shaders are compiled.", {} };
		s.rows.push_back(choose("video.upscale", "Resolution", "Determines the internal resolution as a multiple of the original N64 resolution. Higher values give a sharper picture but reduce performance.",
		                        { { "1x", 1 }, { "2x", 2 }, { "4x", 4 }, { "8x", 8 } }, 1));
		s.rows.push_back(choose("video.ssaa", "SSAA downscaling", "When enabled, draws the picture at a higher resolution and scales it down to the original size. Smooths edges while keeping the original sharpness. Reduces performance.",
		                        { { "Off", 0 }, { "On", 1 } }, 0));
		s.rows.push_back(field_row("downsample", "Downsampling", "Selects how far the upscaled picture is scaled down before it is shown: to 1/2, 1/4 or 1/8 of the Resolution, never below the original size. Smooths edges and reduces sharpness. SSAA downscaling, when enabled, always scales down all the way.",
		                           { { "Off", 0 }, { "1/2", 1 }, { "1/4", 2 }, { "1/8", 3 } }, 0));
		s.rows.push_back(field_row("final_filter", "Final Filter", "Selects how the picture is scaled to the screen. Sharp Bilinear keeps each pixel crisp and blends only at the edges between pixels, Bilinear smooths the whole picture, and Nearest repeats pixels without blending. The CRT shader replaces this filter.",
		                           { { "Sharp Bilinear", 0 }, { "Bilinear", 1 }, { "Nearest", 2 } }, 0));
		s.rows.push_back(field_row("vi_bilinear", "VI Bilinear", "Selects whether the video interface smooths the picture as it scales it, as the N64 does. Auto turns the smoothing off at 4x and 8x, where the larger picture is smooth already, and leaves it on at 1x and 2x.",
		                           { { "Auto", 0 }, { "On", 1 }, { "Off", 2 } }, 0));
		s.rows.push_back(field_row("vi_aa", "VI Anti-Aliasing", "When enabled, applies the video interface's edge smoothing, as the N64 does. When disabled, edges stay as drawn, which is sharper but more jagged.",
		                           { { "Off", 0 }, { "On", 1 } }, 1));
		s.rows.push_back(field_row("dither_filter", "Dither Filter", "When enabled, applies the N64's dither filter, which smooths the dotted patterns that games use to fake extra colors. When disabled, the dots show.",
		                           { { "Off", 0 }, { "On", 1 } }, 1));
		s.rows.push_back(field_row("divot_filter", "Divot Filter", "When enabled, applies the N64's divot filter, which removes stray bright or dark pixels at the edges of objects. When disabled, those pixels show.",
		                           { { "Off", 0 }, { "On", 1 } }, 1));
		s.rows.push_back(field_row("gamma_dither", "Gamma Dither", "When enabled, applies the N64's dither to the gamma curve, which adds fine noise to gradients. When disabled, the picture has no such noise.",
		                           { { "Off", 0 }, { "On", 1 } }, 0));
		s.rows.push_back(field_row("deinterlace", "Deinterlacing", "Selects how interlaced pictures are shown. Bob draws each field by itself, which keeps motion clean but can make thin lines flicker. Blend merges each field with the one before it, which softens flicker but can leave trails behind motion.",
		                           { { "Bob", 0 }, { "Blend", 1 } }, 0));
		s.rows.push_back(field_row("native_texrect", "Native-Resolution Texture Rectangles", "When enabled, draws texture rectangles, which games use for sprites and backgrounds, at the N64's resolution instead of the upscaled one. Can fix drawing errors in some games, but those parts stay soft at high resolutions.",
		                           { { "Off", 0 }, { "On", 1 } }, 0));
		s.rows.push_back(field_row("crop_overscan", "Crop Overscan", "Selects how many pixels are cropped at each edge of the N64's picture, which hides the borders and noise that some games draw there. The picture is enlarged to fill the screen.",
		                           { { "None", 0 }, { "8 Pixels", 8 }, { "16 Pixels", 16 } }, 0));
		s.rows.push_back(choose("video.vsync", "VSync", "When enabled, each frame waits for the display's refresh, which prevents tearing. When disabled, frames are shown as soon as they are ready.",
		                        { { "Off", 0 }, { "On", 1 } }, 1));
		s.rows.push_back(title_row("shader_compile", "Shader Compiles", "Determines how many background threads compile graphics shaders. Until a shader is ready, a general and slower one is used, so the game never waits for a compile. More threads can reduce stutter.",
		                           { { "1 Thread", 1 }, { "4 Threads", 4 } },
		                           [](TitleSettings &t) -> int64_t { return atoi(t.shaderCompile.c_str()); },
		                           [](TitleSettings &t, int64_t v) { t.shaderCompile = v == 4 ? "4" : "1"; }, 1));
		s.rows.push_back(title_row("driver_thread", "Graphics Driver", "Selects whether the graphics driver records and submits its work on a separate thread. Can improve performance in some games.",
		                           { { "Same Thread", 0 }, { "Separate Thread", 1 } },
		                           [](TitleSettings &t) -> int64_t { return t.driverThread; }, [](TitleSettings &t, int64_t v) { t.driverThread = v != 0; }, 0));
		s.rows.push_back(title_row("wave32", "Compute Waves", "Selects how many shader threads the GPU runs together: 64 is the driver's default, 32 can improve performance in some games. GPU Timing shows which is faster.",
		                           { { "64 Wide", 0 }, { "32 Wide", 1 } }, [](TitleSettings &t) -> int64_t { return t.wave32; },
		                           [](TitleSettings &t, int64_t v) { t.wave32 = v != 0; }, 0));
		s.rows.push_back(title_row("small_types", "8/16-Bit Math", "Allows shaders to use 8-bit and 16-bit arithmetic where the GPU has it. Can improve performance in some games. GPU Timing shows whether it does.",
		                           { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.smallTypes; },
		                           [](TitleSettings &t, int64_t v) { t.smallTypes = v != 0; }, 0));
		s.rows.push_back(title_row("rdp_layout", "Upscaled Memory", "Selects how the upscaled copy of the N64's memory is arranged. Per Pixel keeps each pixel's samples together and improves performance at high resolutions. Original may fix drawing errors in some games.",
		                           { { "Per Pixel", 2 }, { "Original", 0 } }, [](TitleSettings &t) -> int64_t { return t.rdpLayout; },
		                           [](TitleSettings &t, int64_t v) { t.rdpLayout = v == 0 ? 0 : 2; }, 2));
		s.rows.push_back(title_row("keep_gpu_memory", "Keep GPU Memory", "When enabled, keeps large blocks of graphics memory for reuse instead of returning them to the console, which can freeze a game for seconds when it switches picture modes at high resolutions. Uses up to 768 MB more memory.",
		                           { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.keepGpuMemory; },
		                           [](TitleSettings &t, int64_t v) { t.keepGpuMemory = v != 0; }, 1));
		s.rows.push_back(title_row("slow_capture", "Slow Moments", "When enabled, a game that runs below 95% speed for 3 seconds has the next 10 seconds recorded in the log, up to three times per game. Used to find what slows a game down.",
		                           { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.slowCapture; },
		                           [](TitleSettings &t, int64_t v) { t.slowCapture = v != 0; }, 1));
		s.rows.push_back(title_row("gpu_timing", "GPU Timing", "When enabled, writes the time the GPU spends in each drawing stage to the log every 10 seconds. Used to find what makes a slow game slow.",
		                           { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.gpuTiming; },
		                           [](TitleSettings &t, int64_t v) { t.gpuTiming = v != 0; }, 0));
		sections.push_back(std::move(s));
	}
	/* Screen and sound */
	{
		Section s{ "Screen & Sound", "adac", "Controls how the picture fills the screen and how much sound is kept queued.", {} };
		s.rows.push_back(choose("video.crt", "Apply CRT shader", "When enabled, adds scanlines and the glow of a CRT television to the picture.", { { "Off", 0 }, { "On", 1 } }, 0));
		s.rows.push_back(choose("video.widescreen", "Widescreen (stretch)", "When enabled, stretches the picture to 16:9. When disabled, the picture keeps its original 4:3 shape.", { { "Off", 0 }, { "On", 1 } }, 0));
		s.rows.push_back(choose("video.integer_scaling", "Integer Scaling", "When enabled, scales the picture by whole numbers only, so every pixel is the same size. May leave borders around the picture.", { { "Off", 0 }, { "On", 1 } }, 0));
		s.rows.push_back(title_row("audio_buffer", "Audio Buffer", "Determines how much audio is kept queued. A larger buffer prevents gaps when a game runs slowly, but adds delay.",
		                           { { "33 ms", 2 }, { "67 ms", 4 }, { "100 ms", 6 }, { "133 ms", 8 } },
		                           [](TitleSettings &t) -> int64_t { return t.audioBuffer; }, [](TitleSettings &t, int64_t v) { t.audioBuffer = (int)v; }, 4));
		sections.push_back(std::move(s));
	}
	/* TV output */
	{
		Section s{ "TV Output", "vdac", "Selects the display's refresh rate and HDR10 output.", {} };
		s.rows.push_back(title_row("display", "Refresh Rate", "Selects the display's refresh rate. 59.94 Hz is the rate of the N64; 119.88 Hz and VRR need a TV that supports them.",
		                           { { "59.94 Hz", 0 }, { "119.88 Hz", 1 }, { "VRR", 2 } },
		                           [](TitleSettings &t) -> int64_t { return t.display == "120" ? 1 : t.display == "vrr" ? 2 : 0; },
		                           [](TitleSettings &t, int64_t v) { t.display = v == 1 ? "120" : v == 2 ? "vrr" : "60"; }, 0));
		s.rows.push_back(title_row("hdr", "HDR10", "Uses HDR10 output when the console's output is set to HDR.", { { "SDR", 0 }, { "HDR10", 1 } },
		                           [](TitleSettings &t) -> int64_t { return t.hdr; }, [](TitleSettings &t, int64_t v) { t.hdr = v != 0; }, 0));
		Row white = title_row("hdr_white", "HDR White Level", "Sets the brightness of white in HDR. 203 nits is the reference level of HDR.", {}, [](TitleSettings &t) -> int64_t { return t.hdrWhite; },
		                      [](TitleSettings &t, int64_t v) { t.hdrWhite = (int)v; }, 203);
		white.kind = Row::Stepper, white.lo = 80, white.hi = 400, white.step = 10, white.unit = " nits";
		s.rows.push_back(white);
		Row peak = title_row("hdr_peak", "HDR Peak Brightness", "Sets the brightness that the brightest parts of the picture are raised to. 0 turns the raising off.", {}, [](TitleSettings &t) -> int64_t { return t.hdrPeak; },
		                     [](TitleSettings &t, int64_t v) { t.hdrPeak = (int)v; }, 1000);
		peak.kind = Row::Stepper, peak.lo = 0, peak.hi = 4000, peak.step = 100, peak.unit = " nits";
		s.rows.push_back(peak);
		sections.push_back(std::move(s));
	}
	/* Emulation */
	{
		Section s{ "Emulation", "xtal", "Controls the emulated N64: its clock, its memory and what the cartridge slot holds.", {} };
		s.rows.push_back(choose("emulation.overclock", "Overclock N64 CPU (may cause bugs)", "When enabled, runs the emulated CPU at 125 MHz instead of 93.75 MHz. Can reduce slowdowns in heavy games, but may cause issues in some games.",
		                        { { "Off", 0 }, { "On", 1 } }, 0));
		s.rows.push_back(choose("emulation.disable_expansion_pak", "Disable Expansion Pak", "When enabled, the N64 has its standard 4 MB of memory instead of the 8 MB it has with the Expansion Pak. Some games need the Expansion Pak, and a few misbehave with it.",
		                        { { "Off", 0 }, { "On", 1 } }, 0));
		s.rows.push_back(choose("emulation.rewind", "Enable Rewind", "When enabled, keeps a snapshot every second so the Rewind hotkey can go back in time. Uses additional memory.", { { "Off", 0 }, { "On", 1 } }, 0));
		s.rows.push_back(choose("emulation.usb", "Emulate UNFLoader", "When enabled, emulates the USB link of development cartridges that homebrew uses. Not supported on PS5.", { { "Off", 0 }, { "On", 1 } }, 0));
		sections.push_back(std::move(s));
	}
	/* Controllers */
	{
		Section s{ "Controllers", "port1", "Selects, for each port, whether a controller is plugged in, which DualSense plays it, its input profile and its Transfer Pak.", {} };
		for (int i = 0; i < 4; i++) {
			const std::string n = std::to_string(i + 1), idx = std::to_string(i);
			static std::string enabled_keys[4], assign_keys[4], profile_keys[4], tpak_keys[4], rom_keys[4], ram_keys[4];
			enabled_keys[i] = "input.controller_enabled." + idx, assign_keys[i] = "input.controller_assignment." + idx;
			profile_keys[i] = "input.input_profile_binding." + idx, tpak_keys[i] = "input.transfer_pak." + idx;
			rom_keys[i] = "input.gb_rom_path." + idx, ram_keys[i] = "input.gb_ram_path." + idx;
			s.rows.push_back(choose(enabled_keys[i].c_str(), "Port " + n + " \xC2\xB7 Enabled", "Determines whether a controller is plugged into port " + n + ".", { { "Off", 0 }, { "On", 1 } }, i == 0 ? 1 : 0));
			Row who;
			who.key = assign_keys[i], who.has_def = true, who.def = 0;
			who.label = "Port " + n + " \xC2\xB7 Controller Assignment", who.help = "Selects which DualSense plays port " + n + ". The players are numbered in the order the console signs them in.";
			who.options = { { "None", 0 }, { "DualSense 1", 1 }, { "DualSense 2", 2 }, { "DualSense 3", 3 }, { "DualSense 4", 4 } };
			markDefault(who);
			who.get = [this, i] {
				const std::string v = host_.text ? host_.text(assign_keys[i].c_str()) : "";
				return v.rfind("ps5:pad", 0) == 0 ? (int64_t)atoi(v.c_str() + 7) : (int64_t)0;
			};
			who.set = [this, i](int64_t v) { if (host_.set_text) host_.set_text(assign_keys[i].c_str(), v ? "ps5:pad" + std::to_string(v) : ""); };
			s.rows.push_back(who);
			Row prof;
			prof.key = profile_keys[i];
			prof.label = "Port " + n + " \xC2\xB7 Profile", prof.help = "Selects the input profile of port " + n + ", which assigns the DualSense controls to the N64 buttons. Profiles are edited in the Profiles section.";
			for (size_t p = 0; p < profiles.size(); p++)
				prof.options.push_back({ profiles[p], (int64_t)p });
			prof.get = [this, i, profiles] {
				const std::string v = host_.text ? host_.text(profile_keys[i].c_str()) : "default";
				const auto it = std::find(profiles.begin(), profiles.end(), v);
				return (int64_t)(it == profiles.end() ? 0 : it - profiles.begin());
			};
			prof.set = [this, i, profiles](int64_t v) {
				if (host_.set_text && v >= 0 && v < (int64_t)profiles.size())
					host_.set_text(profile_keys[i].c_str(), profiles[v]);
			};
			s.rows.push_back(prof);
			s.rows.push_back(choose(tpak_keys[i].c_str(), "Port " + n + " \xC2\xB7 Transfer Pak", "When enabled, plugs a Transfer Pak into port " + n + ". Its Game Boy ROM and save are selected below.",
			                        { { "Off", 0 }, { "On", 1 } }, 0));
			for (int which = 0; which < 2; which++) {
				Row file;
				file.kind = Row::File;
				const char *key = which == 0 ? rom_keys[i].c_str() : ram_keys[i].c_str();
				file.key = key;
				file.label = "Port " + n + (which == 0 ? " \xC2\xB7 Game Boy ROM" : " \xC2\xB7 Game Boy Save");
				file.help = which == 0 ? "Selects the Game Boy cartridge in the Transfer Pak of port " + n + ", from the console or a USB drive. Press \xE2\x97\x84 to clear it."
				                       : "Selects the save of the Game Boy cartridge in port " + n + ". When none is selected, one is created beside the ROM. Press \xE2\x97\x84 to clear it.";
				file.value = [this, key] {
					const std::string v = host_.text ? host_.text(key) : "";
					return v.empty() ? std::string("\xE2\x80\x94 None") : baseName(v);
				};
				file.run = [this, key, which] {
					picker_key_ = key, picker_rom_ = which == 0, picker_sel_ = 0;
					const std::string v = host_.text ? host_.text(key) : "";
					picker_folder_ = v.empty() ? "" : v.substr(0, v.rfind('/'));
				};
				file.clear = [this, key] { if (host_.set_text) host_.set_text(key, ""); };
				s.rows.push_back(file);
			}
		}
		s.rows.push_back(choose("input.emulate_vru", "Emulate VRU", "When enabled, plugs a Voice Recognition Unit into port 4, for games that use voice commands.", { { "Off", 0 }, { "On", 1 } }, 0));
		sections.push_back(std::move(s));
	}
	/* Profiles */
	{
		Section s{ "Profiles", "pif", "Assigns a DualSense control to each N64 button. Press \xE2\x9C\x95 on a button, then the control; \xE2\x97\x84 clears it.", {} };
		Row which;
		which.key = "profile.selected";
		which.label = "Profile", which.help = "Selects the input profile edited below. Each port selects its profile in the Controllers section.";
		for (size_t p = 0; p < profiles.size(); p++)
			which.options.push_back({ profiles[p], (int64_t)p });
		which.get = [this, profiles] {
			const auto it = std::find(profiles.begin(), profiles.end(), bench_profile_);
			return (int64_t)(it == profiles.end() ? 0 : it - profiles.begin());
		};
		which.set = [this, profiles](int64_t v) { if (v >= 0 && v < (int64_t)profiles.size()) bench_profile_ = profiles[v]; };
		s.rows.push_back(which);
		s.rows.push_back(action("profile.create", "Create Input Profile", [] { return std::string("Default Bindings"); }, "Creates a profile with the default bindings of Gopher64, to change here.",
		                        [this, profiles] {
			                        for (int k = 1; k < 100; k++) {
				                        const std::string name = "Profile " + std::to_string(k);
				                        if (std::find(profiles.begin(), profiles.end(), name) == profiles.end()) {
					                        if (host_.profile_new && host_.profile_new(name))
						                        bench_profile_ = name;
					                        break;
				                        }
			                        }
		                        }));
		s.rows.push_back(action("profile.delete", "Delete Input Profile", [this] { return bench_profile_ == "default" ? std::string("Default Profile") : bench_profile_; },
		                        "Deletes the selected profile. Ports that used it switch to the default profile, which cannot be deleted.", [this] {
			                        if (bench_profile_ != "default" && host_.profile_delete && host_.profile_delete(bench_profile_))
				                        bench_profile_ = "default";
		                        }));
		for (int input = 0; input < 19; input++)
			s.rows.push_back(bind_row("profile.input." + std::to_string(input), bench_profile_, input, kN64Inputs[input],
			                          input == 18 ? "Sets the control that is held together with another button to make that button a hotkey. See the Hotkeys section."
			                                      : std::string("Sets the DualSense control for ") + kN64Inputs[input] + ". Press \xE2\x9C\x95, then the control."));
		Row dead;
		dead.kind = Row::Stepper, dead.key = "profile.deadzone", dead.label = "Deadzone Percentage", dead.help = "Sets how far a stick moves before the N64 registers it.";
		dead.lo = 0, dead.hi = 50, dead.step = 1, dead.unit = " %";
		dead.get = [this] { return host_.profile_number ? host_.profile_number(bench_profile_, 0, 0, false) : 0; };
		dead.set = [this](int64_t v) { if (host_.profile_number) host_.profile_number(bench_profile_, 0, v, true); };
		s.rows.push_back(dead);
		Row raw;
		raw.key = "profile.dinput", raw.has_def = true, raw.def = 0;
		raw.label = "Use DirectInput", raw.help = "When enabled, reads a raw joystick instead of a gamepad. The bindings are then those of a joystick, which can only be made on a PC.";
		raw.options = { { "Off", 0 }, { "On", 1 } };
		raw.get = [this] { return host_.profile_number ? host_.profile_number(bench_profile_, 1, 0, false) : 0; };
		raw.set = [this](int64_t v) { if (host_.profile_number) host_.profile_number(bench_profile_, 1, v, true); };
		s.rows.push_back(raw);
		sections.push_back(std::move(s));
	}
	/* Hotkeys */
	{
		static const char *const hotkey_help[15] = {
			"Selects the button that saves a state to the current slot.",
			"Selects the button that loads the state in the current slot.",
			"Selects the button that leaves the game and returns to the library.",
			"Selects the button that fast forwards the game.",
			"Selects the button that rewinds the game. Requires Enable Rewind.",
			"Selects the button that changes the pak plugged into the controller.",
			"Selects the button that resets the game.",
			"Selects the button that pauses the game.",
			"Selects the button that advances a paused game by one frame.",
			"Selects the button that lowers the volume.",
			"Selects the button that raises the volume.",
			"Selects the button that shows the frame rate.",
			"Selects the button that crops the letterbox around the picture.",
			"Selects the button that switches to the next save slot.",
			"Selects the button that switches to the previous save slot.",
		};
		Section s{ "Hotkeys", "cpu", "Selects the button for each hotkey action, used by holding the Hotkey Activator and pressing it. Each action can use any N64 button or be turned off.", {} };
		s.rows.push_back(title_row("hotkeys", "Enable Hotkeys", "When enabled, hotkeys work while the Hotkey Activator is held. When disabled, none of them work.", { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.hotkeys; },
		                           [](TitleSettings &t, int64_t v) { t.hotkeys = v != 0; }, 1));
		Row tap = title_row("quickmenu_tap", "Quick Menu Tap", "When enabled, tapping the touch pad opens the quick menu. It stays enabled while there is no other way to leave a game.",
		                    { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.quickmenuTap; },
		                    [](TitleSettings &t, int64_t v) { t.quickmenuTap = v != 0; }, 1);
		s.rows.push_back(tap);
		s.rows.push_back(bind_row("hotkey.activator", port1_profile, 18, "Hotkey Activator (Port 1)", "Sets the control held to use a hotkey, in the profile of port 1 (\"" + port1_profile + "\"). The default is the touch pad."));
		for (size_t a = 0; a < 15; a++) {
			Row r;
			r.key = std::string("hotkey.") + kHotkeyActions[a].key, r.has_def = true, r.def = TitleSettings{}.hotkey[a];
			r.label = kHotkeyActions[a].label;
			r.help = hotkey_help[a];
			r.options.push_back({ "Off", -1 });
			for (int input = 0; input < 14; input++) {
				const Binding b = host_.binding ? host_.binding(port1_profile, input) : Binding{};
				r.options.push_back({ std::string(kN64Inputs[input]) + (b.kind ? " \xC2\xB7 " + bindingLabel(b) : ""), input });
			}
			markDefault(r);
			r.get = [ts, a]() -> int64_t { return ts ? ts->hotkey[a] : -1; };
			r.set = [ts, a](int64_t v) {
				if (ts) {
					ts->hotkey[a] = (int)v;
					ts->save();
				}
			};
			s.rows.push_back(r);
		}
		sections.push_back(std::move(s));
	}
	/* Console */
	{
		Section s{ "Console", "slot", "Controls network shares, access outside the sandbox, the game library, copying and updates.", {} };
		char shares[48];
		snprintf(shares, sizeof(shares), "%d", host_.share_count ? host_.share_count() : 0);
		const std::string shares_text = shares;
		s.rows.push_back(action("console.shares", "Network Shares", [shares_text] { return shares_text; }, "Allows adding, changing and removing SMB and NFS shares.", [this] { if (host_.shares) host_.shares(); }));
		const bool off = host_.elevation_off && host_.elevation_off();
		const std::string elevation = off ? "Off" : host_.elevated && host_.elevated() ? "On" : "Unavailable";
		s.rows.push_back(action("console.sandbox", "Leave the Sandbox", [elevation] { return elevation; }, "When enabled, leaves the sandbox to reach USB drives and all of /data.",
		                        [this, off] { if (host_.set_elevation_off) host_.set_elevation_off(!off); }));
		s.rows.push_back(action("console.search", "Search for Cartridges", [] { return std::string(library().scanning() ? "Searching" : "Search"); }, "Searches every location for cartridges again.",
		                        [] { library().rescan(); }));
		/* Fast Writes: the write broker (write_broker.h) */
		s.rows.push_back(title_row("write_helper", "Fast Writes (Helper)",
		                           "Uses a helper to write copied games to the console's storage, which keeps writing fast after the first gigabyte. When disabled, the title writes them itself.",
		                           { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.writeHelper; },
		                           [](TitleSettings &t, int64_t v) { t.writeHelper = v != 0; }, 1));
		/* the updater: a check asked for by the user (and at the title's start unless that is off), its offer
		 * with the notes below the rows, and what follows from it (update.h) */
		s.rows.push_back(title_row("update_check", "Check for Updates at Start", "When enabled, checks for a newer release each time the title starts. Nothing is downloaded without asking.",
		                           { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.updateCheck; },
		                           [](TitleSettings &t, int64_t v) { t.updateCheck = v != 0; }, 1));
		const update::Status us = host_.update_status ? host_.update_status() : update::Status{};
		const bool us_idle = updateIdle(us);
		const bool us_offered = us.state == update::State::Available;
		const bool us_busy = us.state == update::State::Downloading || us.state == update::State::Verifying;
		const std::string us_tag = releaseTag(us);
		s.rows.push_back(action("console.update", "Check for Updates", [us] { return updateValue(us); },
		                        "Checks whether a newer release is available and shows what it adds. Nothing is downloaded without asking.",
		                        [this, us_idle] { if (us_idle && host_.update_check) host_.update_check(); }));
		{
			Row r = action("console.update_download", "Download Update", [] { return std::string("Download"); },
			               "Downloads the release and checks it against its SHA-256 digest before anything is replaced.",
			               [this] { if (host_.update_download) host_.update_download(); });
			r.shown = [us_offered] { return us_offered; };
			s.rows.push_back(r);
			r = action("console.update_cancel", "Cancel Download", [] { return std::string("Cancel"); },
			           "Stops the download. Nothing in the title's folder is changed.", [this] { if (host_.update_cancel) host_.update_cancel(); });
			r.shown = [us_busy, us] { return us_busy && !us.rollback; };
			s.rows.push_back(r);
			r = action("console.update_install", "Install Update", [] { return std::string("Install"); },
			           "Replaces the title's program files with the downloaded release, keeping a backup of them, and closes the title. Games, saves and settings stay.",
			           [this, us_tag, us] { if (us.state == update::State::Ready) update_ask_ = 1, update_ask_text_ = "Install " + us_tag + "?"; });
			r.shown = [us] { return us.state == update::State::Ready; };
			s.rows.push_back(r);
			const bool skipped = us.skipped;
			r = action("console.update_skip", "Skip This Version", [skipped] { return std::string(skipped ? "Skipped" : "Skip"); },
			           "Skips the release on offer. A later release is offered again, and Check for Updates still shows this one.",
			           [this, us_offered, skipped] { if (us_offered && !skipped && host_.update_skip) host_.update_skip(); });
			r.shown = [us_offered] { return us_offered; };
			s.rows.push_back(r);
			const std::string previous = us.previous_label.empty() ? us.previous : us.previous_label;
			r = action("console.update_rollback", "Roll Back", [previous] { return previous; },
			           "Puts back the files that the last update replaced, after checking them, and closes the title. Games, saves and settings stay.",
			           [this, previous, us_idle] { if (us_idle) update_ask_ = 2, update_ask_text_ = "Roll back to " + previous + "?"; });
			r.shown = [us, us_idle] { return !us.previous.empty() && us_idle && us.state != update::State::Installed; };
			s.rows.push_back(r);
		}
		s.rows.push_back(action("console.exit", "Exit to Home Screen", [] { return std::string("Exit"); }, "Closes the title and returns to the home screen of the console.", [this] { if (host_.close) host_.close(); }));
		sections.push_back(std::move(s));
	}
	/* What Gopher64 has on a PC only */
	{
		Section s{ "PC Only", "slot", "Shows the Gopher64 settings that are not available on PS5, and why.", {} };
		s.rows.push_back(info("video.fullscreen", "Fullscreen", "Always On", "The picture always fills the whole screen."));
		s.rows.push_back(info("pc.keyboard", "Keyboard Bindings", "No Keyboard", "On a PC, the first binding of each input is a keyboard key. On PS5, the second binding, the controller's, is used."));
		s.rows.push_back(info("pc.library", "Theme, ROM Folder, Recent ROMs", "The Library", "Gopher64's window holds these. The game library takes their place here."));
		s.rows.push_back(info("pc.network", "Netplay, RetroAchievements", "Not Available", "Both need network features that are not available on PS5."));
		sections.push_back(std::move(s));
	}
	if (g_settings_words_dump) {
		dumpWords(g_settings_words_dump, sections);
		fclose(g_settings_words_dump);
		g_settings_words_dump = nullptr;
	}

	/* rows that depend on the updater's state are left out while they do not apply */
	for (Section &sc : sections)
		sc.rows.erase(std::remove_if(sc.rows.begin(), sc.rows.end(), [](const Row &r) { return r.shown && !r.shown(); }), sc.rows.end());
	/* a game's page: its cheats, then the rows a game can have of its own, each with Use Global Setting first
	 * (BENCH's rows themselves, changed to read and write the game's file; the words were dumped above) */
	if (game_page_) {
		std::vector<Section> page;
		{
			Section c{ "Cheats", "slot", "", {} };
			c.rows.push_back(action("page.cheats", "Cheats", [] { return std::string("Open"); },
			                        "Opens the cheats Gopher64 has for this game, to turn each on or off.", [this] {
				                        readCheats(page_game_);
				                        if (cheats_.empty())
					                        host_.tell(page_game_.crc.empty() ? "No cheats: this cartridge's header could not be read" : "Gopher64 has no cheats for this cartridge");
				                        else
					                        cheats_open_ = 0;
			                        }));
			page.push_back(std::move(c));
		}
		for (Section &sc : sections) {
			if (sc.name != "Video" && sc.name != "Screen & Sound" && sc.name != "Emulation" && sc.name != "Controllers")
				continue;
			Section out{ sc.name, sc.part, "", {} };
			for (Row &r : sc.rows) {
				if (r.kind != Row::Choice || !gameSettingEligible(r.key))
					continue;
				Row p = r;
				const std::string key = r.key;
				p.options.insert(p.options.begin(), { "Use Global Setting", kUseGlobal });
				p.inherited = r.get;
				p.get = [this, key]() -> int64_t {
					const int64_t *v = page_overrides_.find(key);
					return v ? *v : kUseGlobal;
				};
				p.set = [this, key](int64_t v) {
					if (v == kUseGlobal)
						page_overrides_.clear(key);
					else
						page_overrides_.set(key, v);
					if (!saveGameSettings(page_key_, page_overrides_) && host_.tell)
						host_.tell("This game's settings could not be written");
					marker_path_.clear(); /* (BENCH's markers read the file again) */
				};
				p.help = r.help + " Use Global Setting leaves it to the setting in BENCH.";
				out.rows.push_back(std::move(p));
			}
			if (!out.rows.empty())
				page.push_back(std::move(out));
		}
		for (Section &sc : page)
			if (sc.text.empty())
				sc.text = "Sets rows for " + titleOf(page_game_) + " only. Each starts at Use Global Setting; the other games keep the global settings.";
		sections = std::move(page);
	}
	const int ns = (int)sections.size();
	bench_section_ = std::clamp(bench_section_, 0, ns - 1);
	Section &sec = sections[bench_section_];
	const int n = (int)sec.rows.size();
	bench_sel_ = std::clamp(bench_sel_, 0, n - 1);

	/* binding an input: the press after every control was let go; 6 s without one, nothing changes */
	if (capture_input_ >= 0) {
		Binding b;
		if (!capture_released_)
			capture_released_ = padAtRest(pad);
		else if (padBinding(pad, &b)) {
			if (host_.bind)
				host_.bind(capture_profile_, capture_input_, b);
			capture_input_ = -1;
		}
		if (capture_input_ >= 0 && pad.now - capture_since_ > 6)
			capture_input_ = -1;
	} else if (picker_sel_ >= 0) {
		/* the file picker has the pad (drawn last) */
	} else {
		if (pad.pressed & PAD_R2_DIGITAL)
			bench_section_ = (bench_section_ + 1) % ns, bench_sel_ = 0, bench_scroll_ = 0;
		if (pad.pressed & PAD_L2_DIGITAL)
			bench_section_ = (bench_section_ + ns - 1) % ns, bench_sel_ = 0, bench_scroll_ = 0;
		if (pad.repeat(PAD_DOWN))
			bench_sel_ = (bench_sel_ + 1) % n;
		if (pad.repeat(PAD_UP))
			bench_sel_ = (bench_sel_ + n - 1) % n;
		if (pad.pressed & PAD_CIRCLE) {
			if (game_page_) {
				game_page_ = false;
				bench_section_ = page_saved_section_, bench_sel_ = page_saved_sel_, bench_scroll_ = 0;
			}
			setView(board::View::Home);
		}
		Row &r = sections[bench_section_].rows[std::clamp(bench_sel_, 0, (int)sections[bench_section_].rows.size() - 1)];
		switch (r.kind) {
		case Row::Choice: {
			if (r.options.empty())
				break;
			const int64_t v = r.get ? r.get() : 0;
			int at = 0;
			for (size_t o = 0; o < r.options.size(); o++)
				if (r.options[o].value == v)
					at = (int)o;
			const int m = (int)r.options.size();
			if (pad.repeat(PAD_RIGHT) || (pad.pressed & PAD_CROSS))
				r.set(r.options[(at + 1) % m].value);
			else if (pad.repeat(PAD_LEFT))
				r.set(r.options[(at + m - 1) % m].value);
			break;
		}
		case Row::Stepper: {
			const int64_t v = r.get ? r.get() : 0;
			if (pad.repeat(PAD_RIGHT))
				r.set(std::min(r.hi, v + r.step));
			if (pad.repeat(PAD_LEFT))
				r.set(std::max(r.lo, v - r.step));
			break;
		}
		case Row::Action:
		case Row::File:
		case Row::Bind:
			if ((pad.pressed & PAD_CROSS) && r.run)
				r.run();
			else if (pad.repeat(PAD_LEFT) && r.clear)
				r.clear();
			break;
		case Row::Info:
			break;
		}
	}

	/* the line from the panel to the section's part, under the panels */
	{
		const ImVec2 part = projector_(board::anchor(sec.part.c_str()));
		dl->AddLine(S(980, 190), part, IM_COL32(255, 182, 72, 230), L(2));
		dl->AddCircleFilled(part, L(8), kAmber);
	}
	const ImU32 solid = IM_COL32(7, 17, 21, 255);
	/* the sections, down the left */
	{
		const float x = 64, y0 = 150, w = 300, rh = 50;
		ink.rect(x, y0, w, ns * rh + 24, solid, 14);
		ink.frame(x, y0, w, ns * rh + 24, kLine, 14, 1);
		for (int i = 0; i < ns; i++) {
			const float y = y0 + 12 + i * rh;
			const bool on = i == bench_section_;
			if (on)
				ink.rect(x + 8, y, w - 16, rh - 6, kAmber, 8);
			ink.text(f.display, 20, x + 22, y + 10, on ? kInk : kText, sections[i].name);
		}
		ink.text(f.mono, 13, x + 4, y0 + ns * rh + 34, kDim, "L2 \xC2\xB7 R2  section", .06f);
	}
	/* the panel: the section's rows */
	{
		const float x = 980, y0 = 150, w = 876, h = 860, rh = 44;
		ink.rect(x, y0, w, h, solid, 16);
		ink.frame(x, y0, w, h, kAmber, 16, 2);
		ink.text(f.display, 28, x + 28, y0 + 18, kText, sec.name, -.01f);
		/* the game's own settings: on its page how many rows it has set, in BENCH a marker for the chosen game's */
		const Game *marked = game_page_ ? &page_game_ : selected();
		const GameSettings *own = game_page_ ? &page_overrides_ : marked ? &markersOf(*marked) : nullptr;
		if (own && !own->empty()) {
			char tag[160];
			snprintf(tag, sizeof(tag), game_page_ ? "%zu SET FOR THIS GAME \xC2\xB7 %s" : "\xE2\x97\x86 %zu SET FOR %s", own->values.size(),
			         game_page_ ? page_key_.c_str() : marker_key_.c_str());
			const float tw = ink.width(f.mono_bold, 13, tag);
			ink.text(f.mono_bold, 13, x + w - 28 - tw, y0 + 26, kAmber, tag, .06f);
		} else if (game_page_) {
			const float tw = ink.width(f.mono, 13, page_key_);
			ink.text(f.mono, 13, x + w - 28 - tw, y0 + 26, kDim, page_key_, .06f);
		}
		const auto lines = wrap(ink, f.mono, 14, sec.text, w - 56, 2);
		float ty = y0 + 60;
		for (const std::string &l : lines) {
			ink.text(f.mono, 14, x + 28, ty, kDim, l);
			ty += 20;
		}
		const float list_top = y0 + 112, list_h = h - 112 - 96;
		const int visible = std::max(1, (int)(list_h / rh));
		const float target = (float)std::clamp(bench_sel_ - visible / 2, 0, std::max(0, n - visible));
		bench_scroll_ += (target - bench_scroll_) * .35f;
		const int first = std::max(0, (int)bench_scroll_);
		for (int i = first; i < std::min(n, first + visible + 1); i++) {
			const float y = list_top + (i - bench_scroll_) * rh;
			if (y < list_top - 1 || y + rh > list_top + list_h + 1)
				continue;
			const Row &r = sec.rows[i];
			const bool here = i == bench_sel_;
			if (here)
				ink.rect(x + 12, y, w - 24, rh - 4, IM_COL32(255, 182, 72, 34), 8);
			const bool owned = own && !own->empty() && own->find(r.key) != nullptr;
			if (owned) {
				const float cx = x + 20, cy = y + rh / 2 - 2, d = 5;
				const ImVec2 diamond[4] = { S(cx, cy - d), S(cx + d, cy), S(cx, cy + d), S(cx - d, cy) };
				dl->AddConvexPolyFilled(diamond, 4, kAmber);
			}
			ink.text(f.mono_bold, 16, x + 28, y + 11, here ? kText : kDim, r.label);
			/* the value, right-aligned: ◄ value ► for what left and right change */
			std::string v;
			switch (r.kind) {
			case Row::Choice: {
				const int64_t cur = r.get ? r.get() : 0;
				v = "?";
				for (const Option &o : r.options)
					if (o.value == cur)
						v = o.label;
				if (cur == kUseGlobal && r.inherited) {
					/* the global setting's choice, beside it */
					const int64_t global = r.inherited();
					for (const Option &o : r.options)
						if (o.value == global && o.value != kUseGlobal) {
							std::string name = o.label;
							const size_t at = name.find(" (Default)");
							if (at != std::string::npos)
								name.erase(at);
							v += " \xC2\xB7 " + name;
						}
				}
				v = "\xE2\x97\x84 " + v + " \xE2\x96\xBA";
				break;
			}
			case Row::Stepper:
				v = "\xE2\x97\x84 " + std::to_string(r.get ? r.get() : 0) + r.unit + " \xE2\x96\xBA";
				break;
			case Row::Action:
				v = (r.value ? r.value() : "") + "  \xE2\x9C\x95";
				break;
			case Row::File:
			case Row::Bind:
				v = (r.value ? r.value() : "") + "  \xE2\x9C\x95";
				break;
			case Row::Info:
				v = r.value ? r.value() : "";
				break;
			}
			const float vw = ink.width(f.mono_bold, 16, v);
			const bool capturing = r.kind == Row::Bind && capture_input_ >= 0 && here;
			if (here && r.kind != Row::Info)
				ink.rect(x + w - 40 - vw, y + 4, vw + 20, rh - 12, capturing ? kText : kAmber, 6);
			ink.text(f.mono_bold, 16, x + w - 30 - vw, y + 11, here && r.kind != Row::Info ? kInk : (r.kind == Row::Info ? kDim : kText), v);
		}
		/* Console: what the updater offers or is doing, under the rows: its state, the progress and the notes */
		if (sec.name == "Console" && host_.update_status) {
			const update::Status us = host_.update_status();
			const bool notes_wanted = us.state == update::State::Available || us.state == update::State::Downloading ||
			                          us.state == update::State::Verifying || us.state == update::State::Ready;
			float ty = list_top + n * rh + 10;
			const float bottom = y0 + h - 100, tx = x + 28, tw = w - 56;
			std::string head;
			ImU32 head_colour = kAmber;
			switch (us.state) {
			case update::State::Available:
				head = releaseTag(us) + (us.name.empty() || us.name == releaseTag(us) ? "" : "  " + us.name) + (us.skipped ? "  (skipped)" : "");
				break;
			case update::State::Downloading: head = us.rollback ? "Checking the kept files" : "Downloading " + releaseTag(us); break;
			case update::State::Verifying: head = us.rollback ? "Checking the kept files" : "Checking the download"; break;
			case update::State::Ready: head = releaseTag(us) + " is downloaded and checked: Install Update puts it in place"; break;
			case update::State::Installed: head = "Installed: the title closes"; break;
			case update::State::UpToDate:
				head = us.developer_build ? "This is a developer build" + (us.tag.empty() ? std::string() : "; the newest release is " + us.tag) : "This is the newest release";
				head_colour = kDim;
				break;
			case update::State::NoRelease: head = us.error.empty() ? "No release has been published yet" : us.error; break;
			case update::State::Failed: head = us.error.empty() ? "The update did not work" : us.error, head_colour = IM_COL32(255, 128, 104, 255); break;
			default: break;
			}
			if (!head.empty() && ty < bottom) {
				ink.rect(x + 12, ty - 6, w - 24, 1, kLine);
				const auto hl = wrap(ink, f.mono_bold, 15, head, tw, 2);
				for (const std::string &l : hl) {
					ink.text(f.mono_bold, 15, tx, ty, head_colour, l);
					ty += 22;
				}
				if (us.state == update::State::Downloading || us.state == update::State::Verifying) {
					const double part = us.state == update::State::Verifying ? 1.0 : us.total ? std::min(1.0, (double)us.done / (double)us.total) : 0.0;
					ink.rect(tx, ty + 2, tw, 10, IM_COL32(16, 34, 38, 255), 5);
					if (part > 0)
						ink.rect(tx, ty + 2, (float)(tw * part), 10, kAmber, 5);
					ty += 22;
					if (us.state == update::State::Downloading) {
						std::string line = fmtBytes(us.done) + (us.total ? " of " + fmtBytes(us.total) : "");
						if (us.speed > 0)
							line += "   " + fmtBytes((uint64_t)us.speed) + "/s";
						ink.text(f.mono, 14, tx, ty, kText, line);
						ty += 22;
					}
				}
				if (notes_wanted && ty < bottom - 20) {
					ty += 4;
					const int room = std::max(1, (int)((bottom - ty) / 20));
					for (const std::string &l : wrapParagraphs(ink, f.mono, 14, updateNotesText(us), tw, room)) {
						ink.text(f.mono, 14, tx, ty, kDim, l);
						ty += 20;
					}
				}
			}
		}
		/* the scroll's place, when the list is longer than the panel */
		if (n > visible) {
			const float bar_h = list_h * visible / n, bar_y = list_top + (list_h - bar_h) * bench_scroll_ / std::max(1, n - visible);
			ink.rect(x + w - 10, bar_y, 4, bar_h, kLine, 2);
		}
		/* the chosen row's help */
		const Row &r = sec.rows[bench_sel_];
		ink.rect(x + 12, y0 + h - 88, w - 24, 1, kLine);
		const auto help = wrap(ink, f.mono, 14, r.help, w - 56, 3);
		float hy = y0 + h - 76;
		for (const std::string &l : help) {
			ink.text(f.mono, 14, x + 28, hy, kText, l);
			hy += 20;
		}
	}
	ink.text(f.mono, 14, 64, 1046, kDim,
	         game_page_ ? "L2 \xC2\xB7 R2  SECTION      \xE2\x96\xB2 \xE2\x96\xBC  SETTING      \xE2\x97\x84 \xE2\x96\xBA  CHANGE      \xE2\x9C\x95  CHOOSE      \xE2\x97\x8B  BACK"
	                    : "L1 \xC2\xB7 R1  VIEWS      L2 \xC2\xB7 R2  SECTION      \xE2\x96\xB2 \xE2\x96\xBC  SETTING      \xE2\x97\x84 \xE2\x96\xBA  CHANGE      \xE2\x9C\x95  CHOOSE      \xE2\x97\x8B  HOME",
	         .08f);
	if (picker_sel_ >= 0)
		filePicker(dl, pad, f);
}

/* A Game Boy ROM or save for a Transfer Pak: folders and files of the console and its USB drives
 * (Gopher64 reads them itself: a network share's are not reachable from it) */
void Frontend::filePicker(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f)
{
	Ink ink{ dl, u_ };
	struct Entry {
		std::string label, path;
		bool folder;
	};
	std::vector<Entry> entries;
	if (picker_folder_.empty()) {
		entries.push_back({ "/data", "/data", true });
		for (int i = 0; i < 8; i++) {
			const std::string usb = "/mnt/usb" + std::to_string(i);
			struct stat st;
			if (stat(usb.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
				entries.push_back({ usb, usb, true });
		}
	} else {
		entries.push_back({ "\xE2\x86\x90 up", picker_folder_.substr(0, picker_folder_.rfind('/')), true });
		std::vector<Entry> folders, files;
		if (DIR *d = opendir(picker_folder_.c_str())) {
			while (dirent *e = readdir(d)) {
				if (e->d_name[0] == '.')
					continue;
				const std::string path = picker_folder_ + "/" + e->d_name;
				struct stat st;
				if (stat(path.c_str(), &st) != 0)
					continue;
				std::string lower = e->d_name;
				for (char &c : lower)
					c = (char)tolower((unsigned char)c);
				auto ends = [&](const char *t) { return lower.size() >= strlen(t) && lower.compare(lower.size() - strlen(t), strlen(t), t) == 0; };
				if (S_ISDIR(st.st_mode))
					folders.push_back({ std::string(e->d_name) + "/", path, true });
				else if (picker_rom_ ? (ends(".gb") || ends(".gbc")) : (ends(".sav") || ends(".srm") || ends(".ram")))
					files.push_back({ e->d_name, path, false });
			}
			closedir(d);
		}
		auto by_name = [](const Entry &a, const Entry &b) { return a.label < b.label; };
		std::sort(folders.begin(), folders.end(), by_name);
		std::sort(files.begin(), files.end(), by_name);
		entries.insert(entries.end(), folders.begin(), folders.end());
		entries.insert(entries.end(), files.begin(), files.end());
		if (!picker_rom_) {
			/* a save beside the ROM, made by Gopher64 when it is not there */
			std::string rom = host_.text ? host_.text(("input.gb_rom_path." + picker_key_.substr(picker_key_.size() - 1)).c_str()) : "";
			if (!rom.empty()) {
				const size_t dot = rom.rfind('.');
				entries.insert(entries.begin() + 1, { "beside the ROM: " + baseName(rom.substr(0, dot)) + ".sav", rom.substr(0, dot) + ".sav", false });
			}
		}
	}
	const int n = (int)entries.size();
	picker_sel_ = std::clamp(picker_sel_, 0, std::max(0, n - 1));
	if (pad.repeat(PAD_DOWN) && n)
		picker_sel_ = (picker_sel_ + 1) % n;
	if (pad.repeat(PAD_UP) && n)
		picker_sel_ = (picker_sel_ + n - 1) % n;
	if (pad.pressed & PAD_CIRCLE) {
		picker_sel_ = -1;
		return;
	}
	if ((pad.pressed & PAD_CROSS) && n) {
		const Entry &e = entries[picker_sel_];
		if (e.folder) {
			picker_folder_ = (e.path == "/mnt" || e.path.empty()) ? "" : e.path;
			if (picker_folder_ == "/" )
				picker_folder_.clear();
			picker_sel_ = 0;
		} else {
			if (host_.set_text)
				host_.set_text(picker_key_.c_str(), e.path);
			picker_sel_ = -1;
			return;
		}
	}
	const float x = 520, y = 150, w = 880, h = 860, rh = 40;
	dl->AddRectFilled(S(0, 0), S(1920, 1080), IM_COL32(0, 0, 0, 150));
	ink.rect(x, y, w, h, IM_COL32(7, 17, 21, 250), 16);
	ink.frame(x, y, w, h, kAmber, 16, 2);
	ink.text(f.display, 26, x + 28, y + 18, kText, picker_rom_ ? "Game Boy ROM" : "Game Boy save");
	ink.text(f.mono, 14, x + 28, y + 56, kDim, picker_folder_.empty() ? "the console and its USB drives" : picker_folder_);
	const int visible = (int)((h - 140) / rh);
	const int first = std::clamp(picker_sel_ - visible / 2, 0, std::max(0, n - visible));
	for (int i = first; i < std::min(n, first + visible); i++) {
		const float ry = y + 90 + (i - first) * rh;
		const bool here = i == picker_sel_;
		if (here)
			ink.rect(x + 12, ry, w - 24, rh - 4, kAmber, 8);
		ink.text(f.mono_bold, 16, x + 28, ry + 9, here ? kInk : (entries[i].folder ? kText : kDim), entries[i].label);
	}
	if (n == 1 && !picker_folder_.empty())
		ink.text(f.mono, 14, x + 28, y + 140, kDim, picker_rom_ ? "No .gb or .gbc files here" : "No .sav files here");
	ink.text(f.mono, 14, x + 28, y + h - 34, kDim, "\xE2\x96\xB2 \xE2\x96\xBC  MOVE      \xE2\x9C\x95  OPEN \xC2\xB7 CHOOSE      \xE2\x97\x8B  CLOSE", .08f);
}

} // namespace psgopher
