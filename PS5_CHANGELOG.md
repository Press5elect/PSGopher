# PSGopher changelog

## Unreleased

Nothing here has run on a console yet.

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
