# PSGopher changelog

## Unreleased

Games ran on no console yet; the list, the sandbox exit, an SMB share and USB drives did (first
console logs, 2026-11-25).

- Fixed (Gopher64, in `ps5.patch`): no game started: Tokio's whole runtime is refused by the console
  (`Operation not supported`, errno 45) and Gopher64 panicked. PSGopher builds it with the scheduler
  and timers only when that happens (all Gopher64 uses on the console), logs which of kqueue and an
  AF_UNIX socket pair was refused, and keeps the USB cartridge's TCP link off without it.
- Changed: a panic is written to the boot log with its message, place and thread.
- Added: the crash report (Crux2's): a fault writes its signal, registers and stack into the boot
  log before the console closes the title. A `video:` line says when a game's first frame is shown,
  then its frames every 10 s.
- Changed: the list's footer says how to leave a game (hold the touch pad + Options).
- Added: the in-game quick menu, the Live Board's LIVE panel: a touch pad tap pauses the game and
  shows Resume, Take a memory probe and Load a probe (save states in slots 1-5, with their times),
  Press reset and Eject cartridge (back to the list), with the live frame rate, the emulation
  thread's CPU time a frame, the present's wait and the audio queue. Drawn with Dear ImGui over the
  game's frame by parallel-rdp's own device (`ps5/host/quickmenu.cpp`; the overlay in
  `ps5.patch`); touch pad + a button stays Gopher64's hotkey.
- Fixed: Rust read the console's `stat`, `dirent` and `kevent` with FreeBSD 12's layouts (its std's
  libc is 12's; the console's is 11's), so every folder looked like no folder ("File exists" at
  Gopher64's start) and Tokio's poller got the wrong event size (likely its "Operation not
  supported"). Rust's calls of `stat`, `fstat`, `readdir` and `kevent` now go through converters
  (`ps5/host/rust_abi.c`), checked against the SDK's structures at compile time and against Rust's
  libc's own offsets by a host test.

- Added: the boot log, `psgopher-boot.log` in the title folder (the run before kept), as OGBox and
  Crux2; it has Gopher64's own messages and panics, which went nowhere before (Rust writes to
  descriptor 2, which klog's capture cannot move). A game that does not start says why in the list.

- Added: the game list: every N64 ROM on the console's `/data`, USB drives, extended storage and
  network shares, in tabs (Internal, USB, Network), kept between starts and searched again in the
  background; a game ends back in the list. Drawn with Dear ImGui on RADV.
- Added: settings in the list, kept as they change: Gopher64's own in its `config.json` (through
  Gopher64's `Config`), PSGopher's refresh and HDR10 in `psgopher.ini`. No text file to edit.
- Added: network shares (SMB and NFS) through Crux2's ps5vfs and OGBox's share list, added and
  edited in the list with an on-screen keyboard and a search for SMB servers.
- Added: USB drives and the whole of `/data`: the title leaves its sandbox at the start, as OGBox
  and Crux2 (the bundled `sandbox-elevator.elf` through the payload loader, port 9021);
  Settings > Storage turns it off.
- Added: `NOTICE` (the OGBox and Crux2 attributions kept, every other author), shipped as
  `NOTICE.txt`; Settings > About.
- Changed (Gopher64, in `ps5.patch`): `psgopher_setting_get/set`, Gopher64's settings by name for
  the list; HDR10 goes off as a game ends.

- Added: Gopher64 (`439e6c11f69d`, master of 2026-11-24) builds for the PS5 as a static library,
  with Rust's standard library rebuilt for the console's target (`ps5/rust/x86_64-sie-ps5.json`)
  and parallel-rdp, volk and rcheevos through the payload SDK's clang (fork `88d6e41`).
- Added: `ps5/sdl3`, the 88 SDL3/SDL3_ttf functions Gopher64 calls, on the PS5: the display through
  RADV's `VK_KHR_display`, up to four DualSense controllers, a 48 kHz audio stream with a
  windowed-sinc resampler, timers and event watches, text through stb_truetype. Host tests.
- Added: the title `PPSA99280` (provisional ID): klog, the splash, folders under `/data/psgopher`,
  the ROM from `autostart.txt` or `roms/`, the shell exit. Links through PS5_Vulkan's RADV recipe
  (RADV at PS5 Mesa `1ba9874`); every import resolves to a module a title loads.
- Added: 119.88 Hz, variable refresh and HDR10 output, through Crux2's RADV, chosen in
  `/data/psgopher/config/psgopher.ini` (`display`, `hdr`); off by default until tested on a console.
- Changed: RADV is Crux2's (PS5 Mesa `1ba9874` with Crux2's 17 driver patches, Mesa main 27.0-devel
  among them), built by Crux2's script; the build refuses PS5_Vulkan's plain archive.
- Added: the console probe (`probe.txt`) and the benchmark mode (`benchmark.txt`).
- Changed: the console icon is Gopher64's gopher logo on PSGopher's dark ground (`ps5/sce_sys/icon0.png`,
  made from Gopher64's `data/icon/gopher64.png`).
- Changed (Gopher64, in `ps5.patch`): `config.json` and the cheats file are written through a
  buffer: a `write()` costs about 3.3 ms on the console (PS5_PayloadSDK's PROBE.md).
