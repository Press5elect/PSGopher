/*
 * PSGopher - the front end: the Live Board (ps5/art/live-board.html), drawn with Dear ImGui's draw
 * lists in the design's stage units (1920x1080, doubled on a 4K display).
 *
 *   HOME   the mainboard with the chosen cartridge seated in its slot, the panel (the cartridge's
 *          facts as Gopher64 reads them, its play time, its probes: save states), the cartridge
 *          strip (recently played first, then the rest).
 *   RACK   every cartridge on shelves of spines, A to Z with their letters, sorted and filtered
 *          (Internal, USB, Network among the filters), and the panel.
 *   BENCH  the board's parts as the settings: Gopher64's own (config.json) and PSGopher's
 *          (psgopher.ini), each a callout drawn to its part; "This console" for the shares, the
 *          storage, the search, About and closing.
 *
 * The pad drives it directly (no ImGui navigation): L1/R1 views, the D-pad moves, Cross acts,
 * Square and Triangle as the hints say, Circle back.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

extern "C" {
#include "platform.h"
}

#include "board.h"
#include "imgui.h"
#include "library.h"
#include "title_settings.h"

namespace psgopher {

/* L2 and R2 as buttons (from their triggers, past half way): bits the console's numbering leaves free */
constexpr uint32_t PAD_L2_DIGITAL = 1u << 28, PAD_R2_DIGITAL = 1u << 29;

/* the pad's state this frame (the console's button numbering, platform.h) */
struct PadInput {
	uint32_t held = 0, pressed = 0;
	bool repeat(uint32_t button) const; // pressed now, or held long enough to repeat
	float now = 0;
	float held_since[32] = {};
	float last_repeat[32] = {};
};

struct FrontendFonts {
	ImFont *display = nullptr; // Bricolage Grotesque ExtraBold
	ImFont *mono = nullptr;    // JetBrains Mono Regular
	ImFont *mono_bold = nullptr; // JetBrains Mono SemiBold
	ImFont *mono_heavy = nullptr; // JetBrains Mono ExtraBold
};

/* What the front end asks of the title */
struct FrontendHost {
	std::function<int64_t(const char *)> get;          // Gopher64's setting
	std::function<void(const char *, int64_t)> set;
	TitleSettings *title = nullptr;                    // PSGopher's (saved on each change)
	std::string display_at_start;                      // the refresh the display opened with
	std::function<void(const Game &, int)> play;       // a game, with a probe's slot (-1 none)
	std::function<void()> shares;                      // the share manager
	std::function<void()> close;                       // close PSGopher
	std::function<void(const std::string &)> tell;     // a notice
	std::function<bool()> elevated;                    // out of the sandbox at this start
	std::function<bool()> elevation_off;               // elevation-off.txt is there
	std::function<void(bool)> set_elevation_off;
	std::function<int()> share_count;
	std::function<bool()> busy;                        // a copy from a share is under way
	std::function<std::string()> notice;               // what was said last ("" none)
};

class Frontend {
public:
	explicit Frontend(FrontendHost host);
	/* one frame: the whole screen (w x h pixels), dt seconds since the last */
	void frame(ImDrawList *dl, const PadInput &pad, float w, float h, float dt, ImTextureRef board, ImTextureRef logo,
	           const FrontendFonts &fonts);
	/* which view (for the tests' pictures) */
	board::View view() const { return view_; }
	void setView(board::View v);

private:
	FrontendHost host_;
	board::View view_ = board::View::Home, from_ = board::View::Home;
	float blend_ = 1; // 0..1 from from_ to view_
	board::Machine machine_;
	/* the cartridges: the strip's order (recent first) and the rack's (sorted, filtered) */
	std::vector<Game> games_;
	unsigned version_ = ~0u;
	std::vector<int> strip_, rack_;
	int strip_sel_ = 0, rack_sel_ = 0;
	float strip_x_ = 0; // the strip's scroll (eased)
	float rack_scroll_ = 0;
	int sort_ = 0, show_ = 0;
	int bench_sel_ = 0;
	int probes_open_ = -1; // the probe picker: its cursor, -1 closed
	std::vector<Probe> probes_;
	std::vector<std::string> probe_files_;
	std::string probes_for_;
	struct Played {
		double seconds = 0;
		int64_t last = 0;
	};
	std::vector<Played> played_; // by games_ index

	void refresh();
	void order();
	const Game *selected() const;
	void home(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f);
	void rack(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f);
	void bench(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f);
	void panel(ImDrawList *dl, const FrontendFonts &f, const Game &g, bool rack_panel, float x, float y, float alpha);
	void probePicker(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f, const Game &g);
	void header(ImDrawList *dl, ImTextureRef logo, const FrontendFonts &f, float w);

	/* the stage's units */
	float u_ = 2;
	ImVec2 S(float x, float y) const { return ImVec2(x * u_, y * u_); }
	float L(float v) const { return v * u_; }
	board::Projector projector_;
};

} // namespace psgopher
