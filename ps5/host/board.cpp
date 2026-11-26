/*
 * PSGopher - the Live Board's mainboard on the console (board.h).
 *
 * Geometry, colours and timings are the design's (ps5/art/live-board.html: chips, slot, buses,
 * drawDynamic, makeLabel, tick); its CSS transform is computed here per vertex.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "board.h"

#include "imgui_internal.h" /* ImDrawListSharedData: the white pixel's UV */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace psgopher::board {

namespace {

constexpr float kPi = 3.14159265358979f, kTau = 2 * kPi;
constexpr float kPerspective = 2600.0f;
/* the transform's origin: the centre of .wrap3d (left -300, top -190, 2000x1250) in the stage */
constexpr float kOriginX = -300 + kBoardW / 2, kOriginY = -190 + kBoardH / 2;

/* the design's parts (board pixels) */
struct Box {
	float x, y, w, h;
};
const Box kCpu{ 502, 520, 228, 234 }, kRcp{ 833, 437, 238, 259 }, kRamA{ 866, 725, 205, 84 };
const Box kVdac{ 1160, 440, 110, 120 }, kAdac{ 1286, 545, 94, 108 }, kPif{ 1340, 789, 78, 142 };
constexpr float kPakFit = .62f; /* the seated pak's width as a part of the slot's pin row (630 wide, centred on x 971) */
const Box kSlot{ 544, 178, 858, 125 }, kXtal1{ 1207, 712, 78, 48 }, kExp{ 817, 969, 312, 63 };
const float kPortX[4] = { 340, 564, 1149, 1383 };
const Box kPort(int i) { return Box{ kPortX[i], 1082, 214, 158 }; }

/* the design's colours */
const ImU32 kData = IM_COL32(0x4f, 0xf0, 0xe0, 255), kKey = IM_COL32(0xb9, 0xff, 0x5e, 255), kMem = IM_COL32(0x7f, 0xa8, 0xff, 255);
const ImU32 kVideo = IM_COL32(0xff, 0x5f, 0xd2, 255), kAudio = IM_COL32(0xff, 0xb6, 0x48, 255), kClock = IM_COL32(255, 255, 255, 255);

ImU32 alpha(ImU32 c, float a)
{
	const int base = (int)((c >> IM_COL32_A_SHIFT) & 0xff);
	return (c & ~IM_COL32_A_MASK) | ((ImU32)std::clamp((int)(base * a), 0, 255) << IM_COL32_A_SHIFT);
}

/* ---- the buses (the design's bus(): n parallel lines along a path, gap apart) ---- */
struct Bus {
	const char *name;
	ImU32 colour;
	std::vector<std::vector<ImVec2>> lines;
	std::vector<float> lengths;
	float seg = 34, period = 190, speed = 260, delay = 0;
};

std::vector<ImVec2> offsetLine(const std::vector<ImVec2> &pts, float d)
{
	std::vector<ImVec2> out;
	const size_t n = pts.size();
	auto normal = [](ImVec2 p, ImVec2 q) {
		const float dx = q.x - p.x, dy = q.y - p.y;
		float l = std::hypot(dx, dy);
		if (l == 0)
			l = 1;
		return ImVec2(-dy / l, dx / l);
	};
	for (size_t i = 0; i < n; i++) {
		const ImVec2 a = pts[i ? i - 1 : 0], b = pts[i], c = pts[std::min(n - 1, i + 1)];
		const ImVec2 n1 = i > 0 ? normal(a, b) : normal(b, c), n2 = i + 1 < n ? normal(b, c) : n1;
		float mx = n1.x + n2.x, my = n1.y + n2.y;
		float ml = std::hypot(mx, my);
		if (ml == 0)
			ml = 1;
		mx /= ml;
		my /= ml;
		const float k = d / std::max(0.4f, mx * n1.x + my * n1.y);
		out.push_back(ImVec2(b.x + mx * k, b.y + my * k));
	}
	return out;
}

Bus makeBus(const char *name, ImU32 colour, int n, float gap, std::vector<ImVec2> pts, float delay, float speed = 260,
            float period = 190, float seg = 34)
{
	Bus b;
	b.name = name;
	b.colour = colour;
	b.delay = delay;
	b.speed = speed;
	b.period = period;
	b.seg = seg;
	for (int i = 0; i < n; i++) {
		auto line = offsetLine(pts, (i - (n - 1) / 2.0f) * gap);
		float len = 0;
		for (size_t k = 1; k < line.size(); k++)
			len += std::hypot(line[k].x - line[k - 1].x, line[k].y - line[k - 1].y);
		b.lines.push_back(line);
		b.lengths.push_back(len);
	}
	return b;
}

const std::vector<Bus> &buses()
{
	static std::vector<Bus> all = [] {
		std::vector<Bus> v;
		/* the signals, routed as the console wires them (the design's comment) */
		v.push_back(makeBus("cic", kKey, 2, 9, { { 1395, 303 }, { 1395, 420 }, { 1446, 470 }, { 1446, 760 }, { 1395, 789 } }, .05f, 120, 90, 18));
		v.push_back(makeBus("pi", kData, 14, 7, { { 952, 303 }, { 952, 437 } }, .2f));
		v.push_back(makeBus("pifcpu", kKey, 4, 8, { { 1360, 789 }, { 1360, 700 }, { 1150, 700 }, { 1150, 680 }, { 1071, 680 } }, .35f, 160));
		v.push_back(makeBus("xtal", kClock, 1, 0, { { 1207, 727 }, { 1112, 727 }, { 1112, 660 }, { 1071, 660 } }, .3f, 420, 46, 14));
		v.push_back(makeBus("cpurcp", kData, 14, 7, { { 730, 600 }, { 833, 600 } }, .55f, 340, 130));
		v.push_back(makeBus("ram1", kMem, 8, 7, { { 1010, 696 }, { 1010, 725 } }, .7f, 300, 60, 16));
		v.push_back(makeBus("ram2", kMem, 8, 7, { { 1080, 809 }, { 1092, 809 }, { 1092, 878 }, { 1071, 878 } }, .74f, 300));
		v.push_back(makeBus("exp", kMem, 8, 7, { { 1092, 878 }, { 1092, 950 }, { 1060, 969 } }, .8f, 300, 80, 20));
		v.push_back(makeBus("video", kVideo, 8, 7, { { 1071, 490 }, { 1160, 490 } }, .9f, 380));
		v.push_back(makeBus("vout", kVideo, 3, 10, { { 1215, 440 }, { 1215, 360 }, { 1470, 360 }, { 1470, 190 } }, .95f, 380, 70, 26));
		v.push_back(makeBus("audio", kAudio, 4, 8, { { 1071, 622 }, { 1200, 622 }, { 1224, 598 }, { 1286, 598 } }, .95f, 200));
		v.push_back(makeBus("aout", kAudio, 2, 10, { { 1380, 598 }, { 1556, 598 }, { 1556, 190 } }, 1.0f, 200, 70, 22));
		static const char *const names[4] = { "in0", "in1", "in2", "in3" };
		for (int i = 0; i < 4; i++) {
			const float cx = kPortX[i] + 107, y = 1066 - i * 8.0f, px = 1352 + i * 16.0f;
			v.push_back(makeBus(names[i], kKey, 3, 6, { { cx, 1082 }, { cx, y }, { px, y }, { px, 931 } }, 1 + i * .05f, 150, 240, 22));
		}
		return v;
	}();
	return all;
}

/* a polyline's dashes: on for seg, off for the rest of period, moved by offset along it */
void dashes(ImDrawList *dl, const std::vector<ImVec2> &line, float seg, float period, float offset, ImU32 col, float width)
{
	float at = 0; // distance along the line where this segment starts
	for (size_t k = 1; k < line.size(); k++) {
		const ImVec2 a = line[k - 1], b = line[k];
		const float len = std::hypot(b.x - a.x, b.y - a.y);
		if (len <= 0)
			continue;
		/* the dash pattern's phase at a: dashes start at offset + j*period */
		float phase = std::fmod(at - offset, period);
		if (phase < 0)
			phase += period;
		float s = -phase; // a dash's start relative to a
		for (; s < len; s += period) {
			const float d0 = std::max(0.0f, s), d1 = std::min(len, s + seg);
			if (d1 <= d0)
				continue;
			const ImVec2 p0(a.x + (b.x - a.x) * d0 / len, a.y + (b.y - a.y) * d0 / len);
			const ImVec2 p1(a.x + (b.x - a.x) * d1 / len, a.y + (b.y - a.y) * d1 / len);
			dl->AddLine(p0, p1, col, width);
		}
		at += len;
	}
}

void roundRect(ImDrawList *dl, Box b, ImU32 col, float rounding, float width)
{
	dl->AddRect(ImVec2(b.x, b.y), ImVec2(b.x + b.w, b.y + b.h), col, rounding, width);
}

/* text in board pixels: the font's size in board pixels, left/centre */
void silk(ImDrawList *dl, ImFont *font, const char *text, float x, float y_baseline, float size, bool centre, float alpha_value)
{
	if (!font)
		return;
	const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0, text);
	const float x0 = centre ? x - sz.x / 2 : x;
	dl->AddText(font, size, ImVec2(x0, y_baseline - size * 0.8f), IM_COL32(214, 236, 232, (int)(255 * alpha_value)), text);
}

/* the design's rng (mulberry32) */
struct Rng {
	uint32_t s;
	float operator()()
	{
		s += 0x6D2B79F5u;
		uint32_t t = s;
		t = (t ^ (t >> 15)) * (1u | t);
		t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
		return (float)((t ^ (t >> 14)) >> 0) / 4294967296.0f;
	}
};

/* ---- polygons clipped to a rectangle, for the labels ---- */
using Poly = std::vector<ImVec2>;
Poly clip(const Poly &in, float x0, float y0, float x1, float y1)
{
	Poly out = in;
	auto pass = [&](auto inside, auto cross) {
		Poly src = out;
		out.clear();
		for (size_t i = 0; i < src.size(); i++) {
			const ImVec2 a = src[i], b = src[(i + 1) % src.size()];
			const bool ia = inside(a), ib = inside(b);
			if (ia)
				out.push_back(a);
			if (ia != ib)
				out.push_back(cross(a, b));
		}
	};
	auto lerp = [](ImVec2 a, ImVec2 b, float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); };
	pass([&](ImVec2 p) { return p.x >= x0; }, [&](ImVec2 a, ImVec2 b) { return lerp(a, b, (x0 - a.x) / (b.x - a.x)); });
	if (out.size() < 3)
		return {};
	pass([&](ImVec2 p) { return p.x <= x1; }, [&](ImVec2 a, ImVec2 b) { return lerp(a, b, (x1 - a.x) / (b.x - a.x)); });
	if (out.size() < 3)
		return {};
	pass([&](ImVec2 p) { return p.y >= y0; }, [&](ImVec2 a, ImVec2 b) { return lerp(a, b, (y0 - a.y) / (b.y - a.y)); });
	if (out.size() < 3)
		return {};
	pass([&](ImVec2 p) { return p.y <= y1; }, [&](ImVec2 a, ImVec2 b) { return lerp(a, b, (y1 - a.y) / (b.y - a.y)); });
	return out.size() < 3 ? Poly{} : out;
}

