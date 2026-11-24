# PSGopher changelog

## Unreleased

Nothing here has run on a console yet.

- Added: Gopher64 (`439e6c11f69d`, master of 2026-11-24) builds for the PS5 as a static library,
  with Rust's standard library rebuilt for the console's target (`ps5/rust/x86_64-sie-ps5.json`)
  and parallel-rdp, volk and rcheevos through the payload SDK's clang (fork `88d6e41`).
- Added: `ps5/sdl3`, the 88 SDL3/SDL3_ttf functions Gopher64 calls, on the PS5: the display through
  RADV's `VK_KHR_display`, up to four DualSense controllers, a 48 kHz audio stream with a
  windowed-sinc resampler, timers and event watches, text through stb_truetype. Host tests.
- Added: the title `PPSA99280` (provisional ID): klog, the splash, folders under `/data/psgopher`,
  the ROM from `autostart.txt` or `roms/`, the shell exit. Links through PS5_Vulkan's RADV recipe
  (RADV at PS5 Mesa `1ba9874`); every import resolves to a module a title loads.
- Added: the console probe (`probe.txt`) and the benchmark mode (`benchmark.txt`).
- Changed (Gopher64, in `ps5.patch`): `config.json` and the cheats file are written through a
  buffer: a `write()` costs about 3.3 ms on the console (PS5_PayloadSDK's PROBE.md).
