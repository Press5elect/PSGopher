// PSGopher lockstep harness: Gopher64's front end (src/ui*), replaced for a headless,
// deterministic run. The same file goes into the original's build and the candidate's.
//
// What it keeps of the real thing, because the emulated machine depends on it:
//   - storage: the save buffers, save-type choice (from the ROM header or --save-types; the
//     front end's per-game table is not copied), schedule_save's event, savestate loading
//   - video: the RDP timing model of parallel-rdp/interface.cpp (rdp_process_commands): the
//     command fetch from RDRAM or DMEM, the busy-until clock and the SyncFull interrupt timer, so
//     DP interrupts arrive as in the real build. Nothing is rendered: the RDP writes nothing to
//     RDRAM (no framebuffer, no depth buffer), and rdp_check_rdram waits for nothing.
// What it drops: the window, SDL, audio output, controllers (no buttons pressed), USB, netplay,
// the speed limiter (off) and every wall-clock path (the RTC is fixed, --rtc).
//
// SPDX-License-Identifier: GPL-3.0-or-later
use crate::{device, savestates};

#[derive(Default, Clone)]
pub struct Dirs {
    pub config_dir: std::path::PathBuf,
    pub data_dir: std::path::PathBuf,
    pub cache_dir: std::path::PathBuf,
}

#[derive(Default)]
pub struct Audio {}

#[derive(Default)]
pub struct Input {}

#[derive(Default)]
pub struct Storage {
    pub save_type: Vec<storage::SaveTypes>,
    pub paths: storage::Paths,
    pub saves: storage::Saves,
    pub save_state_slot: u32,
}

#[derive(Default)]
pub struct Video {
    pub fullscreen: bool,
    pub widescreen: bool,
    pub fps_tx: Option<tokio::sync::mpsc::UnboundedSender<bool>>,
    pub fps_rx: Option<tokio::sync::mpsc::UnboundedReceiver<bool>>,
    pub vis_tx: Option<tokio::sync::mpsc::UnboundedSender<bool>>,
    pub vis_rx: Option<tokio::sync::mpsc::UnboundedReceiver<bool>>,
}

#[derive(Default)]
pub struct Usb {
    pub usb_tx: Option<tokio::sync::mpsc::UnboundedSender<usb::UsbData>>,
    pub cart_rx: Option<tokio::sync::mpsc::UnboundedReceiver<usb::UsbData>>,
}

#[derive(Clone)]
pub struct GameSettings {
    pub overclock: bool,
    pub disable_expansion_pak: bool,
    pub cheats: rustc_hash::FxHashMap<String, Option<String>>,
    pub load_savestate_slot: Option<u32>,
    pub rng_seed: Option<u64>,
}

pub struct SpeedLimiter {
    pub limit_freq: u64,
    pub limit_freq_check: std::time::Instant,
    pub frame_counter: u64,
    pub enabled: bool,
    pub min_wait_time: std::time::Duration,
    pub next_pace_deadline: Option<std::time::Instant>,
}

impl Default for SpeedLimiter {
    fn default() -> Self {
        Self {
            limit_freq: 2,
            limit_freq_check: std::time::Instant::now(),
            frame_counter: 0,
            enabled: false, // the harness runs as fast as it can
            min_wait_time: std::time::Duration::from_secs(1),
            next_pace_deadline: None,
        }
    }
}

#[derive(Default)]
pub struct Ui {
    pub dirs: Dirs,
    pub config: config::Config,
    pub game_id: String,
    pub game_hash: String,
    pub audio: Audio,
    pub input: Input,
    pub storage: Storage,
    pub video: Video,
    pub usb: Usb,
    pub speed_limiter: SpeedLimiter,
    pub savestate: savestates::Savestate,
}

impl Ui {
    pub fn new() -> Ui {
        Ui::default()
    }
}

struct BootOptions {
    rtc_timestamp: i64,
    state: Option<std::path::PathBuf>,
    save_types: Option<Vec<storage::SaveTypes>>,
}

thread_local! {
    static BOOT: std::cell::RefCell<BootOptions> = const {
        std::cell::RefCell::new(BootOptions { rtc_timestamp: 0, state: None, save_types: None })
    };
}

