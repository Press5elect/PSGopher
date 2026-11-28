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
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <string>
#include <vector>

extern "C" {
#include "platform.h"
}

#include "board.h"
#include "expansion_pak.h"
#include "game_settings.h"
#include "imgui.h"
#include "library.h"
#include "title_settings.h"
#include "update.h"

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
	/* the sticks and triggers (-1..1 and 0..1), for binding a profile's input to one */
	float left_x = 0, left_y = 0, right_x = 0, right_y = 0, l2 = 0, r2 = 0;
};

/* a controller binding of an input profile (psgopher.rs's psgopher_profile_binding): kind 0 none,
 * 1 a gamepad button (SDL_GamepadButton), 2 a gamepad axis (SDL_GamepadAxis, its direction), 3 a
 * raw joystick's */
struct Binding {
	int kind = 0, id = 0, axis = 0;
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
	std::function<void(const Game &, int)> benchmark;  // a benchmark from a probe (1x 2x 4x 8x)
	std::function<void()> shares;                      // the share manager
	std::function<void()> close;                       // close PSGopher
	/* the updater (update.h): its status, polled each frame, and its steps; install() true means the
	 * files are in place and the title must close (the front end then takes close's way) */
	std::function<update::Status()> update_status;
	std::function<void()> update_check, update_download, update_cancel, update_skip, update_rollback;
	std::function<bool()> update_install;
	std::function<void(const std::string &)> tell;     // a notice
	std::function<bool()> elevated;                    // out of the sandbox at this start
	std::function<bool()> elevation_off;               // elevation-off.txt is there
	std::function<void(bool)> set_elevation_off;
	std::function<int()> share_count;
	std::function<bool()> busy;                        // a copy from a share is under way
	std::function<std::string()> notice;               // what was said last ("" none)
	/* Gopher64's cheats of a game (by its key): "name\tnote\toptions\ton\toption" lines; one set */
	std::function<std::string(const std::string &)> cheats_of;
	std::function<bool(const std::string &, const std::string &, bool, const std::string &)> cheat_set;
	/* the cartridge's cover (libretro's box art): its texture and width / height; invalid while
	 * there is none (asked for when asked here) */
	std::function<std::pair<ImTextureID, float>(const Game &)> cover;
	/* a probe's picture (the save state file's path + ".png"): its texture and width / height;
	 * invalid while there is none */
	std::function<std::pair<ImTextureID, float>(const std::string &)> probe_picture;
	/* its description ("title\ntext": Wikipedia's summary), "" while there is none */
	std::function<std::string(const Game &)> about;
	/* Gopher64's text settings (psgopher_setting_text): the Transfer Pak's files, each port's profile
	 * and controller */
	std::function<std::string(const char *)> text;
	std::function<bool(const char *, const std::string &)> set_text;
	/* its input profiles: the names, a new one (Gopher64's default bindings), one taken away, an
	 * input's binding and a new one, the deadzone (0) and DirectInput (1) */
	std::function<std::vector<std::string>()> profiles;
	std::function<bool(const std::string &)> profile_new, profile_delete;
	std::function<Binding(const std::string &, int)> binding;
	std::function<bool(const std::string &, int, const Binding &)> bind;
	std::function<int64_t(const std::string &, int, int64_t, bool)> profile_number;
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
	int sort_ = 0, show_ = 0, region_ = 0; // region_: 0 all, 1 USA, 2 Europe (PAL), 3 Japan, 4 other (R3)
	int bench_section_ = 0, bench_sel_ = 0;
	float bench_scroll_ = 0;
	/* a game's page (Triangle on HOME): BENCH's Video, Screen & Sound, Emulation and Controllers rows for one
	 * game, each starting at Use Global Setting (game_settings.h); BENCH's own place is kept meanwhile */
	bool game_page_ = false;
	Game page_game_;
	std::string page_key_;
	GameSettings page_overrides_;
	int page_saved_section_ = 0, page_saved_sel_ = 0;
	void openGamePage(const Game &g);
	/* BENCH's markers: the chosen game's own settings (looked up once a game, again when the list changes) */
	bool rack_chosen_ = false; // BENCH was entered from the library: its game is the rack's
	std::string marker_path_, marker_key_;
	GameSettings marker_overrides_;
	unsigned marker_version_ = ~0u;
	const GameSettings &markersOf(const Game &g);
	/* a game's Expansion Pak: what the table says of it, the row as set for it (its own, else the global one) and whether
	 * the pak is on for it */
	struct PakView {
		PakInfo info;
		int setting = PakAuto;
		bool on = true;
	};
	PakView pakView(const Game &g);
	/* a game started: asked first when it needs the Expansion Pak and that is set to Off */
	void startGame(const Game &g, int slot);
	bool pak_ask_ = false;
	Game pak_game_;
	int pak_slot_ = -1;
	std::string bench_profile_;       // the profile BENCH's Profiles section edits
	/* binding an input: the profile and input, when it began, and whether the button that began it
	 * was let go */
	int capture_input_ = -1;
	std::string capture_profile_;
	float capture_since_ = 0;
	bool capture_released_ = false;
	/* the file picker (a Transfer Pak's Game Boy ROM or save): its folder, the setting it writes, its
	 * cursor (-1 closed) */
	int picker_sel_ = -1;
	std::string picker_folder_, picker_key_;
	bool picker_rom_ = true;
	int picker_kind_ = 0; // 0 a Game Boy ROM or save (picker_rom_), 2 the 64DD IPL (files of 4 MB)
	std::function<void(const std::string &)> picker_done_; // set: the chosen file goes to it (else to picker_key_)
	void filePicker(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f);
	/* the game folders (BENCH > Console): 0 the Game Boy picker above, 1 the list of chosen folders,
	 * 2 choosing one to add (picker_folder_ is where it is); a change searches again when it closes */
	int picker_mode_ = 0;
	bool folders_changed_ = false;
	bool folders_error_ = false;  // the last change could not be saved (shown in the chooser)
	bool folders_opened_ = false; // the frame it opens: the Cross that opened it is not taken again
	void folderPicker(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f);
	bool early_checked_ = false, early_warn_ = false; // the one-time early-build warning: asked once, up
	bool close_open_ = false; // "Close PSGopher?" is up
	/* "Install vX.Y.Z?" / "Roll back to ...?" is up (1 install, 2 roll back); the roll back's answer is waited for */
	int update_ask_ = 0;
	std::string update_ask_text_;
	bool rollback_wanted_ = false;
	char letter_ = 0;         // the letter just jumped to, and when (the pad's clock)
	float letter_at_ = -10;
	int probes_open_ = -1; // the probe picker: its cursor, -1 closed
	struct Cheat {
		std::string name, note;
		std::vector<std::string> options;
		bool on = false;
		std::string option;
	};
	int cheats_open_ = -1; // the cheat picker: its cursor, -1 closed
	std::vector<Cheat> cheats_;
	void readCheats(const Game &g);
	void cheatPicker(ImDrawList *dl, const PadInput &pad, const FrontendFonts &f, const Game &g);
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
	board::Look lookFor(const Game &g) const; // its look, with its cover when there is one
	/* a probe's picture at 4:3 (the N64's pixels are not square), or its fingerprint when it has none */
	void probeThumb(ImDrawList *dl, const std::string &stateFile, const board::Look &look, float x, float y, float w, float h);
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
