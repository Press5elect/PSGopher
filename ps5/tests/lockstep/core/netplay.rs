// PSGopher lockstep harness: netplay is never active (device.netplay stays None); these are the
// names the interpreter sources refer to.
//
// SPDX-License-Identifier: GPL-3.0-or-later
use crate::{device, ui};

pub struct NetplayConfig {
    pub server_addr: String,
    pub player_number: usize,
    pub number_of_players: usize,
    pub input_delay: usize,
    pub ice_config_path: std::path::PathBuf,
}

pub struct Session;
impl Session {
    pub fn frames_ahead(&self) -> i32 {
        0
    }
}

pub struct Netplay {
    pub player_number: usize,
    pub inputs: Vec<ui::input::InputData>,
    pub connected: [bool; 4],
    pub input_delay: usize,
    pub session: Session,
}

pub fn in_rollback(_netplay: Option<&Netplay>) -> bool {
    false
}
pub fn replaying_audio(_netplay: Option<&Netplay>) -> bool {
    false
}
pub fn process_requests(_device: &mut device::Device) -> Vec<ui::input::InputData> {
    vec![ui::input::InputData::default(); 4]
}
pub fn init(_device: &mut device::Device, _config: &NetplayConfig, _pal: bool) -> Option<Netplay> {
    None
}
pub fn close(_netplay: &mut Netplay) {}
pub fn send_rtc(_netplay: &mut Netplay, _rtc: i64) {}
pub fn receive_rtc(_netplay: &mut Netplay) -> i64 {
    0
}
pub fn send_rng(_netplay: &mut Netplay, _seed: u64) {}
pub fn receive_rng(_netplay: &mut Netplay) -> u64 {
    0
}
pub fn send_save(_netplay: &mut Netplay, _save_type: &str, _save_data: &[u8]) {}
pub fn receive_save(_netplay: &mut Netplay, _save_type: &str, _save_data: &mut Vec<u8>) {}
pub fn change_input_delay(_netplay: &mut Netplay, _input_delay: usize) {}
