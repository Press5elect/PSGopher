// PSGopher lockstep harness: the points where the harness sees the emulated machine.
//
//   Point::Vi       every screen update (once a VI field), from ui::video::check_callback
//   Point::RspTask  after every RSP burst, from the call run-lockstep.sh injects into the scratch
//                   copy of rsp_interface.rs's do_task (never into vendor/gopher64)
//
// The hook may set device.cpu.running = false to end the run.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSGopher by Press5elect"; see NOTICE.
use crate::device;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Point {
    Vi,
    RspTask,
}

pub type Hook = fn(&mut device::Device, Point);

thread_local! {
    static HOOK: std::cell::Cell<Option<Hook>> = const { std::cell::Cell::new(None) };
}

pub fn set(hook: Hook) {
    HOOK.with(|h| h.set(Some(hook)));
}

pub fn call(device: &mut device::Device, point: Point) {
    if let Some(hook) = HOOK.with(|h| h.get()) {
        hook(device, point)
    }
}

pub fn rsp_task(device: &mut device::Device, _timer: u64) {
    call(device, Point::RspTask)
}