pub fn set_boot_options(boot: &crate::Boot) {
    BOOT.with(|b| {
        *b.borrow_mut() = BootOptions {
            rtc_timestamp: boot.rtc_timestamp,
            state: boot.state.clone(),
            save_types: boot.save_types.clone(),
        }
    });
}

pub mod config {
    #[derive(Clone)]
    pub struct Input {
        pub controller_enabled: [bool; 4],
        pub emulate_vru: bool,
        pub transfer_pak: [bool; 4],
    }
    #[derive(Clone, Default)]
    pub struct Emulation {
        pub overclock: bool,
        pub disable_expansion_pak: bool,
        pub rewind: bool,
        pub usb: bool,
    }
    #[derive(Clone)]
    pub struct Config {
        pub input: Input,
        pub emulation: Emulation,
    }
    impl Default for Config {
        fn default() -> Self {
            Config {
                input: Input {
                    controller_enabled: [true, false, false, false],
                    emulate_vru: false,
                    transfer_pak: [false; 4],
                },
                emulation: Emulation::default(),
            }
        }
    }
}

pub mod storage {
    use crate::{device, netplay, ui};
    use std::io::{Read, Write};

    #[derive(PartialEq, Clone, Copy, Debug)]
    pub enum SaveTypes {
        Eeprom4k,
        Eeprom16k,
        Sram,
        Sram768k,
        Sram1m,
        Flash,
        Mempak,
        Sdcard,
        Romsave,
    }

    #[derive(Default)]
    pub struct Paths {
        pub eep_file_path: std::path::PathBuf,
        pub sra_file_path: std::path::PathBuf,
        pub fla_file_path: std::path::PathBuf,
        pub pak_file_path: std::path::PathBuf,
        pub sdcard_file_path: std::path::PathBuf,
        pub romsave_file_path: std::path::PathBuf,
        pub savestate_file_path: std::path::PathBuf,
    }

    #[derive(Default, Clone, serde::Serialize, serde::Deserialize)]
    pub struct Save {
        pub data: Vec<u8>,
        pub write_pending: bool,
    }

    #[derive(Default, Clone, serde::Serialize, serde::Deserialize)]
    pub struct RomSave {
        #[serde(serialize_with = "crate::savestates::serialize_sorted_map")]
        pub data: rustc_hash::FxHashMap<u32, u8>,
        pub write_pending: bool,
    }

    #[derive(Default, Clone, serde::Serialize, serde::Deserialize)]
    pub struct Saves {
        pub eeprom: Save,
        pub sram: Save,
        pub flash: Save,
        pub mempak: Save,
        pub sdcard: Save,
        pub romsave: RomSave,
        pub write_to_disk: bool,
    }

    impl Saves {
        pub fn copy_from(&mut self, other: &Saves) {
            *self = other.clone();
        }
    }

    pub fn parse_save_type(name: &str) -> Option<SaveTypes> {
        Some(match name {
            "eeprom4k" => SaveTypes::Eeprom4k,
            "eeprom16k" => SaveTypes::Eeprom16k,
            "sram" => SaveTypes::Sram,
            "sram768k" => SaveTypes::Sram768k,
            "sram1m" => SaveTypes::Sram1m,
            "flash" => SaveTypes::Flash,
            _ => return None,
        })
    }

    // The ROM header's save type (the "ED" convention, as Gopher64's get_save_type), else
    // EEPROM 4K + SRAM, Gopher64's default; its per-game table is not copied here.
    fn header_save_type(rom: &[u8]) -> Vec<SaveTypes> {
        if rom.len() > 0x3F && &rom[0x3C..0x3E] == b"ED" {
            return match rom[0x3F] >> 4 {
                1 => vec![SaveTypes::Eeprom4k],
                2 => vec![SaveTypes::Eeprom16k],
                3 => vec![SaveTypes::Sram],
                4 => vec![SaveTypes::Sram768k],
                5 => vec![SaveTypes::Flash],
                6 => vec![SaveTypes::Sram1m],
                _ => vec![],
            };
        }
        vec![SaveTypes::Eeprom4k, SaveTypes::Sram]
    }