struct LabelPainter {
	ImDrawList *dl;
	const Transform &to;
	float x0, y0, x1, y1; // the label's visible part, label pixels
	void fill(const Poly &p, ImU32 col)
	{
		Poly c = clip(p, x0, y0, x1, y1);
		if (c.size() < 3)
			return;
		for (ImVec2 &v : c)
			v = to(v);
		dl->AddConvexPolyFilled(c.data(), (int)c.size(), col);
	}
	/* a thick segment as a quad */
	void stroke(ImVec2 a, ImVec2 b, float width, ImU32 col)
	{
		const float dx = b.x - a.x, dy = b.y - a.y, l = std::hypot(dx, dy);
		if (l <= 0)
			return;
		const float nx = -dy / l * width / 2, ny = dx / l * width / 2;
		/* round caps' length, as the design's lineCap */
		const float ex = dx / l * width / 2, ey = dy / l * width / 2;
		fill({ ImVec2(a.x + nx - ex, a.y + ny - ey), ImVec2(b.x + nx + ex, b.y + ny + ey), ImVec2(b.x - nx + ex, b.y - ny + ey),
		       ImVec2(a.x - nx - ex, a.y - ny - ey) },
		     col);
	}
	void circle(ImVec2 c, float r, ImU32 col)
	{
		Poly p;
		const int n = std::max(10, (int)(r * 2.5f));
		for (int i = 0; i < n; i++)
			p.push_back(ImVec2(c.x + std::cos(kTau * i / n) * r, c.y + std::sin(kTau * i / n) * r));
		fill(p, col);
	}
	/* a quad with a colour at each corner (the background's gradient) */
	void gradient(ImU32 c00, ImU32 c10, ImU32 c11, ImU32 c01)
	{
		const ImVec2 uv = dl->_Data->TexUvWhitePixel;
		dl->PrimReserve(6, 4);
		const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
		dl->PrimWriteVtx(to(ImVec2(x0, y0)), uv, c00);
		dl->PrimWriteVtx(to(ImVec2(x1, y0)), uv, c10);
		dl->PrimWriteVtx(to(ImVec2(x1, y1)), uv, c11);
		dl->PrimWriteVtx(to(ImVec2(x0, y1)), uv, c01);
		dl->PrimWriteIdx(base);
		dl->PrimWriteIdx(base + 1);
		dl->PrimWriteIdx(base + 2);
		dl->PrimWriteIdx(base);
		dl->PrimWriteIdx(base + 2);
		dl->PrimWriteIdx(base + 3);
	}
};

