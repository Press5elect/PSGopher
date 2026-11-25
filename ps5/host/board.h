/*
 * PSGopher - the Live Board's mainboard on the console (ps5/art/live-board.html is the design).
 *
 * The board is the design's flat 2000x1250 canvas: its still layer as a picture (ps5/art/board.png,
 * twice the size), drawn as a grid of triangles through the design's CSS 3D transform (a
 * translation, perspective(2600px), rotateX, rotateZ, scale, about the board's centre), one pose
 * per view, eased from one to the next as the design's transition is. Over it, in the board's own
 * coordinates and through the same projection, the moving parts the design's drawDynamic draws:
 * the signals on their buses, the chips' meters and halos, the lights, the Expansion Pak or the
 * jumper, the crystal's frequency and the cartridge sliding into its slot.
 *
 * Cartridge labels are the design's (makeLabel): drawn from the game's name as polygons, clipped to
 * the label here, so the same label is drawn flat or seated in the board's slot.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "imgui.h"

namespace psgopher::board {

/* ---- the design's units: the stage is 1920x1080, the board 2000x1250 ---- */
constexpr float kStageW = 1920, kStageH = 1080, kBoardW = 2000, kBoardH = 1250;

enum class View { Home, Rack, Bench, Live };

struct Pose {
	float tx, ty, rx, rz, scale, opacity;
};
Pose poseOf(View view);
/* a to b at t (0..1) */
Pose mix(const Pose &a, const Pose &b, float t);
/* the design's transition: cubic-bezier(.2,.8,.2,1) at t */
float ease(float t);

/* A board point (0..2000, 0..1250) to the screen: the pose, then the stage (1920x1080) to the
 * screen's pixels (unit pixels a stage pixel, the stage at origin). */
struct Projector {
	Pose pose;
	float unit = 2.0f;
	ImVec2 origin = ImVec2(0, 0);
	ImVec2 operator()(ImVec2 board) const;
	ImVec2 stage(ImVec2 stage_point) const { return ImVec2(origin.x + stage_point.x * unit, origin.y + stage_point.y * unit); }
};

/* ---- a game's look (the design's hue and label) ---- */
struct Look {
	uint32_t seed = 0;  // from the game's name
	float hue = 0;      // 0..360
	std::string code;   // what the label's band says ("NCKE")
	/* the cartridge's box art (libretro's), drawn in the label's place when there is one */
	ImTextureID cover = ImTextureID_Invalid;
	float cover_aspect = 0; // its width / height
};
Look lookOf(const std::string &name, const std::string &code);
ImU32 hsl(float h, float s, float l, float a = 1.0f); // h 0..360, s and l 0..1
ImU32 colourOf(const Look &look, float lightness = 0.62f);

/* A label (the design's 236x208 picture, or the cover filling it) drawn into rect a..b through `to` (label pixels to the
 * screen: flat for a strip, the projector for the slot). font for the band's code. */
using Transform = std::function<ImVec2(ImVec2)>;
void drawLabel(ImDrawList *dl, const Look &look, ImFont *font, const Transform &to);
/* its top part: visible_height of the label's 208 units (the slot shows less of it); the box art when
 * there is one, else the drawn label */
void drawLabelPart(ImDrawList *dl, const Look &look, ImFont *font, const Transform &to, float visible_height);

/* ---- the moving parts ---- */
struct Machine {
	/* the settings the board shows */
	bool expansion = true; // the Expansion Pak (false: the jumper)
	bool overclock = false;
	int upscale = 1;       // 1, 2, 4, 8
	bool crt = false;
	/* what the board does */
	bool running = false;  // a game runs (LIVE): every bus busy
	float seat = 1.0f;     // the cartridge: 0 out, 1 seated
	int seat_dir = 0;      // -1 coming out (to swap), 1 going in
	float boot = 9.0f;     // seconds since it was seated (the boot wave)
	float time = 0;
	float load_cpu = 0, load_rsp = 0, load_rdp = 0;
	float levels[24] = {}; // each bus's level
	Look shown;            // the cartridge in the slot
	bool has_cartridge = false;
	/* the next frame (dt seconds): the cartridge's slide and the buses' levels; `wanted` is the
	 * game whose cartridge should be in the slot */
	void step(float dt, const Look *wanted);
};

struct Fonts {
	ImFont *mono = nullptr, *display = nullptr;
};

/* The board: its picture (texture, 0 for none) and the moving parts over it. */
void drawBoard(ImDrawList *dl, ImTextureRef texture, const Projector &p, const Machine &m, const Fonts &fonts);

/* A board point by name, for the lines the views draw to the board: "slot", "cpu", "rcp", "xtal",
 * "exp", "vdac", "adac", "pif", "port1".."port4" (the part's centre). */
ImVec2 anchor(const char *name);

} // namespace psgopher::board
