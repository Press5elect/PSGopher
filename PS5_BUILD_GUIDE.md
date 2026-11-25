# PSGopher: build guide

How the title is built on an x86-64 Linux PC (Ubuntu 24.04 here), as it was built on 2026-11-24.

## What sits beside this repository

| Checkout | Used for |
| --- | --- |
| `../PS5_Vulkan` ([mihawk-99/PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)) | RADV's release archive, the link recipe (`tools/radv-link.sh`), the native tool, `libc.prx`, the compiler wrapper, the console tools |
| `../PS5_Mesa` | RADV's source, built by PS5_Vulkan at its pin |
| `../Crux2` ([Press5elect/Crux2](https://github.com/Press5elect/Crux2)) | RADV: its driver patches (`ps5/patches/mesa-27`) and the script that builds them (`ps5/tools/build-radv-crux2.sh`) |
| `../PS5_PayloadSDK` | the payload SDK fork and its platform layer, exported at `ps5/tools/setup-sdk.sh`'s pin |
| `../PS5_VulkanTemplate` (optional) | its `ps5/tools/bootstrap.sh` sets the three up; the PS5 layer in `ps5/host/platform*` comes from it |

## Host packages

```bash
apt-get install -y git cmake ninja-build clang-18 lld-18 llvm-18-dev libclang-18-dev libclang-cpp18-dev libclc-18-dev libllvmspirvlib-18-dev llvm-spirv-18 libclang-rt-18-dev glslang-tools rsync zstd curl make python3-pil python3-numpy
```

```bash
pip install meson mako
```

```bash
rustup toolchain install nightly --profile minimal --component rust-src
```

The LLVM/Clang/libclc/SPIR-V packages are for Mesa's `mesa_clc` host tool (RADV's build);
`libclang-rt-18-dev` gives clang's builtins (`__emutls_get_address`), which the link takes.

## Once: the stack

```bash
git clone https://github.com/mihawk-99/PS5_VulkanTemplate.git ../PS5_VulkanTemplate
```

```bash
(cd ../PS5_VulkanTemplate && ps5/tools/bootstrap.sh)
```

That clones PS5_Vulkan, PS5_Mesa and PS5_PayloadSDK beside it, builds PS5_Vulkan's SDK, the native
tool and `libc.prx`, and RADV's release archive (`tools/build-radv.sh release`, the long step).
It also asks for the console's address in PS5_Vulkan's `.env`, which only the console tools need.

Then RADV as Crux2 builds it (another long build; again after Crux2's Mesa patches change):

```bash
git clone https://github.com/Press5elect/Crux2.git ../Crux2
```

```bash
../Crux2/ps5/tools/build-radv-crux2.sh
```

It applies Crux2's patches to PS5_Vulkan's Mesa pin on a local `crux2-radv` branch of PS5_Mesa
(nothing is pushed) and leaves the archive in `../PS5_Vulkan/.deps/native/radv-crux2`, the one
PSGopher links. PS5_Vulkan's plain release archive is left as it was and is not used.

## The build

```bash
ps5/tools/build.sh
```

| Step | Script | Output |
| --- | --- | --- |
| sources current? | `ps5/tools/check-sources.sh` | stops if a pin is behind (`PSGOPHER_ALLOW_STALE=1` builds anyway) |
| the SDK at its pin | `ps5/tools/setup-sdk.sh` | `.deps/native/ps5-payload-sdk` |
| Gopher64 at its pin + patch | `ps5/tools/fetch-gopher64.sh` | `vendor/gopher64` |
| the core | `ps5/tools/build-core.sh` | `build/core/x86_64-sie-ps5/release/libgopher64.a` |
| libsmb2, libnfs, Dear ImGui at their pins | `ps5/tools/build-deps.sh` | `.deps/src`, `.deps/ports` |
| the sandbox exit's helper | `ps5/tools/build-elevator.sh` | `build/sandbox-elevator.elf` |
| the PS5 code | `build.sh` | `build/ps5/obj/*.o`, the list's font `build/ps5/gen/menu_font.h` |
| link, sign, lay out | `ps5/tools/link-title.sh` | `dist/PPSA99280/` (`eboot.bin`, `sce_sys/`, `sce_module/libc.prx`, `sandbox-elevator.elf`, `NOTICE.txt`), `build/ps5/imports.txt` |

`ps5/tools/build.sh --compile` stops before the link.

## Changing Gopher64

Edit `vendor/gopher64`, then:

```bash
ps5/tools/fetch-gopher64.sh --export
```

```bash
ps5/tools/fetch-gopher64.sh --check
```

The first writes `ps5/patches/gopher64/ps5.patch`, the second applies it to a fresh checkout at
the pin. Moving the pin: change `revision=` in `fetch-gopher64.sh`, fetch, resolve the patch,
export.

## Tests on the PC

```bash
CC=gcc CXX=g++ ps5/tests/run-host-tests.sh
```

Under AddressSanitizer and UBSan (gcc):

- `test_sdl3`: ps5/sdl3 against SDL's headers with the PS5 layer's host build: audio accounting and
  resampling (amplitude, spurious tones, images, frequency ratio, underruns), window and controller
  answers, events, timers, main-thread calls, text rendering.
- `test_library`: the game list's search (which files are games, depth, the title's own folders
  skipped, each file once, sorting, a share's listing and copy, the kept list), the settings file
  (defaults, round trip, values held in range) and the shares file (accounts encoded, mount names).
- `test_menu`: the list itself, drawn offscreen through the PC's Vulkan (lavapipe:
  `PSGOPHER_LAVAPIPE_ICD`, default `/opt/lavapipe/lvp_icd.json`) with Dear ImGui's Vulkan backend as on
  the console, a scripted pad through every screen; checks what the screens did, and leaves pictures
  in `build/host-tests/menu-pictures`. Needs a build first (`.deps`, `vendor/gopher64`, the font
  header).

## On the console

With PS5_Vulkan's `.env` set and the console idle (the `ps5-console` skill):

```bash
python3 ../PS5_Vulkan/tools/deploy-title-folder.py dist/PPSA99280
```

```bash
python3 ../PS5_Vulkan/tools/run-title.py PPSA99280 --until 'Gopher64 returned|probe: done|closing' --elf build/ps5/eboot.elf --echo '\[PSGopher\]|probe:|bench:|sandbox|menu:|library:|shares:' --timeout 600
```

Files the title reads, in `/data/psgopher/` (create them over FTP):

| File | Effect |
| --- | --- |
| `roms/<file>` | ROMs (`.z64 .n64 .v64`, `.zip`/`.7z` of one); in the game list, as is every ROM found in `/data`, on USB drives and shares |
| `autostart.txt` | first line: a ROM to boot before the list (a file in `roms/` or a full path) |
| `config/network.cfg` | the network shares (OGBox's format); the list writes it |
| `elevation-off.txt` (in the title folder) | stay in the sandbox (no USB drives); Settings > Storage writes it |
| `probe.txt` | run the console probe instead of a game, then exit (`probe:` lines) |
| `config/psgopher.ini` | the display (`display = 60`, `120` or `vrr`) and HDR10 (`hdr = on`, `hdr_white`, `hdr_peak`); the list's settings write it |
| `benchmark.txt` | benchmark mode: lines `rom <file>`, `seconds <n>`, `unlimited` (`bench:` lines, `bench-<time>.csv`) |

What Gopher64 writes there: `config/config.json` (the list's settings write it too), `data/saves/`,
`data/states/`, `cache/` (RADV's shader cache in `cache/radv`). The list keeps `cache/library.tsv`
and copies a share's ROM to `cache/play/`.

## Not done yet

- The game list does not set what Gopher64's desktop GUI sets with file pickers or editors: the
  Transfer Pak's Game Boy ROM and save, input profiles, cheats, RetroAchievements and netplay. They
  stay in `config.json`, `cheats.json` and `retroachievements.json` as Gopher64 keeps them.
- Nothing of the list, the shares or the sandbox exit has run on a console with this title ID.

- Release packaging: notices for every Rust crate and C library linked (OGBox's
  `rust-licences.py` approach), the ZIP and `SHA256SUMS` (`ps5-release` skill).
