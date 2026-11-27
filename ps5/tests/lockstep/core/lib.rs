// PSGopher lockstep harness: the crate root of one "core" build (original or candidate).
//
// run-lockstep.sh copies a Gopher64 tree's interpreter sources (src/device.rs, src/device/,
// src/cheats.rs, src/savestates.rs) next to this file, unchanged except for one injected call
// (lockstep_hook::rsp_task after each RSP burst, in the scratch copy only). Everything Gopher64
// reaches outside the emulated machine (window, GPU, audio, input, netplay, RetroAchievements) is
// replaced by the stubs here, which are the same for both builds: no SDL, no parallel-rdp, no
// Vulkan, no wall clock in the emulated path.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
#![allow(warnings)]

pub mod cheats;
pub mod device;
pub mod lockstep_hook;
pub mod netplay;
pub mod retroachievements;
pub mod savestates;
pub mod ui;

/// What a harness run asks of the core.
pub struct Boot<'a> {
    pub rom: &'a [u8],
    pub overclock: bool,
    pub disable_expansion_pak: bool,
    pub rng_seed: u64,
    pub rtc_timestamp: i64,
    /// a Gopher64 savestate file, loaded at the first screen update (as `--load-state` does)
    pub state: Option<std::path::PathBuf>,
    pub save_types: Option<Vec<ui::storage::SaveTypes>>,
}

/// Run the ROM through the tree's own `device::run_game` until the hook stops it.
pub fn run(boot: Boot, hook: lockstep_hook::Hook) {
    ui::set_boot_options(&boot);
    lockstep_hook::set(hook);
    let mut device = device::Device::new(true);
    let settings = ui::GameSettings {
        overclock: boot.overclock,
        disable_expansion_pak: boot.disable_expansion_pak,
        cheats: Default::default(),
        load_savestate_slot: boot.state.as_ref().map(|_| 0),
        rng_seed: Some(boot.rng_seed),
    };
    device::run_game(
        &mut device,
        boot.rom,
        settings,
        retroachievements::RAConfig::default(),
        None,
    );
}

/// A device with the RSP's tables and the CPU's decoder initialised, for unit tests.
pub fn unit_device() -> Box<device::Device> {
    let mut device = device::Device::new(false);
    device::rsp_interface::init(&mut device);
    device
}

/// Write the device as a Gopher64 savestate (the format `savestates::load_savestate` reads; no
/// RDP or RetroAchievements state, which the harness does not have).
pub fn write_state(device: &device::Device, path: &std::path::Path) -> std::io::Result<()> {
    let err = |e: &dyn std::fmt::Display| std::io::Error::other(e.to_string());
    let dev = postcard::to_stdvec(device).map_err(|e| err(&e))?;
    let saves = postcard::to_stdvec(&device.ui.storage.saves).map_err(|e| err(&e))?;
    let file = ui::storage::compress_file(&[
        (&dev, "device"),
        (&saves, "saves"),
        (&[], "rdp_state"),
        (&[], "ra_state"),
    ])
    .map_err(|e| err(&e))?;
    std::fs::write(path, file)
}
