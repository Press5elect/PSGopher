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
	const board::Look look = g ? board::lookOf(g->name, g->id) : board::Look{};

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
	const board::Look look = board::lookOf(g.name, g.id);
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
	ink.text(f.mono, 16, x + pad_x, cy, withAlpha(kDim, alpha), sub);
	cy += 16 + 16;

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
		{ "WHERE", where(g) },
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
		ink.text(f.mono_bold, 18, cx + 14, cyy + 30, withAlpha(kText, alpha), value);
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
		                  { rack_panel ? "\xE2\x96\xB3 Seat it" : "\xE2\x96\xB3 Bench", false } };
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

void Frontend::home(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f)
{
	Ink ink{ dl, u_ };
	const int n = (int)strip_.size();
	const Game *g = selected();
	if (probes_open_ >= 0 && g) {
		probePicker(dl, pad, f, *g);
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
	if (pad.pressed & PAD_TRIANGLE)
		setView(board::View::Bench);

	/* the line from the strip's cartridge to the slot */
	if (g) {
		const board::Look look = board::lookOf(g->name, g->id);
		const ImVec2 slot = projector_(board::anchor("slot"));
		const ImVec2 from = S(960, 806);
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
	const float pitch = 172, target = 960 - 75 - strip_sel_ * pitch;
	strip_x_ += (target - strip_x_) * std::min(1.0f, 1.0f / 60 * 9);
	if (n == 0) {
		ink.text(f.mono, 18, 64, 900, kDim,
		         library().scanning() ? "Looking for cartridges\xE2\x80\xA6" : "No cartridges yet: ROMs (.z64 .n64 .v64, .zip or .7z of one) in /data, on USB drives or network shares");
	}
	for (int k = std::max(0, strip_sel_ - 7); k < std::min(n, strip_sel_ + 8); k++) {
		const Game &cg = games_[strip_[k]];
		const bool on = k == strip_sel_;
		const float scale = on ? 1.08f : .9f, lift = on ? 0 : 34;
		const float cx = strip_x_ + k * pitch + 75, top = 808 + lift + (1 - scale) * 120;
		const float sw = 150 * scale, sh = 170 * scale, x0 = cx - sw / 2;
		const board::Look look = board::lookOf(cg.name, cg.id);
		if (on)
			ink.glow(x0, top, sw, sh, board::colourOf(look), 22, 10);
		/* the shell (the design's clip-path), its label */
		const float px[10] = { 0, .10f, .90f, 1, 1, .78f, .74f, .26f, .22f, 0 };
		const float py[10] = { .12f, 0, 0, .12f, 1, 1, .93f, .93f, 1, 1 };
		ImVec2 shell[10];
		for (int i = 0; i < 10; i++)
			shell[i] = S(x0 + px[i] * sw, top + py[i] * sh);
		dl->AddConcavePolyFilled(shell, 10, IM_COL32(0x22, 0x2b, 0x33, 255));
		dl->AddRectFilledMultiColor(S(x0, top + sh * .12f), S(x0 + sw, top + sh * .93f), IM_COL32(0x27, 0x31, 0x3a, 0),
		                            IM_COL32(0x27, 0x31, 0x3a, 0), IM_COL32(0x12, 0x18, 0x1d, 200), IM_COL32(0x12, 0x18, 0x1d, 200));
		const float lw = 118 * scale, lx = cx - lw / 2, ly = top + 16 * scale, s = lw / 236 * u_;
		board::drawLabel(dl, look, f.mono_heavy, [lx, ly, s, this](ImVec2 v) { return ImVec2(lx * u_ + v.x * s, ly * u_ + v.y * s); });
		std::string name = titleOf(cg);
		while (name.size() > 4 && ink.width(f.mono, 16, name) > 168)
			name = name.substr(0, name.size() - 4) + "\xE2\x80\xA6";
		const float nw = ink.width(f.mono, 16, name);
		ink.text(f.mono, 16, cx - nw / 2, top + sh + 10, on ? kText : kDim, name);
	}
	ink.text(f.mono, 14, 64, 1046, kDim, "L1 \xC2\xB7 R1  VIEWS      \xE2\x97\x84 \xE2\x96\xBA  CARTRIDGE      \xE2\x9C\x95  POWER ON      \xE2\x96\xA1  PROBES      \xE2\x96\xB3  BENCH", .08f);
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
	const board::Look look = board::lookOf(g.name, g.id);
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
	const board::Look sel_look = g ? board::lookOf(g->name, g->id) : board::Look{};
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

/* a callout's row: options to choose one of, or a value moved with left/right */
struct Option {
	const char *label;
	int64_t value;
};
struct Row {
	std::vector<Option> options;
	std::function<int64_t()> get;
	std::function<void(int64_t)> set;
	bool stepper = false; // left/right move the value by `step` within [lo, hi]
	int64_t lo = 0, hi = 0, step = 1;
	const char *unit = "";
};
struct Callout {
	const char *title, *text;
	const char *part; // board::anchor's name
	float x, y, w;
	std::vector<Row> rows;
};

} // namespace

void Frontend::bench(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f)
{
	Ink ink{ dl, u_ };
	auto setting = [this](const char *key) {
		return Row{ {}, [this, key] { return host_.get ? host_.get(key) : 0; }, [this, key](int64_t v) { if (host_.set) host_.set(key, v); } };
	};
	auto choose = [&](const char *key, std::vector<Option> options) {
		Row r = setting(key);
		r.options = std::move(options);
		return r;
	};
	TitleSettings *ts = host_.title;
	auto title_row = [ts](std::vector<Option> options, std::function<int64_t(TitleSettings &)> get, std::function<void(TitleSettings &, int64_t)> set) {
		Row r;
		r.options = std::move(options);
		r.get = [ts, get] { return ts ? get(*ts) : 0; };
		r.set = [ts, set](int64_t v) {
			if (ts) {
				set(*ts, v);
				ts->save();
			}
		};
		return r;
	};
	std::vector<Callout> callouts;
	callouts.push_back({ "Crystal \xC2\xB7 the clock", "Swap the oscillator to overclock the emulated CPU. Fewer slowdowns in heavy games; a few games need the stock part.",
		                 "xtal", 440, 140, 300, { choose("emulation.overclock", { { "93.75 MHz stock", 0 }, { "125 MHz", 1 } }) } });
	callouts.push_back({ "Video stage \xC2\xB7 sharpness", "How much finer than the original the picture is drawn before it reaches your TV.", "rdp", 1000, 140, 330,
		                 { choose("video.upscale", { { "1\xC3\x97 original", 1 }, { "2\xC3\x97", 2 }, { "4\xC3\x97", 4 }, { "8\xC3\x97", 8 } }),
		                   choose("video.ssaa", { { "Sharp", 0 }, { "Supersampled", 1 } }) } });
	callouts.push_back({ "AV out \xC2\xB7 the screen", "What is plugged into the AV multi out, and how the picture fills it.", "vdac", 1413, 300, 330,
		                 { choose("video.crt", { { "Flat panel", 0 }, { "Tube TV look", 1 } }),
		                   choose("video.widescreen", { { "4:3", 0 }, { "16:9 stretch", 1 } }),
		                   choose("video.integer_scaling", { { "Any size", 0 }, { "Whole steps", 1 } }) } });
	callouts.push_back({ "TV link \xC2\xB7 the console's output", "The refresh (from the next start) and HDR10 (from the next game). Untested on a console yet.", "vdac", 1413, 560, 330,
		                 { title_row({ { "59.94 Hz", 0 }, { "119.88 Hz", 1 }, { "VRR", 2 } },
		                             [](TitleSettings &t) -> int64_t { return t.display == "120" ? 1 : t.display == "vrr" ? 2 : 0; },
		                             [](TitleSettings &t, int64_t v) { t.display = v == 1 ? "120" : v == 2 ? "vrr" : "60"; }),
		                   title_row({ { "SDR", 0 }, { "HDR10", 1 } }, [](TitleSettings &t) -> int64_t { return t.hdr; },
		                             [](TitleSettings &t, int64_t v) { t.hdr = v != 0; }) } });
	{
		Row white = title_row({}, [](TitleSettings &t) -> int64_t { return t.hdrWhite; }, [](TitleSettings &t, int64_t v) { t.hdrWhite = (int)v; });
		white.stepper = true, white.lo = 80, white.hi = 400, white.step = 10, white.unit = " nits white";
		Row peak = title_row({}, [](TitleSettings &t) -> int64_t { return t.hdrPeak; }, [](TitleSettings &t, int64_t v) { t.hdrPeak = (int)v; });
		peak.stepper = true, peak.lo = 0, peak.hi = 4000, peak.step = 100, peak.unit = " nits highlights";
		callouts.back().rows.push_back(white);
		callouts.back().rows.push_back(peak);
	}
	callouts.push_back({ "Expansion slot \xC2\xB7 memory", "The 4 MB Expansion Pak. Some games need it, a few misbehave with it.", "exp", 1413, 868, 330,
		                 { choose("emulation.disable_expansion_pak", { { "Jumper Pak", 1 }, { "Expansion Pak", 0 } }) } });
	{
		Callout ports{ "Ports \xC2\xB7 your DualSense", "Which ports have a controller: player n's DualSense plays port n. Port 4 can hold the Voice Recognition Unit.", "port1", 60, 610, 330, {} };
		for (int i = 0; i < 4; i++) {
			static const char *const keys[4] = { "input.controller_enabled.0", "input.controller_enabled.1", "input.controller_enabled.2",
				                                 "input.controller_enabled.3" };
			static const char *const names[4] = { "Port 1", "Port 2", "Port 3", "Port 4" };
			Row r = choose(keys[i], { { "Empty", 0 }, { names[i], 1 } });
			ports.rows.push_back(r);
		}
		ports.rows.push_back(choose("input.emulate_vru", { { "No VRU", 0 }, { "VRU in port 4", 1 } }));
		callouts.push_back(ports);
	}
	callouts.push_back({ "Cartridge slot \xC2\xB7 extras", "Rewind keeps the last moments to go back to; the USB link is the development cartridge's (homebrew).", "slot", 60, 300, 330,
		                 { choose("emulation.rewind", { { "No rewind", 0 }, { "Rewind", 1 } }), choose("emulation.usb", { { "No USB link", 0 }, { "USB link", 1 } }) } });
	/* this console: actions, not parts */
	struct Action {
		std::string label, hint;
		std::function<void()> run;
	};
	std::vector<Action> actions;
	char shares[48];
	snprintf(shares, sizeof(shares), "%d", host_.share_count ? host_.share_count() : 0);
	actions.push_back({ "Network shares", shares, [this] { if (host_.shares) host_.shares(); } });
	const bool off = host_.elevation_off && host_.elevation_off();
	actions.push_back({ "Leave the sandbox", off ? "off from the next start" : host_.elevated && host_.elevated() ? "on: USB, all of /data" : "on (not left: see the log)",
		                [this, off] { if (host_.set_elevation_off) host_.set_elevation_off(!off); } });
	actions.push_back({ "Search for cartridges", library().scanning() ? "searching" : "again", [] { library().rescan(); } });
	actions.push_back({ "Close PSGopher", "to the home screen", [this] { if (host_.close) host_.close(); } });

	/* the items: each callout's rows, then the actions */
	struct Item {
		int callout = -1, row = -1, action = -1;
	};
	std::vector<Item> items;
	for (size_t c = 0; c < callouts.size(); c++)
		for (size_t r = 0; r < callouts[c].rows.size(); r++)
			items.push_back({ (int)c, (int)r, -1 });
	for (size_t a = 0; a < actions.size(); a++)
		items.push_back({ -1, -1, (int)a });
	const int n = (int)items.size();
	bench_sel_ = std::clamp(bench_sel_, 0, n - 1);
	if (pad.repeat(PAD_DOWN))
		bench_sel_ = (bench_sel_ + 1) % n;
	if (pad.repeat(PAD_UP))
		bench_sel_ = (bench_sel_ + n - 1) % n;
	if (pad.pressed & PAD_CIRCLE)
		setView(board::View::Home);
	const Item &it = items[bench_sel_];
	if (it.callout >= 0) {
		Row &r = callouts[it.callout].rows[it.row];
		const int64_t v = r.get ? r.get() : 0;
		if (r.stepper) {
			if (pad.repeat(PAD_RIGHT))
				r.set(std::min(r.hi, v + r.step));
			if (pad.repeat(PAD_LEFT))
				r.set(std::max(r.lo, v - r.step));
		} else {
			int at = 0;
			for (size_t o = 0; o < r.options.size(); o++)
				if (r.options[o].value == v)
					at = (int)o;
			const int m = (int)r.options.size();
			if (pad.repeat(PAD_RIGHT) || (pad.pressed & PAD_CROSS))
				r.set(r.options[(at + 1) % m].value);
			else if (pad.repeat(PAD_LEFT))
				r.set(r.options[(at + m - 1) % m].value);
		}
	} else if (it.action >= 0 && (pad.pressed & PAD_CROSS))
		actions[it.action].run();

	/* the callouts, each with its line to its part */
	for (size_t c = 0; c < callouts.size(); c++) {
		const Callout &co = callouts[c];
		const ImVec2 part = projector_(board::anchor(co.part));
		const auto lines = wrap(ink, f.mono, 13, co.text, co.w - 36, 4);
		float h = 16 + 26 + lines.size() * 19 + 10;
		for (const Row &r : co.rows)
			h += r.stepper ? 38 : 38;
		h += 8;
		const float cx = co.x, cy = co.y;
		/* the line: from the card's side nearest the part */
		const bool right_side = co.x > 1200;
		const ImVec2 from = right_side ? S(cx, cy + h / 2) : S(cx + co.w, cy + h / 2);
		dl->AddLine(from, part, IM_COL32(255, 182, 72, 230), L(2));
		dl->AddCircleFilled(part, L(7), kAmber);
		bool any = false;
		for (size_t r = 0; r < co.rows.size(); r++)
			any |= it.callout == (int)c && it.row == (int)r;
		ink.rect(cx, cy, co.w, h, kPanel, 14);
		ink.frame(cx, cy, co.w, h, any ? kAmber : kLine, 14, any ? 2 : 1);
		ink.text(f.display, 22, cx + 18, cy + 14, kText, co.title, -.01f);
		float y = cy + 16 + 28;
		for (const std::string &l : lines) {
			ink.text(f.mono, 13, cx + 18, y, kDim, l);
			y += 19;
		}
		y += 8;
		for (size_t r = 0; r < co.rows.size(); r++) {
			const Row &row = co.rows[r];
			const bool here = it.callout == (int)c && it.row == (int)r;
			const int64_t v = row.get ? row.get() : 0;
			float x = cx + 18;
			if (here)
				ink.rect(cx + 6, y - 4, co.w - 12, 36, IM_COL32(255, 182, 72, 26), 8);
			if (row.stepper) {
				char t[64];
				snprintf(t, sizeof(t), "\xE2\x97\x84 %lld%s \xE2\x96\xBA", (long long)v, row.unit);
				const float tw = ink.width(f.mono_bold, 14, t) + 20;
				ink.rect(x, y, tw, 28, here ? kAmber : IM_COL32(12, 26, 30, 255), 6);
				ink.text(f.mono_bold, 14, x + 10, y + 6, here ? kInk : kText, t);
			} else
				for (const Option &o : row.options) {
					const bool chosen = o.value == v;
					const float tw = ink.width(f.mono_bold, 14, o.label) + 20;
					if (chosen)
						ink.rect(x, y, tw, 28, kAmber, 6);
					else
						ink.frame(x, y, tw, 28, kLine, 6, 1);
					ink.text(f.mono_bold, 14, x + 10, y + 6, chosen ? kInk : (here ? kText : kDim), o.label);
					x += tw + 6;
				}
			y += 38;
		}
	}
	/* this console */
	{
		const float cx = 760, cy = 868, w = 520, rh = 34;
		const float h = 16 + 30 + actions.size() * (rh + 4) + 10;
		bool here_any = false;
		for (size_t a = 0; a < actions.size(); a++)
			here_any |= it.action == (int)a;
		ink.rect(cx, cy, w, h, kPanel, 14);
		ink.frame(cx, cy, w, h, here_any ? kAmber : kLine, 14, here_any ? 2 : 1);
		ink.text(f.display, 22, cx + 18, cy + 14, kText, "This console");
		float y = cy + 16 + 30;
		for (size_t a = 0; a < actions.size(); a++) {
			const bool here = it.action == (int)a;
			ink.rect(cx + 12, y, w - 24, rh, here ? kText : IM_COL32(16, 34, 38, 255), 8);
			ink.text(f.mono_bold, 15, cx + 26, y + 9, here ? kInk : kText, actions[a].label);
			ink.text(f.mono, 13, cx + w - 26 - ink.width(f.mono, 13, actions[a].hint), y + 10, here ? kInk : kDim, actions[a].hint);
			y += rh + 4;
		}
	}
	ink.text(f.mono, 14, 64, 1046, kDim,
	         "L1 \xC2\xB7 R1  VIEWS      \xE2\x96\xB2 \xE2\x96\xBC  PART      \xE2\x97\x84 \xE2\x96\xBA \xE2\x9C\x95  CHANGE      \xE2\x97\x8B  HOME", .08f);
}

} // namespace psgopher