/* the vertices from `start` on, through `to` */
void transformFrom(ImDrawList *dl, int start, const Transform &to)
{
	for (int i = start; i < dl->VtxBuffer.Size; i++)
		dl->VtxBuffer[i].pos = to(dl->VtxBuffer[i].pos);
}

ImU32 lerpColour(ImU32 a, ImU32 b, float t)
{
	auto ch = [&](int s) {
		const float x = (float)((a >> s) & 0xff), y = (float)((b >> s) & 0xff);
		return (ImU32)std::clamp((int)(x + (y - x) * t + 0.5f), 0, 255) << s;
	};
	return ch(0) | ch(8) | ch(16) | ch(24);
}

} // namespace

void drawLabelPart(ImDrawList *dl, const Look &look, ImFont *font, const Transform &to, float visible_height, float crop_x)
{
	const float cx0 = std::clamp(crop_x, 0.0f, 100.0f), cx1 = 236 - cx0; /* the label's visible columns */
	if (look.cover != ImTextureID_Invalid && look.cover_aspect > 0) {
		/* the box art, cropped to fill the label (centred), its top part when less is visible; in a
		 * grid of quads, so the slot's projection bends it as it bends the board */
		const float vis = std::min(208.0f, visible_height), want = 236.0f / 208.0f;
		float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
		if (look.cover_aspect > want) {
			const float keep = want / look.cover_aspect;
			u0 = (1 - keep) / 2;
			u1 = u0 + keep;
		} else {
			const float keep = look.cover_aspect / want;
			v0 = (1 - keep) / 2;
			v1 = v0 + keep;
		}
		v1 = v0 + (v1 - v0) * vis / 208.0f;
		constexpr int kCells = 6;
		for (int j = 0; j < kCells; j++)
			for (int i = 0; i < kCells; i++) {
				const float a = (float)i / kCells, b = (float)(i + 1) / kCells, c = (float)j / kCells, d = (float)(j + 1) / kCells;
				auto at = [&](float x, float y) { return to(ImVec2(cx0 + x * (cx1 - cx0), y * vis)); };
				auto uv = [&](float x, float y) { return ImVec2(u0 + (u1 - u0) * (cx0 + x * (cx1 - cx0)) / 236, v0 + (v1 - v0) * y); };
				dl->AddImageQuad(ImTextureRef(look.cover), at(a, c), at(b, c), at(b, d), at(a, d), uv(a, c), uv(b, c), uv(b, d), uv(a, d));
			}
		/* its edge, as the label's */
		const ImVec2 edge[4] = { to(ImVec2(cx0, 0)), to(ImVec2(cx1, 0)), to(ImVec2(cx1, vis)), to(ImVec2(cx0, vis)) };
		dl->AddPolyline(edge, 4, IM_COL32(0, 0, 0, 90), 1.0f, ImDrawFlags_Closed);
		return;
	}
	Rng r{ look.seed };
	const float h2 = std::fmod(look.hue + 40 + r() * 80, 360.0f);
	LabelPainter p{ dl, to, cx0, 0, cx1, std::min(208.0f, visible_height) };
	/* the background: from hsl(hue 70% 16%) at the top left to hsl(h2 75% 30%) at the bottom right */
	const ImU32 g0 = hsl(look.hue, .70f, .16f), g1 = hsl(h2, .75f, .30f);
	auto along = [&](float x, float y) { return std::clamp((x * 236 + y * 208) / (236 * 236 + 208 * 208.0f), 0.0f, 1.0f); };
	p.gradient(lerpColour(g0, g1, along(0, 0)), lerpColour(g0, g1, along(236, 0)), lerpColour(g0, g1, along(236, p.y1)),
	           lerpColour(g0, g1, along(0, p.y1)));
	const int kind = (int)(r() * 4);
	if (kind == 0)
		for (int i = 9; i > 0; i--) {
			const ImU32 c = hsl(i % 2 ? look.hue : h2, .95f, (30 + i * 5) / 100.0f);
			const ImVec2 centre(60 + r() * 20, 150);
			const float a0 = -1.9f, a1 = -0.2f + r() * 0.5f, radius = i * 22.0f;
			const int n = 18;
			for (int k = 0; k < n; k++) {
				const float t0 = a0 + (a1 - a0) * k / n, t1 = a0 + (a1 - a0) * (k + 1) / n;
				p.stroke(ImVec2(centre.x + std::cos(t0) * radius, centre.y + std::sin(t0) * radius),
				         ImVec2(centre.x + std::cos(t1) * radius, centre.y + std::sin(t1) * radius), 7, c);
			}
		}
	if (kind == 1)
		for (int i = 0; i < 16; i++) {
			const ImU32 c = hsl(i % 3 ? look.hue : h2, .95f, (45 + (i % 4) * 8) / 100.0f);
			const float x = -40 + i * 22.0f;
			p.stroke(ImVec2(x, 230), ImVec2(60 + i * 22.0f + r() * 30, -20), 4.0f + (i % 3) * 4, c);
		}
	if (kind == 2)
		for (int i = 0; i < 9; i++)
			for (int j = 0; j < 8; j++) {
				const float s = 3 + 9 * std::fabs(std::sin(i * .7f + j * .5f + look.hue));
				p.circle(ImVec2(22 + i * 24.0f, 22 + j * 24.0f), s, hsl((i + j) % 2 ? look.hue : h2, .95f, .62f));
			}
	if (kind == 3)
		for (int i = 0; i < 7; i++) {
			const ImU32 c = hsl(i % 2 ? look.hue : h2, .92f, (38 + i * 6) / 100.0f, .85f);
			const float a = r() * kTau;
			p.fill({ ImVec2(118, 110), ImVec2(118 + std::cos(a) * 240, 110 + std::sin(a) * 240),
			         ImVec2(118 + std::cos(a + .5f) * 240, 110 + std::sin(a + .5f) * 240) },
			       c);
		}
	if (p.y1 >= 208) {
		/* the band with the code and the game's colour */
		p.fill({ ImVec2(0, 168), ImVec2(236, 168), ImVec2(236, 208), ImVec2(0, 208) }, IM_COL32(3, 10, 13, 209));
		if (font) {
			const int start = dl->VtxBuffer.Size;
			dl->AddText(font, 22, ImVec2(std::max(14.0f, cx0 + 4), 196 - 22 * 0.8f), IM_COL32(0xe6, 0xf4, 0xf2, 255), look.code.c_str());
			transformFrom(dl, start, to);
		}
		p.fill({ ImVec2(196, 180), ImVec2(222, 180), ImVec2(222, 194), ImVec2(196, 194) }, colourOf(look));
	}
}