    pub fn get_game_crc(rom: &[u8]) -> String {
        format!(
            "{:08X}-{:08X}-C:{:02X}",
            u32::from_be_bytes(rom[0x10..0x14].try_into().unwrap()),
            u32::from_be_bytes(rom[0x14..0x18].try_into().unwrap()),
            rom[0x3E]
        )
    }

    pub fn init(ui: &mut ui::Ui, rom: &[u8]) {
        let (types, state) = super::BOOT.with(|b| {
            let b = b.borrow();
            (b.save_types.clone(), b.state.clone())
        });
        ui.storage.save_type = types.unwrap_or_else(|| header_save_type(rom));
        if let Some(state) = state {
            ui.storage.paths.savestate_file_path = state;
        }
    }

    // no save files are read: every run starts from blank saves
    pub fn load_saves(_ui: &mut ui::Ui, _netplay: &mut Option<netplay::Netplay>, _cic_seed: u8) {}

    pub fn format_saves(device: &mut device::Device) {
        device::cart::format_eeprom(device);
        device::cart::sram::format_sram(device);
        device::cart::sram::format_flash(device);
        device::controller::mempak::format_mempak(device);
        // run_game set the RTC from the wall clock just before this: fix it
        device.cart.rtc_timestamp = super::BOOT.with(|b| b.borrow().rtc_timestamp);
    }

    pub fn write_saves(_device: &mut device::Device) {}

    pub fn save_event(device: &mut device::Device) {
        write_saves(device);
    }

    pub fn schedule_save(device: &mut device::Device, save_type: SaveTypes) {
        match save_type {
            SaveTypes::Eeprom4k | SaveTypes::Eeprom16k => {
                device.ui.storage.saves.eeprom.write_pending = true;
            }
            SaveTypes::Sram | SaveTypes::Sram768k | SaveTypes::Sram1m => {
                device.ui.storage.saves.sram.write_pending = true;
            }
            SaveTypes::Flash => {
                device.ui.storage.saves.flash.write_pending = true;
            }
            SaveTypes::Mempak => {
                device.ui.storage.saves.mempak.write_pending = true;
            }
            SaveTypes::Sdcard => {
                device.ui.storage.saves.sdcard.write_pending = true;
            }
            SaveTypes::Romsave => {
                if !device::cart::rom::romsave_persists(device.cart.cic_seed) {
                    return;
                }
                device.ui.storage.saves.romsave.write_pending = true;
            }
        }
        device::events::create_event(
            device,
            device::events::EVENT_TYPE_SAVE,
            device.cpu.clock_rate, // 1 second
        );
    }

    pub fn decompress_file(input: &[u8], name: &str) -> Result<Vec<u8>, zip::result::ZipError> {
        let mut out = Vec::new();
        if let Ok(mut reader) = zip::ZipArchive::new(std::io::Cursor::new(input))
            && let Ok(mut file) = reader.by_name(name)
            && file.read_to_end(&mut out).is_ok()
        {
            return Ok(out);
        }
        Err(zip::result::ZipError::FileNotFound)
    }

    pub fn compress_file(data: &[(&[u8], &str)]) -> Result<Vec<u8>, zip::result::ZipError> {
        let mut out = Vec::new();
        {
            let mut writer = zip::ZipWriter::new(std::io::Cursor::new(&mut out));
            for item in data {
                if writer
                    .start_file(item.1, zip::write::SimpleFileOptions::default())
                    .is_err()
                    || writer.write_all(item.0).is_err()
                {
                    return Err(zip::result::ZipError::FileNotFound);
                }
            }
        }
        Ok(out)
    }
}

pub mod video {
    use crate::{device, ui};

    pub type MESSAGE_LENGTH = u32;
    pub const MESSAGE_LENGTH_MESSAGE_VERY_SHORT: MESSAGE_LENGTH = 0;
    pub const MESSAGE_LENGTH_MESSAGE_SHORT: MESSAGE_LENGTH = 1;
    pub const MESSAGE_LENGTH_MESSAGE_LONG: MESSAGE_LENGTH = 2;

