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
	view_ = v;
	blend_ = 0;
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
	if (view_ == board::View::Rack && rack_sel_ < (int)rack_.size())
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

/* ---------------------------------------------------------------- the frame */

void Frontend::frame(ImDrawList *dl, const PadInput &pad, float w, float h, float dt, ImTextureRef boardTex, ImTextureRef logo,
                     const FrontendFonts &f)
{
	refresh();
	u_ = h / board::kStageH;
	const float stage_w = w / u_; // (16:9: 1920)
	(void)stage_w;
	Ink ink{ dl, u_ };
	const Game *g = selected();
	const board::Look look = g ? lookFor(*g) : board::Look{};

	/* the views: L1/R1 (not while the probe picker is up) */
	if (probes_open_ < 0) {
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
				if (name.rfind(stem, 0) != 0 || name.find(".state") == std::string::npos)
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
		fingerprint(ink, file, look, x + pad_x, cy, 112, 42);
		snprintf(path, sizeof(path), "Probe %d", probes_[i].slot);
		ink.text(f.mono_bold, 15, x + pad_x + 126, cy + 2, withAlpha(kText, alpha), path);
		ink.text(f.mono, 15, x + pad_x + 126, cy + 22, withAlpha(kDim, alpha), agoText(probes_[i].time, true));
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
	if (g && (pad.pressed & PAD_TRIANGLE)) {
		readCheats(*g);
		if (cheats_.empty())
			host_.tell(g->crc.empty() ? "No cheats: this cartridge's header could not be read" : "Gopher64 has no cheats for this cartridge");
		else
			cheats_open_ = 0;
	}

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
	ink.text(f.mono, 14, 64, 1046, kDim, "L1 \xC2\xB7 R1  VIEWS      \xE2\x97\x84 \xE2\x96\xBA  CARTRIDGE      \xE2\x9C\x95  POWER ON      \xE2\x96\xA1  PROBES      \xE2\x96\xB3  CHEATS", .08f);
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
		ink.text(f.mono_bold, 18, x + 48, cy + 16, on ? kInk : kText, t);
		const std::string when = agoText(probes_[i].time, true);
		ink.text(f.mono, 15, x + w - 48 - ink.width(f.mono, 15, when), cy + 18, on ? kInk : kDim, when);
		cy += 62;
	}
	ink.text(f.mono, 14, 64, 1046, kDim, "\xE2\x96\xB2 \xE2\x96\xBC  PROBE      \xE2\x9C\x95  POWER ON FROM IT      \xE2\x97\x8B  BACK", .08f);
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
				if (dash != std::string::npos && name.find(".state") != std::string::npos)
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
		/* L2/R2: the next letter (A-Z order) */
		if ((pad.pressed & (PAD_L2_DIGITAL | PAD_R2_DIGITAL)) && sort_ == 0) {
			const int d = (pad.pressed & PAD_R2_DIGITAL) ? 1 : -1;
			const char now = letterOf(games_[rack_[rack_sel_]]);
			int k = rack_sel_;
			if (d > 0) {
				while (k < n && letterOf(games_[rack_[k]]) == now)
					k++;
				while (k < n && !visible(k))
					k++;
				if (k < n)
					rack_sel_ = k;
			} else {
				while (k > 0 && letterOf(games_[rack_[k - 1]]) == now)
					k--;
				k--;
				if (k >= 0) {
					const char prev = letterOf(games_[rack_[k]]);
					while (k > 0 && letterOf(games_[rack_[k - 1]]) == prev)
						k--;
					while (k < n && !visible(k))
						k++;
					rack_sel_ = std::min(k, n - 1);
				}
			}
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
	         "\xE2\x97\x84 \xE2\x96\xBA \xE2\x96\xB2 \xE2\x96\xBC  CARTRIDGE      L2 \xC2\xB7 R2  LETTER      \xE2\x9C\x95  POWER ON      \xE2\x96\xB3  SEAT IT",
	         .08f);

	if (g)
		panel(dl, f, *g, true, 1920 - 64 - 470, 150, 1);
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
	std::string label, help;
	std::vector<Option> options;                 // Choice
	std::function<int64_t()> get;                // Choice, Stepper
	std::function<void(int64_t)> set;
	int64_t lo = 0, hi = 0, step = 1;            // Stepper
	std::string unit;
	std::function<std::string()> value;          // Action, File, Bind, Info: what is shown
	std::function<void()> run;                   // Action, File (the picker), Bind (press a button)
	std::function<void()> clear;                 // File, Bind: left clears
};
struct Section {
	std::string name, part, text;
	std::vector<Row> rows;
};