/* An N64 cartridge seen from the front, as the owner's reference photo shows one: a grey shell wider
 * than tall with an arched top, a raised centre panel set off by grooves, steps at the bottom of the
 * side panels, and the label recessed into the centre panel (the box art, or the drawn label). The home
 * strip and the board's slot both draw it here. */
void drawCartridge(ImDrawList *dl, const Look &look, ImFont *font, const Transform &to, float sw, float sh, float unit, bool on,
                   float opacity, const FlagHook &flag)
{
	auto P = [&](float x, float y) { return to(ImVec2(x * sw, y * sh)); };
	auto F = [&](float x, float y) { return to(ImVec2(x, y)); };
	auto A = [&](ImU32 c) {
		const int base = (int)((c >> IM_COL32_A_SHIFT) & 0xff);
		return (c & ~IM_COL32_A_MASK) | ((ImU32)std::clamp((int)(base * opacity), 0, 255) << IM_COL32_A_SHIFT);
	};
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
	const ImU32 grey = A(IM_COL32(0x8e, 0x90, 0x95, 255)), light = A(IM_COL32(0xa2, 0xa4, 0xa9, 255)), groove = A(IM_COL32(0x6c, 0x6e, 0x73, 255)),
	            shadow = A(IM_COL32(0x5c, 0x5e, 0x63, 255));
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
	dl->AddRectFilledMultiColor(P(0, .45f), P(1, 1), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), A(IM_COL32(0, 0, 0, 46)), A(IM_COL32(0, 0, 0, 46)));
	/* the grooves either side of the centre panel */
	for (float x : { .19f, .81f })
		dl->AddLine(P(x, arc(x) + .01f), P(x, 1), groove, unit * 1.6f);
	/* the steps at the bottom of the side panels */
	for (int side = 0; side < 2; side++) {
		auto X = [side](float x) { return side ? 1 - x : x; };
		const ImVec2 step[4] = { P(X(.035f), 1), P(X(.035f), .70f), P(X(.13f), .70f), P(X(.13f), 1) };
		dl->AddPolyline(step, 4, groove, unit * 1.3f);
	}
	/* the label's recess: a rounded inset, its edge in shadow; the label inside, filling it (the cover cropped to
	 * the recess's shape, centred, so it keeps its proportions) */
	const float lx = .245f * sw, ly = .085f * sh, lw = .51f * sw, lh = .83f * sh;
	dl->AddRectFilled(F(lx - 3, ly - 3), F(lx + lw + 3, ly + lh + 3), shadow, 7 * unit);
	{
		const float k = std::max(lw / 236, lh / 208);
		const float crop = std::max(0.0f, (236 - lw / k) / 2), ox = lx + lw / 2 - 118 * k;
		drawLabelPart(dl, look, font, [&](ImVec2 v) { return F(ox + v.x * k, ly + v.y * k); }, lh / k, crop);
	}
	dl->AddRect(F(lx - 1, ly - 1), F(lx + lw + 1, ly + lh + 1), A(IM_COL32(0x4a, 0x4c, 0x50, 255)), 6 * unit, 1.5f * unit);
	/* the region's flag, on the right of the label */
	if (flag && look.region) {
		const float fw = 22 * sw / 196;
		flag(.86f * sw - 12 * sw / 196, .30f * sh, fw);
	}
	/* the shell's edge */
	dl->AddPolyline(shell, n, A(on ? colourOf(look) : IM_COL32(0x55, 0x57, 0x5c, 255)), unit * (on ? 2.2f : 1.2f), ImDrawFlags_Closed);
}