    pub fn init(device: &mut device::Device, _netplay: bool) {
        let (fps_tx, fps_rx) = tokio::sync::mpsc::unbounded_channel();
        let (vis_tx, vis_rx) = tokio::sync::mpsc::unbounded_channel();
        // receivers dropped: the senders' errors are ignored by the callers
        drop((fps_rx, vis_rx));
        device.ui.video.fps_tx = Some(fps_tx);
        device.ui.video.vis_tx = Some(vis_tx);
        super::rdp_model::init(device);
    }
    pub fn close(_ui: &ui::Ui) {}
    pub fn update_screen() {}
    pub fn render_frame() {}
    pub fn state_size() -> usize {
        0
    }
    pub fn save_state(_rdp_state: *mut u8) {}
    pub fn idle() {}
    pub fn load_state(_device: &mut device::Device, _rdp_state: *const u8, _len: usize) {}
    pub fn pause_loop(_ui: &mut ui::Ui, _frame_time: f64) {}

    /// Once a screen update: the harness's VI checkpoint. Nothing is paused or toggled.
    pub fn check_callback(device: &mut device::Device) -> (bool, bool) {
        crate::lockstep_hook::call(device, crate::lockstep_hook::Point::Vi);
        (false, false)
    }
    pub fn set_register(_reg: u32, _value: u32) {}
    pub fn process_rdp_list(now: u64) -> u64 {
        super::rdp_model::process_commands(now)
    }
    pub fn translate_rdp_busy_until(old_count: u64, new_count: u64) {
        super::rdp_model::translate_busy_until(old_count, new_count)
    }
    pub fn check_rdram_read(_address: u32, _length: u32) {}
    pub fn check_rdram_write(_address: u32, _length: u32) {}
    pub fn onscreen_message(message: &str, _milliseconds: MESSAGE_LENGTH) {
        if std::env::var_os("LOCKSTEP_VERBOSE").is_some() {
            eprintln!("lockstep: core message: {message}");
        }
    }
}

// parallel-rdp/interface.cpp's rdp_process_commands, timing only (no rendering)
mod rdp_model {
    use crate::device;

    const CMD_LEN_LUT: [usize; 64] = [
        1, 1, 1, 1, 1, 1, 1, 1, 4, 6, 12, 14, 12, 14, 20, 22, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1,
    ];
    const PRIMITIVE_SETUP_CLOCKS: u64 = 16;
    const SCANLINE_CLOCKS: u64 = 16;
    const SYNC_CLOCKS: u64 = 50;
    const DP_STATUS_XBUS_DMA: u32 = 1;

    struct State {
        dpc: *mut u32,
        dmem: *const u8,
        rdram: *const u8,
        rdram_size: usize,
        busy_until: u64,
        cmd_data: Vec<u32>,
        cmd_ptr: i64,
        cmd_cur: i64,
        cycle_type: u32,
        fb_pixel_size: u32,
        tex_pixel_size: u32,
        scissor_xh: i32,
        scissor_yh: i32,
        scissor_xl: i32,
        scissor_yl: i32,
        scissor_field: bool,
        scissor_keep_odd: bool,
    }

    thread_local! {
        static STATE: std::cell::RefCell<Option<State>> = const { std::cell::RefCell::new(None) };
    }

    pub fn init(device: &mut device::Device) {
        let state = State {
            dpc: device.rdp.regs_dpc.as_mut_ptr(),
            dmem: device.rsp.mem.as_ptr(),
            rdram: device.rdram.mem.as_ptr(),
            rdram_size: device.rdram.mem.len(),
            busy_until: 0,
            cmd_data: vec![0; 0x40000 >> 2],
            cmd_ptr: 0,
            cmd_cur: 0,
            cycle_type: 0,
            fb_pixel_size: 0,
            tex_pixel_size: 0,
            scissor_xh: 0,
            scissor_yh: 0,
            scissor_xl: 0,
            scissor_yl: 0,
            scissor_field: false,
            scissor_keep_odd: false,
        };
        STATE.with(|s| *s.borrow_mut() = Some(state));
    }

    pub fn translate_busy_until(old_count: u64, new_count: u64) {
        STATE.with(|s| {
            if let Some(st) = s.borrow_mut().as_mut() {
                let remaining = st.busy_until.saturating_sub(old_count);
                st.busy_until = new_count + remaining;
            }
        })
    }