/* a DualSense control by SDL's gamepad numbering, as Gopher64's profiles keep it */
std::string bindingLabel(const Binding &b)
{
	static const char *const buttons[21] = { "Cross", "Circle", "Square", "Triangle", "Touch pad", "PS", "Options", "L3", "R3", "L1", "R1",
		                                     "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Mute", "", "", "", "", "Touch pad" };
	switch (b.kind) {
	case 1:
		return b.id >= 0 && b.id < 21 && buttons[b.id][0] ? buttons[b.id] : "Button " + std::to_string(b.id);
	case 2:
		switch (b.id) {
		case 0: return b.axis < 0 ? "Left stick \xE2\x86\x90" : "Left stick \xE2\x86\x92";
		case 1: return b.axis < 0 ? "Left stick \xE2\x86\x91" : "Left stick \xE2\x86\x93";
		case 2: return b.axis < 0 ? "Right stick \xE2\x86\x90" : "Right stick \xE2\x86\x92";
		case 3: return b.axis < 0 ? "Right stick \xE2\x86\x91" : "Right stick \xE2\x86\x93";
		case 4: return "L2";
		case 5: return "R2";
		default: return "Axis " + std::to_string(b.id);
		}
	case 3: return "a joystick's input (DirectInput)";
	default: return "\xE2\x80\x94 none";
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

std::string baseName(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

void Frontend::bench(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f)
{
	Ink ink{ dl, u_ };
	TitleSettings *ts = host_.title;
	auto choose = [this](const char *key, const std::string &label, const std::string &help, std::vector<Option> options) {
		Row r;
		r.label = label, r.help = help, r.options = std::move(options);
		r.get = [this, key] { return host_.get ? host_.get(key) : 0; };
		r.set = [this, key](int64_t v) { if (host_.set) host_.set(key, v); };
		return r;
	};
	auto title_row = [ts](const std::string &label, const std::string &help, std::vector<Option> options,
	                      std::function<int64_t(TitleSettings &)> get, std::function<void(TitleSettings &, int64_t)> set) {
		Row r;
		r.label = label, r.help = help, r.options = std::move(options);
		r.get = [ts, get] { return ts ? get(*ts) : 0; };
		r.set = [ts, set](int64_t v) {
			if (ts) {
				set(*ts, v);
				ts->save();
			}
		};
		return r;
	};
	auto info = [](const std::string &label, const std::string &value, const std::string &help) {
		Row r;
		r.kind = Row::Info, r.label = label, r.help = help;
		r.value = [value] { return value; };
		return r;
	};
	auto action = [](const std::string &label, std::function<std::string()> value, const std::string &help, std::function<void()> run) {
		Row r;
		r.kind = Row::Action, r.label = label, r.help = help, r.value = std::move(value), r.run = std::move(run);
		return r;
	};
	const std::vector<std::string> profiles = host_.profiles ? host_.profiles() : std::vector<std::string>{ "default" };
	if (bench_profile_.empty() || std::find(profiles.begin(), profiles.end(), bench_profile_) == profiles.end())
		bench_profile_ = profiles.empty() ? "default" : profiles.front();
	auto bind_row = [this, &pad](const std::string &profile, int input, const std::string &label, const std::string &help) {
		Row r;
		r.kind = Row::Bind, r.label = label, r.help = help;
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
		Section s{ "Video", "rdp", "How finely the picture is drawn and how parallel-rdp makes its shaders (from the next game).", {} };
		s.rows.push_back(choose("video.upscale", "Resolution", "How much finer than the original the picture is drawn: 2\xC3\x97, 4\xC3\x97 or 8\xC3\x97 the N64's pixels.",
		                        { { "1\xC3\x97 original", 1 }, { "2\xC3\x97", 2 }, { "4\xC3\x97", 4 }, { "8\xC3\x97", 8 } }));
		s.rows.push_back(choose("video.ssaa", "Supersampling", "Draws finely and shows the result at the original size: smoother edges, the original's sharpness.",
		                        { { "Off", 0 }, { "On", 1 } }));
		s.rows.push_back(choose("video.vsync", "Vsync", "Each frame waits for the screen's refresh (Gopher64's vsync).", { { "Off", 0 }, { "On", 1 } }));
		s.rows.push_back(title_row("Shader compiles", "Until a state's own shader is made, parallel-rdp draws with a general, slower one: one thread (Gopher64's), four, or each made when first needed.",
		                           { { "1 thread", 1 }, { "4 threads", 4 }, { "At once", 0 } },
		                           [](TitleSettings &t) -> int64_t { return t.shaderCompile == "now" ? 0 : atoi(t.shaderCompile.c_str()); },
		                           [](TitleSettings &t, int64_t v) { t.shaderCompile = v == 0 ? "now" : std::to_string(v); }));
		s.rows.push_back(title_row("Graphics driver", "The driver records and submits on a thread of its own (Crux2's switch).", { { "Same thread", 0 }, { "Own thread", 1 } },
		                           [](TitleSettings &t) -> int64_t { return t.driverThread; }, [](TitleSettings &t, int64_t v) { t.driverThread = v != 0; }));
		s.rows.push_back(title_row("Compute waves", "How wide the GPU runs parallel-rdp's shaders: 64 (RADV's default) or 32, which suits its per-pixel loops on RDNA. GPU timing shows which is faster.",
		                           { { "64 wide", 0 }, { "32 wide", 1 } }, [](TitleSettings &t) -> int64_t { return t.wave32; },
		                           [](TitleSettings &t, int64_t v) { t.wave32 = v != 0; }));
		s.rows.push_back(title_row("8/16-bit maths", "parallel-rdp's 8 and 16-bit arithmetic, which it turns off on RADV (measured on PCs). GPU timing shows which is faster here.",
		                           { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.smallTypes; },
		                           [](TitleSettings &t, int64_t v) { t.smallTypes = v != 0; }));
		s.rows.push_back(title_row("GPU timing", "Writes the GPU's time in each of parallel-rdp's stages to the boot log every 10 s (gpu: lines), to find what a slow game costs.",
		                           { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.gpuTiming; },
		                           [](TitleSettings &t, int64_t v) { t.gpuTiming = v != 0; }));
		sections.push_back(std::move(s));
	}
	/* Screen and sound */
	{
		Section s{ "Screen & sound", "adac", "What is plugged into the AV multi out, how the picture fills the screen, how much sound is kept ready.", {} };
		s.rows.push_back(choose("video.crt", "Tube TV look", "Scanlines and the glow of a CRT.", { { "Off", 0 }, { "On", 1 } }));
		s.rows.push_back(choose("video.widescreen", "Widescreen", "Stretches the picture to 16:9.", { { "4:3", 0 }, { "16:9 stretch", 1 } }));
		s.rows.push_back(choose("video.integer_scaling", "Integer scaling", "The picture grows in whole steps only: every pixel the same size.", { { "Off", 0 }, { "On", 1 } }));
		s.rows.push_back(title_row("Sound buffer", "How much sound is kept queued: more, fewer gaps when the game is slow, and more delay.",
		                           { { "33 ms", 2 }, { "67 ms", 4 }, { "100 ms", 6 }, { "133 ms", 8 } },
		                           [](TitleSettings &t) -> int64_t { return t.audioBuffer; }, [](TitleSettings &t, int64_t v) { t.audioBuffer = (int)v; }));
		sections.push_back(std::move(s));
	}
	/* TV output */
	{
		Section s{ "TV output", "vdac", "The console's output: refresh from the next start, HDR10 from the next game. Untested on a console yet.", {} };
		s.rows.push_back(title_row("Refresh", "59.94 Hz is the N64's; 119.88 Hz and variable refresh need a TV that has them.",
		                           { { "59.94 Hz", 0 }, { "119.88 Hz", 1 }, { "VRR", 2 } },
		                           [](TitleSettings &t) -> int64_t { return t.display == "120" ? 1 : t.display == "vrr" ? 2 : 0; },
		                           [](TitleSettings &t, int64_t v) { t.display = v == 1 ? "120" : v == 2 ? "vrr" : "60"; }));
		s.rows.push_back(title_row("HDR10", "HDR10 when the console's own output is HDR.", { { "SDR", 0 }, { "HDR10", 1 } },
		                           [](TitleSettings &t) -> int64_t { return t.hdr; }, [](TitleSettings &t, int64_t v) { t.hdr = v != 0; }));
		Row white = title_row("HDR white", "The brightness white is shown at (203 nits is HDR's reference).", {}, [](TitleSettings &t) -> int64_t { return t.hdrWhite; },
		                      [](TitleSettings &t, int64_t v) { t.hdrWhite = (int)v; });
		white.kind = Row::Stepper, white.lo = 80, white.hi = 400, white.step = 10, white.unit = " nits";
		s.rows.push_back(white);
		Row peak = title_row("HDR highlights", "The brightness the brightest parts are raised to (0 off).", {}, [](TitleSettings &t) -> int64_t { return t.hdrPeak; },
		                     [](TitleSettings &t, int64_t v) { t.hdrPeak = (int)v; });
		peak.kind = Row::Stepper, peak.lo = 0, peak.hi = 4000, peak.step = 100, peak.unit = " nits";
		s.rows.push_back(peak);
		sections.push_back(std::move(s));
	}
	/* Emulation */
	{
		Section s{ "Emulation", "xtal", "The N64 itself: its clock, its memory, and what the cartridge slot holds.", {} };
		s.rows.push_back(choose("emulation.overclock", "CPU clock", "Overclocks the emulated CPU: fewer slowdowns in heavy games, more work for the console; a few games need the stock clock.",
		                        { { "93.75 MHz stock", 0 }, { "125 MHz", 1 } }));
		s.rows.push_back(choose("emulation.disable_expansion_pak", "Memory", "The 4 MB Expansion Pak. Some games need it, a few misbehave with it.",
		                        { { "Expansion Pak", 0 }, { "Jumper Pak", 1 } }));
		s.rows.push_back(choose("emulation.rewind", "Rewind", "Keeps a snapshot every second to go back to (the hotkey's Rewind).", { { "Off", 0 }, { "On", 1 } }));
		s.rows.push_back(choose("emulation.usb", "USB link", "The development cartridge's USB link (homebrew). Off on the console: it needs network sockets.", { { "Off", 0 }, { "On", 1 } }));
		sections.push_back(std::move(s));
	}
	/* Controllers */
	{
		Section s{ "Controllers", "port1", "Each port: whether a controller is plugged in, which DualSense plays it, its input profile, its Transfer Pak.", {} };
		for (int i = 0; i < 4; i++) {
			const std::string n = std::to_string(i + 1), idx = std::to_string(i);
			static std::string enabled_keys[4], assign_keys[4], profile_keys[4], tpak_keys[4], rom_keys[4], ram_keys[4];
			enabled_keys[i] = "input.controller_enabled." + idx, assign_keys[i] = "input.controller_assignment." + idx;
			profile_keys[i] = "input.input_profile_binding." + idx, tpak_keys[i] = "input.transfer_pak." + idx;
			rom_keys[i] = "input.gb_rom_path." + idx, ram_keys[i] = "input.gb_ram_path." + idx;
			s.rows.push_back(choose(enabled_keys[i].c_str(), "Port " + n, "Whether port " + n + " has a controller.", { { "Empty", 0 }, { "Plugged in", 1 } }));
			Row who;
			who.label = "Port " + n + " \xC2\xB7 controller", who.help = "Which DualSense plays port " + n + " (player numbers as the console signs them in).";
			who.options = { { "None", 0 }, { "DualSense 1", 1 }, { "DualSense 2", 2 }, { "DualSense 3", 3 }, { "DualSense 4", 4 } };
			who.get = [this, i] {
				const std::string v = host_.text ? host_.text(assign_keys[i].c_str()) : "";
				return v.rfind("ps5:pad", 0) == 0 ? (int64_t)atoi(v.c_str() + 7) : (int64_t)0;
			};
			who.set = [this, i](int64_t v) { if (host_.set_text) host_.set_text(assign_keys[i].c_str(), v ? "ps5:pad" + std::to_string(v) : ""); };
			s.rows.push_back(who);
			Row prof;
			prof.label = "Port " + n + " \xC2\xB7 profile", prof.help = "Its input profile: which DualSense control is which N64 button (edited under Profiles).";
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
			s.rows.push_back(choose(tpak_keys[i].c_str(), "Port " + n + " \xC2\xB7 Transfer Pak", "A Transfer Pak with a Game Boy cartridge: its ROM and save below.",
			                        { { "Off", 0 }, { "On", 1 } }));
			for (int which = 0; which < 2; which++) {
				Row file;
				file.kind = Row::File;
				const char *key = which == 0 ? rom_keys[i].c_str() : ram_keys[i].c_str();
				file.label = "Port " + n + (which == 0 ? " \xC2\xB7 Game Boy ROM" : " \xC2\xB7 Game Boy save");
				file.help = which == 0 ? "The Game Boy cartridge in the Transfer Pak (.gb, .gbc) from the console or a USB drive. \xE2\x97\x84 clears."
				                       : "Its save (.sav), or one made beside the ROM. \xE2\x97\x84 clears.";
				file.value = [this, key] {
					const std::string v = host_.text ? host_.text(key) : "";
					return v.empty() ? std::string("\xE2\x80\x94 none") : baseName(v);
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
		s.rows.push_back(choose("input.emulate_vru", "Voice Recognition Unit", "The VRU in port 4 (its games' voice commands).", { { "Off", 0 }, { "In port 4", 1 } }));
		sections.push_back(std::move(s));
	}
	/* Profiles */
	{
		Section s{ "Profiles", "pif", "Input profiles: each N64 button's DualSense control. \xE2\x9C\x95 then press the control; \xE2\x97\x84 clears.", {} };
		Row which;
		which.label = "Profile", which.help = "The profile edited below. Ports choose theirs under Controllers.";
		for (size_t p = 0; p < profiles.size(); p++)
			which.options.push_back({ profiles[p], (int64_t)p });
		which.get = [this, profiles] {
			const auto it = std::find(profiles.begin(), profiles.end(), bench_profile_);
			return (int64_t)(it == profiles.end() ? 0 : it - profiles.begin());
		};
		which.set = [this, profiles](int64_t v) { if (v >= 0 && v < (int64_t)profiles.size()) bench_profile_ = profiles[v]; };
		s.rows.push_back(which);
		s.rows.push_back(action("New profile", [] { return std::string("Gopher64's default bindings"); }, "Makes a profile with Gopher64's default bindings, to change here.",
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
		s.rows.push_back(action("Delete this profile", [this] { return bench_profile_ == "default" ? std::string("default stays") : bench_profile_; },
		                        "Takes this profile away; ports that had it get \"default\".", [this] {
			                        if (bench_profile_ != "default" && host_.profile_delete && host_.profile_delete(bench_profile_))
				                        bench_profile_ = "default";
		                        }));
		for (int input = 0; input < 19; input++)
			s.rows.push_back(bind_row(bench_profile_, input, kN64Inputs[input],
			                          input == 18 ? "Held with another button, it makes that button a hotkey (see Hotkeys)."
			                                      : std::string("The N64's ") + kN64Inputs[input] + ": \xE2\x9C\x95 then press the DualSense control for it."));
		Row dead;
		dead.kind = Row::Stepper, dead.label = "Deadzone", dead.help = "How far a stick moves before the N64 sees it.";
		dead.lo = 0, dead.hi = 50, dead.step = 1, dead.unit = " %";
		dead.get = [this] { return host_.profile_number ? host_.profile_number(bench_profile_, 0, 0, false) : 0; };
		dead.set = [this](int64_t v) { if (host_.profile_number) host_.profile_number(bench_profile_, 0, v, true); };
		s.rows.push_back(dead);
		Row raw;
		raw.label = "Read the pad as", raw.help = "Gopher64's DirectInput mode reads a raw joystick; its bindings are then a joystick's, which only a PC's dialog makes.";
		raw.options = { { "Gamepad", 0 }, { "Joystick (DirectInput)", 1 } };
		raw.get = [this] { return host_.profile_number ? host_.profile_number(bench_profile_, 1, 0, false) : 0; };
		raw.set = [this](int64_t v) { if (host_.profile_number) host_.profile_number(bench_profile_, 1, v, true); };
		s.rows.push_back(raw);
		sections.push_back(std::move(s));
	}
	/* Hotkeys */
	{
		Section s{ "Hotkeys", "cpu", "Gopher64's hotkeys: the Hotkey Activator held, then a button. Each can be another N64 button or off.", {} };
		s.rows.push_back(title_row("Hotkeys", "All of them on or off.", { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.hotkeys; },
		                           [](TitleSettings &t, int64_t v) { t.hotkeys = v != 0; }));
		Row tap = title_row("Quick menu tap", "A touch pad tap opens the quick menu. Kept on while nothing else can leave a game (Hotkeys off or Leave the game off).",
		                    { { "Off", 0 }, { "On", 1 } }, [](TitleSettings &t) -> int64_t { return t.quickmenuTap; },
		                    [](TitleSettings &t, int64_t v) { t.quickmenuTap = v != 0; });
		s.rows.push_back(tap);
		s.rows.push_back(bind_row(port1_profile, 18, "Activator (port 1)", "The control held for a hotkey, in port 1's profile (\"" + port1_profile + "\"): the touch pad by default."));
		for (size_t a = 0; a < 15; a++) {
			Row r;
			r.label = kHotkeyActions[a].label;
			r.help = a < 6 ? "One of Gopher64's own hotkeys." : "On a PC this is a key; here it can be a hotkey.";
			r.options.push_back({ "Off", -1 });
			for (int input = 0; input < 14; input++) {
				const Binding b = host_.binding ? host_.binding(port1_profile, input) : Binding{};
				r.options.push_back({ std::string(kN64Inputs[input]) + (b.kind ? " \xC2\xB7 " + bindingLabel(b) : ""), input });
			}
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
	/* This console */
	{
		Section s{ "This console", "slot", "PSGopher on this console: network shares, the sandbox, the library.", {} };
		char shares[48];
		snprintf(shares, sizeof(shares), "%d", host_.share_count ? host_.share_count() : 0);
		const std::string shares_text = shares;
		s.rows.push_back(action("Network shares", [shares_text] { return shares_text; }, "Add, change or take away SMB and NFS shares.", [this] { if (host_.shares) host_.shares(); }));
		const bool off = host_.elevation_off && host_.elevation_off();
		const std::string elevation = off ? "off from the next start" : host_.elevated && host_.elevated() ? "on: USB, all of /data" : "on (not left: see the log)";
		s.rows.push_back(action("Leave the sandbox", [elevation] { return elevation; }, "Outside the sandbox PSGopher reaches USB drives and all of /data.",
		                        [this, off] { if (host_.set_elevation_off) host_.set_elevation_off(!off); }));
		s.rows.push_back(action("Search for cartridges", [] { return std::string(library().scanning() ? "searching" : "again"); }, "Searches every place again.",
		                        [] { library().rescan(); }));
		s.rows.push_back(action("Close PSGopher", [] { return std::string("to the home screen"); }, "Closes the title.", [this] { if (host_.close) host_.close(); }));
		sections.push_back(std::move(s));
	}
	/* What Gopher64 has on a PC only */
	{
		Section s{ "PC only", "slot", "Gopher64's settings that have no place on the console, and why.", {} };
		s.rows.push_back(info("Fullscreen", "always", "The console's screen is always the whole picture."));
		s.rows.push_back(info("Keyboard bindings", "no keyboard", "Each input's first binding is a keyboard key; the console's controller is the second."));
		s.rows.push_back(info("Theme, ROM folder, recent games", "the library", "Gopher64's own window keeps these; PSGopher's library and board do that here."));
		s.rows.push_back(info("Netplay, RetroAchievements", "not on the console", "Both need network sockets Gopher64 makes through Tokio, which the console refuses."));
		sections.push_back(std::move(s));
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
		if (pad.pressed & PAD_CIRCLE)
			setView(board::View::Home);
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
	         "L1 \xC2\xB7 R1  VIEWS      L2 \xC2\xB7 R2  SECTION      \xE2\x96\xB2 \xE2\x96\xBC  SETTING      \xE2\x97\x84 \xE2\x96\xBA  CHANGE      \xE2\x9C\x95  CHOOSE      \xE2\x97\x8B  HOME",
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