namespace {

} // namespace

/* ---------------------------------------------------------------- poses */

Pose poseOf(View view)
{
	switch (view) {
	case View::Home: return { 0, 0, 55, -16, .86f, 1 };
	case View::Bench: return { 95, 110, 36, -5, .62f, 1 };
	case View::Live: return { -190, 60, 60, -22, .70f, 1 };
	case View::Rack: return { 520, -330, 64, -14, .50f, .22f };
	}
	return { 0, 0, 55, -16, .86f, 1 };
}

Pose mix(const Pose &a, const Pose &b, float t)
{
	auto m = [t](float x, float y) { return x + (y - x) * t; };
	return { m(a.tx, b.tx), m(a.ty, b.ty), m(a.rx, b.rx), m(a.rz, b.rz), m(a.scale, b.scale), m(a.opacity, b.opacity) };
}

float ease(float t)
{
	/* cubic-bezier(.2,.8,.2,1): x(u) = 3(1-u)^2 u .2 + 3(1-u) u^2 .2 + u^3, solved for u by bisection */
	t = std::clamp(t, 0.0f, 1.0f);
	float lo = 0, hi = 1, u = t;
	for (int i = 0; i < 24; i++) {
		u = (lo + hi) / 2;
		const float x = 3 * (1 - u) * (1 - u) * u * .2f + 3 * (1 - u) * u * u * .2f + u * u * u;
		(x < t ? lo : hi) = u;
	}
	return 3 * (1 - u) * (1 - u) * u * .8f + 3 * (1 - u) * u * u * 1.0f + u * u * u;
}

ImVec2 Projector::operator()(ImVec2 b) const
{
	const float rx = pose.rx * kPi / 180, rz = pose.rz * kPi / 180;
	const float x = (b.x - kBoardW / 2) * pose.scale, y = (b.y - kBoardH / 2) * pose.scale;
	const float x1 = x * std::cos(rz) - y * std::sin(rz), y1 = x * std::sin(rz) + y * std::cos(rz);
	const float y2 = y1 * std::cos(rx), z2 = y1 * std::sin(rx);
	const float w = 1 - z2 / kPerspective;
	return stage(ImVec2(kOriginX + pose.tx + x1 / w, kOriginY + pose.ty + y2 / w));
}

/* ---------------------------------------------------------------- looks */