    impl State {
        fn add_busy(&mut self, clocks: u64) {
            self.busy_until += clocks * 3 / 2;
        }
        fn pixel_clocks(&self, pixels: u64) -> u64 {
            match self.cycle_type {
                0 => pixels,
                1 => pixels * 2,
                _ => {
                    let bits = 4u64 << self.fb_pixel_size;
                    (pixels * bits + 63) / 64
                }
            }
        }
        fn scissor_rows(&self, top: i32, bottom: i32) -> u64 {
            if bottom <= top {
                return 0;
            }
            if !self.scissor_field {
                return (bottom - top) as u64;
            }
            let p = self.scissor_keep_odd as i32;
            let below = |y: i32| (y - p + 1) >> 1;
            (below(bottom) - below(top)) as u64
        }
        fn clipped_area(&self, xh: i32, yh: i32, xl: i32, yl: i32) -> u64 {
            let xh = (xh >> 2).max(self.scissor_xh);
            let yh = (yh >> 2).max(self.scissor_yh);
            let xl = (xl >> 2).min(self.scissor_xl);
            let yl = (yl >> 2).min(self.scissor_yl);
            if xl <= xh {
                return 0;
            }
            (xl - xh) as u64 * self.scissor_rows(yh, yl)
        }
        fn rectangle_pixels(&self, w1: u32, w2: u32) -> u64 {
            let (mut xl, mut yl) = (((w1 >> 12) & 0xFFF) as i32, (w1 & 0xFFF) as i32);
            let (xh, yh) = (((w2 >> 12) & 0xFFF) as i32, (w2 & 0xFFF) as i32);
            if self.cycle_type >= 2 {
                xl += 4;
                yl += 4;
            }
            self.clipped_area(xh, yh, xl, yl)
        }
        fn rectangle_rows(&self, w1: u32, w2: u32) -> u64 {
            let mut yl = (w1 & 0xFFF) as i32;
            let yh = (w2 & 0xFFF) as i32;
            if self.cycle_type >= 2 {
                yl += 4;
            }
            self.scissor_rows((yh >> 2).max(self.scissor_yh), (yl >> 2).min(self.scissor_yl))
        }
        fn triangle_pixels(&self, at: usize) -> u64 {
            let cmd = &self.cmd_data[at..];
            let sx = |v: u32| ((v << 18) as i32) >> 18;
            let yl = sx(cmd[0]);
            let ym = sx(cmd[1] >> 16);
            let yh = sx(cmd[1]);
            let xl = cmd[2] as i32;
            let xh = cmd[4] as i32;
            let dxhdy = cmd[5] as i32;
            let major_x_at_ym = xh as i64 + dxhdy as i64 * (ym - yh) as i64 / 4;
            let mut width = (major_x_at_ym - xl as i64).abs() >> 16;
            width = width.min((self.scissor_xl - self.scissor_xh).max(0) as i64);
            let top = (yh >> 2).max(self.scissor_yh);
            let bottom = (yl >> 2).min(self.scissor_yl);
            width as u64 * self.scissor_rows(top, bottom) / 2
        }
    }

    pub fn process_commands(now: u64) -> u64 {
        STATE.with(|s| {
            let mut guard = s.borrow_mut();
            let st = guard.as_mut().expect("rdp model not initialised");
            process(st, now)
        })
    }

