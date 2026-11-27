// PSGopher lockstep harness: RetroAchievements is off (no client, never hardcore).
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.

#[derive(Default, Clone, serde::Serialize, serde::Deserialize)]
pub struct RAConfig {
    pub username: String,
    pub token: String,
    pub enabled: bool,
    pub hardcore: bool,
    pub challenge: bool,
    pub leaderboard: bool,
    pub rich_presence: bool,
}

pub fn load_game(_rom: &[u8], _rom_size: usize, _rich_presence: bool) -> (Option<()>, Option<()>) {
    (None, None)
}
pub fn unload_game(_watch: Option<()>, _handle: Option<()>) {}
pub fn set_rdram(_rdram: *const u8, _rdram_size: usize) {}
pub fn do_frame() {}
pub fn do_idle() {}
pub fn get_hardcore() -> bool {
    false
}
pub fn state_size() -> usize {
    0
}
pub fn save_state(_state: *mut u8, _state_size: usize) {}
pub fn load_state(_state: *const u8, _state_size: usize) {}