ImU32 hsl(float h, float s, float l, float a)
{
	h = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f) / 60.0f;
	const float c = (1 - std::fabs(2 * l - 1)) * s, x = c * (1 - std::fabs(std::fmod(h, 2.0f) - 1)), m = l - c / 2;
	float r = 0, g = 0, b = 0;
	switch ((int)h) {
	case 0: r = c, g = x; break;
	case 1: r = x, g = c; break;
	case 2: g = c, b = x; break;
	case 3: g = x, b = c; break;
	case 4: r = x, b = c; break;
	default: r = c, b = x; break;
	}
	return IM_COL32((int)((r + m) * 255 + .5f), (int)((g + m) * 255 + .5f), (int)((b + m) * 255 + .5f), (int)(a * 255 + .5f));
}

ImU32 colourOf(const Look &look, float lightness)
{
	return hsl(look.hue, .92f, lightness);
}

Look lookOf(const std::string &name, const std::string &code)
{
	uint32_t h = 2166136261u; /* FNV-1a */
	for (const unsigned char c : name)
		h = (h ^ c) * 16777619u;
	Look look;
	look.seed = h;
	look.hue = (float)((h >> 8) % 360);
	look.code = code;
	if (look.code.empty()) { /* no header: the name's first letters */
		for (const char c : name)
			if (std::isalnum((unsigned char)c) && look.code.size() < 4)
				look.code += (char)std::toupper((unsigned char)c);
	}
	return look;
}

void drawLabel(ImDrawList *dl, const Look &look, ImFont *font, const Transform &to)
{
	drawLabelPart(dl, look, font, to, 208);
}

ImVec2 anchor(const char *name)
{
	auto centre = [](Box b) { return ImVec2(b.x + b.w / 2, b.y + b.h / 2); };
	if (!strcmp(name, "slot")) return centre(kSlot);
	if (!strcmp(name, "cpu")) return centre(kCpu);
	if (!strcmp(name, "rcp")) return centre(kRcp);
	if (!strcmp(name, "rdp")) return ImVec2(kRcp.x + kRcp.w * .75f, kRcp.y + kRcp.h / 2);
	if (!strcmp(name, "xtal")) return centre(kXtal1);
	if (!strcmp(name, "exp")) return centre(kExp);
	if (!strcmp(name, "vdac")) return centre(kVdac);
	if (!strcmp(name, "adac")) return centre(kAdac);
	if (!strcmp(name, "pif")) return centre(kPif);
	if (!strcmp(name, "ram")) return centre(kRamA);
	if (!strncmp(name, "port", 4) && name[4] >= '1' && name[4] <= '4')
		return ImVec2(kPortX[name[4] - '1'] + 107, 1082 + 50);
	return ImVec2(kBoardW / 2, kBoardH / 2);
}

/* ---------------------------------------------------------------- the moving parts */

void Machine::step(float dt, const Look *wanted)
{
	time += dt;
	boot += dt;
	/* the cartridge: out, swap, in, boot (the design's tick) */
	const bool other = wanted && (!has_cartridge || wanted->seed != shown.seed);
	if (other && seat_dir == 0)
		seat_dir = has_cartridge ? -1 : 1;
	if (wanted && has_cartridge && !other && seat_dir == 0) {
		/* the same cartridge: its cover, come since it was seated */
		shown.cover = wanted->cover;
		shown.cover_aspect = wanted->cover_aspect;
	}
	if (!has_cartridge && wanted) {
		shown = *wanted;
		has_cartridge = true;
		seat = 0;
	}
	if (seat_dir < 0) {
		seat -= dt / .16f;
		if (seat <= 0) {
			seat = 0;
			if (wanted)
				shown = *wanted;
			seat_dir = 1;
		}
	} else if (seat_dir > 0) {
		seat += dt / .3f;
		if (seat >= 1) {
			seat = 1;
			seat_dir = 0;
			boot = 0;
		}
	}
	auto wob = [this](float f, float p) { return .5f + .5f * std::sin(time * f + p); };
	struct Base {
		const char *name;
		float idle, live;
	};
	const Base bases[] = { { "cic", .55f, .1f }, { "pi", .16f, .45f }, { "pifcpu", .14f, .4f }, { "xtal", .7f, 1 },
		                   { "cpurcp", .06f, 1 }, { "ram1", .05f, .9f }, { "ram2", .05f, .8f }, { "exp", 0, expansion ? .7f : 0 },
		                   { "video", .04f, 1 }, { "vout", .04f, 1 }, { "audio", .03f, .85f }, { "aout", .03f, .85f },
		                   { "in0", .12f, .35f + .5f * (wob(2.3f, 0) > .8f) }, { "in1", 0, 0 }, { "in2", 0, 0 }, { "in3", 0, 0 } };
	const auto &all = buses();
	for (size_t i = 0; i < all.size() && i < 24; i++) {
		float base = 0;
		for (const Base &b : bases)
			if (!strcmp(b.name, all[i].name))
				base = running ? b.live : b.idle;
		const float wave = seat >= 1 ? std::exp(-std::pow((boot - all[i].delay) / .22f, 2.0f)) : 0;
		const float target = std::max(base, wave) * (seat >= 1 || running ? 1 : .2f);
		levels[i] += (target - levels[i]) * std::min(1.0f, dt * 9);
	}
	const float bump = std::exp(-std::pow((boot - .6f) / .3f, 2.0f)) * .7f, oc = overclock ? .82f : 1.0f;
	const float sc[] = { .5f, .7f, 1, 1.9f };
	const int sci = upscale >= 8 ? 3 : upscale >= 4 ? 2 : upscale >= 2 ? 1 : 0;
	const float tc = running ? (.58f + .1f * wob(.9f, 1) + .05f * wob(5, 2)) * oc : .03f + bump;
	const float ts = running ? .2f + .1f * wob(1.3f, 3) : .02f + bump * .6f;
	const float td = running ? std::min(1.0f, (.3f + .12f * wob(.7f, 5)) * sc[sci]) : .02f + bump * .5f;
	load_cpu += (tc - load_cpu) * std::min(1.0f, dt * 6);
	load_rsp += (ts - load_rsp) * std::min(1.0f, dt * 6);
	load_rdp += (td - load_rdp) * std::min(1.0f, dt * 6);
}