    fn process(st: &mut State, now: u64) -> u64 {
        let mut interrupt_timer = 0;
        if st.busy_until < now {
            st.busy_until = now;
        }
        // (pointers into the device, as parallel-rdp's gfx_info holds them)
        let (dpc_ptr, dmem, rdram) = (st.dpc, st.dmem, st.rdram);
        let dpc = |i: usize| unsafe { dpc_ptr.add(i).read() };
        let current = dpc(device::rdp::DPC_CURRENT_REG) & 0x00FF_FFF8;
        let end = dpc(device::rdp::DPC_END_REG) & 0x00FF_FFF8;
        let mut length = end as i64 - current as i64;
        if length <= 0 {
            return interrupt_timer;
        }
        length >>= 3;
        if (st.cmd_ptr + length) & !(0x0003_FFFF >> 3) != 0 {
            return interrupt_timer;
        }
        let mut offset = current;
        if dpc(device::rdp::DPC_STATUS_REG) & DP_STATUS_XBUS_DMA != 0 {
            while length > 0 {
                offset &= 0xFF8;
                let o = offset as usize;
                let rd = |o: usize| unsafe {
                    u32::from_be_bytes(std::ptr::read(dmem.add(o) as *const [u8; 4]))
                };
                let (a, b) = (rd(o), rd(o + 4));
                st.cmd_data[2 * st.cmd_ptr as usize] = a;
                st.cmd_data[2 * st.cmd_ptr as usize + 1] = b;
                offset += 8;
                st.cmd_ptr += 1;
                length -= 1;
            }
        } else {
            if end > 0x7ff_ffff || current > 0x7ff_ffff {
                return interrupt_timer;
            }
            while length > 0 {
                offset &= (st.rdram_size as u32 - 1) & !7;
                let o = offset as usize;
                let rd = |o: usize| unsafe {
                    u32::from_ne_bytes(std::ptr::read(rdram.add(o) as *const [u8; 4]))
                };
                let (a, b) = (rd(o), rd(o + 4));
                st.cmd_data[2 * st.cmd_ptr as usize] = a;
                st.cmd_data[2 * st.cmd_ptr as usize + 1] = b;
                offset += 8;
                st.cmd_ptr += 1;
                length -= 1;
            }
        }

        while st.cmd_cur - st.cmd_ptr < 0 {
            let at = 2 * st.cmd_cur as usize;
            let (w1, w2) = (st.cmd_data[at], st.cmd_data[at + 1]);
            let command = (w1 >> 24) & 63;
            let cmd_length = CMD_LEN_LUT[command as usize] as i64;
            if st.cmd_ptr - st.cmd_cur - cmd_length < 0 {
                unsafe {
                    let e = st.dpc.add(device::rdp::DPC_END_REG).read();
                    st.dpc.add(device::rdp::DPC_START_REG).write(e);
                    st.dpc.add(device::rdp::DPC_CURRENT_REG).write(e);
                }
                return interrupt_timer;
            }
            if (0x26..=0x29).contains(&command) {
                st.add_busy(SYNC_CLOCKS);
            } else if (0x08..=0x0F).contains(&command) {
                let c = PRIMITIVE_SETUP_CLOCKS + st.pixel_clocks(st.triangle_pixels(at));
                st.add_busy(c);
            } else if command == 0x24 || command == 0x25 {
                let c = PRIMITIVE_SETUP_CLOCKS
                    + st.pixel_clocks(st.rectangle_pixels(w1, w2))
                    + st.rectangle_rows(w1, w2) * SCANLINE_CLOCKS;
                st.add_busy(c);
            } else if command == 0x36 {
                let c = PRIMITIVE_SETUP_CLOCKS + st.pixel_clocks(st.rectangle_pixels(w1, w2));
                st.add_busy(c);
            } else if command == 0x30 {
                let sl = ((w1 >> 12) & 0xFFF) >> 2;
                let sh = ((w2 >> 12) & 0xFFF) >> 2;
                let entries = if sh >= sl { (sh - sl + 1) as u64 } else { 0 };
                st.add_busy(PRIMITIVE_SETUP_CLOCKS + SCANLINE_CLOCKS + entries);
            } else if command == 0x33 || command == 0x34 {
                let (sl, tl) = ((w1 >> 12) & 0xFFF, w1 & 0xFFF);
                let (sh, th) = ((w2 >> 12) & 0xFFF, w2 & 0xFFF);
                let texels = if command == 0x33 {
                    if sh >= sl { (sh - sl + 1) as u64 } else { 0 }
                } else {
                    (if sh >= sl { (((sh - sl) >> 2) + 1) as u64 } else { 0 })
                        * (if th >= tl { (((th - tl) >> 2) + 1) as u64 } else { 0 })
                };
                let rows = if command == 0x33 {
                    1
                } else if th >= tl {
                    (((th - tl) >> 2) + 1) as u64
                } else {
                    0
                };
                let bits = 4u64 << st.tex_pixel_size;
                st.add_busy(PRIMITIVE_SETUP_CLOCKS + (texels * bits + 63) / 64 + rows * SCANLINE_CLOCKS);
            }
            match command {
                0x3F => st.fb_pixel_size = (w1 >> 19) & 3, // SetColorImage
                0x3D => st.tex_pixel_size = (w1 >> 19) & 3, // SetTextureImage
                0x2D => {
                    // SetScissor
                    st.scissor_xh = (((w1 >> 12) & 0xFFF) >> 2) as i32;
                    st.scissor_yh = ((w1 & 0xFFF) >> 2) as i32;
                    st.scissor_xl = (((w2 >> 12) & 0xFFF) >> 2) as i32;
                    st.scissor_yl = ((w2 & 0xFFF) >> 2) as i32;
                    st.scissor_field = (w2 >> 25) & 1 != 0;
                    st.scissor_keep_odd = (w2 >> 24) & 1 != 0;
                }
                0x2F => st.cycle_type = (w1 >> 20) & 3, // SetOtherModes
                0x29 => interrupt_timer = st.busy_until - now, // SyncFull
                _ => {}
            }
            st.cmd_cur += cmd_length;
        }
        st.cmd_ptr = 0;
        st.cmd_cur = 0;
        unsafe {
            let e = st.dpc.add(device::rdp::DPC_END_REG).read();
            st.dpc.add(device::rdp::DPC_CURRENT_REG).write(e);
        }
        interrupt_timer
    }
}