void drawBoard(ImDrawList *dl, ImTextureRef texture, const Projector &p, const Machine &m, const Fonts &fonts)
{
	const float op = p.pose.opacity;
	/* the board's picture, as a grid so the perspective holds across it */
	if (texture.GetTexID() != ImTextureID_Invalid || texture._TexData) {
		constexpr int GX = 40, GY = 25;
		dl->PushTexture(texture);
		dl->PrimReserve(GX * GY * 6, (GX + 1) * (GY + 1));
		const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
		const ImU32 tint = IM_COL32(255, 255, 255, (int)(255 * op));
		for (int j = 0; j <= GY; j++)
			for (int i = 0; i <= GX; i++) {
				const float u = (float)i / GX, v = (float)j / GY;
				dl->PrimWriteVtx(p(ImVec2(u * kBoardW, v * kBoardH)), ImVec2(u, v), tint);
			}
		for (int j = 0; j < GY; j++)
			for (int i = 0; i < GX; i++) {
				const ImDrawIdx a = (ImDrawIdx)(base + j * (GX + 1) + i), b = (ImDrawIdx)(a + 1), c = (ImDrawIdx)(a + GX + 1),
				                d = (ImDrawIdx)(c + 1);
				dl->PrimWriteIdx(a);
				dl->PrimWriteIdx(b);
				dl->PrimWriteIdx(d);
				dl->PrimWriteIdx(a);
				dl->PrimWriteIdx(d);
				dl->PrimWriteIdx(c);
			}
		dl->PopTexture();
	}

	/* the moving parts, in board pixels, then through the projection */
	const int start = dl->VtxBuffer.Size;
	dl->PushClipRect(ImVec2(-1e6f, -1e6f), ImVec2(1e6f, 1e6f));
	const auto &all = buses();
	for (size_t i = 0; i < all.size(); i++) {
		const Bus &b = all[i];
		const float level = m.levels[i];
		if (level < .02f)
			continue;
		const float speed = !strcmp(b.name, "xtal") ? (m.overclock ? 620.0f : 420.0f)
		                    : (!strcmp(b.name, "video") || !strcmp(b.name, "vout"))
		                        ? 300.0f + (m.upscale >= 8 ? 3 : m.upscale >= 4 ? 2 : m.upscale >= 2 ? 1 : 0) * 90.0f
		                        : b.speed;
		for (size_t k = 0; k < b.lines.size(); k++) {
			const auto &line = b.lines[k];
			/* the glow under it (the design's blurred emissive layer), the trace lit, its moving signals */
			dl->AddPolyline(line.data(), (int)line.size(), alpha(b.colour, level * .10f * op), 9.0f);
			dl->AddPolyline(line.data(), (int)line.size(), alpha(b.colour, level * .32f * op), 2.4f);
			dashes(dl, line, b.seg, b.period, m.time * speed - k * 53.0f, alpha(b.colour, std::min(1.0f, level * 1.25f) * op), 3.4f);
		}
	}
	auto meter = [&](float x, float y, float w, float v, ImU32 col) {
		for (int i = 0; i < 12; i++) {
			const bool on = i < (int)std::lround(v * 12);
			dl->AddRectFilled(ImVec2(x + i * (w / 12), y), ImVec2(x + i * (w / 12) + w / 12 - 4, y + 11),
			                  on ? alpha(col, op) : IM_COL32(255, 255, 255, (int)(18 * op)));
		}
	};
	auto halo = [&](Box b, float v, ImU32 col) {
		roundRect(dl, Box{ b.x - 4, b.y - 4, b.w + 8, b.h + 8 }, alpha(col, std::min(.9f, v) * op), 10, 4);
	};
	meter(kCpu.x + 46, kCpu.y + 164, kCpu.w - 92, m.load_cpu, kData);
	halo(kCpu, m.load_cpu, kData);
	meter(kRcp.x + 18, kRcp.y + 170, kRcp.w / 2 - 36, m.load_rsp, kKey);
	meter(kRcp.x + kRcp.w / 2 + 18, kRcp.y + 170, kRcp.w / 2 - 36, m.load_rdp, kVideo);
	halo(kRcp, std::max(m.load_rsp, m.load_rdp), kVideo);
	/* the power light, and port 1's light (a controller in it) */
	dl->AddEllipseFilled(ImVec2(960, 1118), ImVec2(26, 34), alpha(IM_COL32(0xff, 0x4a, 0x3d, 255), .25f * op));
	dl->AddEllipseFilled(ImVec2(960, 1118), ImVec2(12, 18), alpha(IM_COL32(0xff, 0x4a, 0x3d, 255), op));
	for (int i = 0; i < 4; i++) {
		const Box pb = kPort(i);
		dl->AddCircleFilled(ImVec2(pb.x + pb.w - 24, pb.y + 22), 6, i == 0 ? alpha(kKey, op) : IM_COL32(255, 255, 255, (int)(30 * op)));
	}
	/* the expansion slot: the Expansion Pak, or the jumper the console comes with */
	if (m.expansion) {
		const ImVec2 a(kExp.x + 4, kExp.y - 14), b(kExp.x + kExp.w - 4, kExp.y + kExp.h - 4);
		dl->AddRectFilledMultiColor(a, b, alpha(IM_COL32(0x4a, 0x1a, 0x15, 255), op), alpha(IM_COL32(0x4a, 0x1a, 0x15, 255), op),
		                            alpha(IM_COL32(0x1c, 0x0a, 0x0a, 255), op), alpha(IM_COL32(0x1c, 0x0a, 0x0a, 255), op));
		dl->AddRect(a, b, IM_COL32(255, 120, 100, (int)(140 * op)), 8, 2);
		silk(dl, fonts.mono, "EXPANSION PAK \xC2\xB7 +4 MB", kExp.x + kExp.w / 2, kExp.y + 26, 16, true, .92f * op);
		dl->AddCircleFilled(ImVec2(kExp.x + kExp.w - 20, kExp.y - 2), 5, alpha(IM_COL32(0xff, 0x6a, 0x55, 255), op));
	} else {
		const ImVec2 a(kExp.x + 96, kExp.y - 6), b(kExp.x + 216, kExp.y + kExp.h - 10);
		dl->AddRectFilled(a, b, alpha(IM_COL32(0x10, 0x18, 0x1c, 255), op), 6);
		dl->AddRect(a, b, IM_COL32(255, 255, 255, (int)(51 * op)), 6);
		silk(dl, fonts.mono, "JUMPER", kExp.x + kExp.w / 2, kExp.y + 32, 13, true, .7f * op);
	}
	silk(dl, fonts.mono, m.overclock ? "125.000 MHz" : "93.750 MHz", kXtal1.x, kXtal1.y - 10, 14, false, .9f * op);
	if (m.overclock)
		roundRect(dl, Box{ kXtal1.x - 4, kXtal1.y - 4, kXtal1.w + 8, kXtal1.h + 8 }, alpha(kAudio, .7f * op), 26, 3);

	/* the slot's housing narrowed to the pak: its dark plastic (the art's own colour) over the pin rows (y 192..288,
	 * x 656..1286) outside the pak's footprint, an edge line (the art's housing edge colour) where it meets the pak */
	{
		const ImU32 plastic = IM_COL32(5, 9, 11, 255), edge = IM_COL32(45, 48, 50, 255);
		const float l = 971 - 315 * kPakFit, r = 971 + 315 * kPakFit, t = 188, b = 296;
		dl->AddRectFilled(ImVec2(648, t), ImVec2(l, b), plastic);
		dl->AddRectFilled(ImVec2(r, t), ImVec2(1294, b), plastic);
		dl->AddLine(ImVec2(l, t), ImVec2(l, b), edge, 2);
		dl->AddLine(ImVec2(r, t), ImVec2(r, b), edge, 2);
	}
	dl->PopClipRect();
	transformFrom(dl, start, [&p](ImVec2 v) { return p(v); });

	/* the cartridge, sliding down into the slot from above. It stands upright in the slot, as a real one does
	 * (the board is tilted, the pak is not): its bottom edge is the slot's pin row, projected (board x 656..1286,
	 * y 288: the pin rows of the art are at y 192..217 and 263..288), its sides vertical on screen, and its front
	 * face the strip's proportions (196:126), so it is the strip's cartridge sheared to the slot's slant. */
	if (m.has_cartridge) {
		const float s = m.seat, e = s * s * (3 - 2 * s);
		/* at full pin-row width an upright pak would reach the top bar, so it is scaled to kPakFit of the row, centred
		 * on it, keeping its proportions; the housing is narrowed to match (above) */
		const ImVec2 b0 = p(ImVec2(971 - 315 * kPakFit, 288)), b1 = p(ImVec2(971 + 315 * kPakFit, 288));
		const float len = std::hypot(b1.x - b0.x, b1.y - b0.y), face = len * 126 / 196, unit = len / 196;
		const float lift = (1 - e) * face * 1.4f; /* screen pixels above its seat */
		auto to = [&](ImVec2 v) {
			const float u = v.x / 196;
			return ImVec2(b0.x + (b1.x - b0.x) * u, b0.y + (b1.y - b0.y) * u - (126 - v.y) / 126 * face - lift);
		};
		const FlagHook flag = [&](float x, float y, float w) {
			if (m.flag)
				m.flag(dl, m.shown.region, to(ImVec2(x, y)).x, to(ImVec2(x, y)).y, w * unit);
		};
		drawCartridge(dl, m.shown, fonts.mono, to, 196, 126, unit, true, op, flag);
		dl->AddCircleFilled(to(ImVec2(196 - 8, 10)), 6 * unit, alpha(colourOf(m.shown), e * op));
	}
}

} // namespace psgopher::board