pub mod audio {
    use crate::{device, ui};
    pub fn init(_device: &mut device::Device) {}
    pub fn update_freq(_device: &mut device::Device) {}
    pub fn reset_game_audio(_device: &mut device::Device) {}
    pub fn close(_ui: &mut ui::Ui) {}
    pub fn lower_audio_volume(_ui: &mut ui::Ui) {}
    pub fn raise_audio_volume(_ui: &mut ui::Ui) {}
    pub fn play_audio(_device: &mut device::Device, _dram_addr: usize, _length: u64) {}
}

pub mod input {
    use crate::{device, ui};

    #[derive(Default, PartialEq, Copy, Clone, serde::Serialize, serde::Deserialize)]
    pub struct InputData {
        pub data: u32,
        pub pak_change_pressed: bool,
    }

    pub fn init(_device: &mut device::Device, _netplay: bool) {}
    pub fn close(_ui: &mut ui::Ui) {}
    pub fn set_rumble(_ui: &ui::Ui, _channel: usize, _rumble: u8) {}
    /// no buttons pressed, sticks centred
    pub fn get(_ui: &mut ui::Ui, _channel: usize) -> InputData {
        InputData::default()
    }
}

pub mod input_profile {
    pub const R_DPAD: usize = 0;
    pub const L_DPAD: usize = 1;
    pub const D_DPAD: usize = 2;
    pub const U_DPAD: usize = 3;
    pub const START_BUTTON: usize = 4;
    pub const Z_TRIG: usize = 5;
    pub const B_BUTTON: usize = 6;
    pub const A_BUTTON: usize = 7;
    pub const R_CBUTTON: usize = 8;
    pub const L_CBUTTON: usize = 9;
    pub const D_CBUTTON: usize = 10;
    pub const U_CBUTTON: usize = 11;
    pub const R_TRIG: usize = 12;
    pub const L_TRIG: usize = 13;
    pub const AXIS_RIGHT: usize = 14;
    pub const AXIS_LEFT: usize = 15;
    pub const AXIS_DOWN: usize = 16;
    pub const AXIS_UP: usize = 17;
    pub const HOTKEY: usize = 18;
    pub const PROFILE_SIZE: usize = 19;
}

pub mod usb {
    #[derive(Clone, Debug)]
    pub struct UsbData {
        pub data: Vec<u8>,
        pub data_type: u32,
        pub data_size: u32,
    }
    pub fn send_to_usb(_usb_tx: &tokio::sync::mpsc::UnboundedSender<UsbData>, _buffer: UsbData) {}
}
