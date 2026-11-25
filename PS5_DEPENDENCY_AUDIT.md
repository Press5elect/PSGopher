# PSGopher: dependency and platform audit

Every dependency of Gopher64 at the pin (`439e6c11f69d`), what it needs from the platform, and
what happens to it on the PS5. "Compiles" means [here]: it built for `x86_64-sie-ps5` on
2026-11-24 with Rust nightly 2026-11-23 and the payload SDK fork `88d6e41`. Nothing in this table
has run on a console.

## Submodules

| Submodule | Revision | Licence | PS5 |
| --- | --- | --- | --- |
| `parallel-rdp/parallel-rdp-standalone` | `1c1014c8` (Gopher64's `gopher64` branch) | MIT | compiles; Vulkan through volk; runs on this RADV in another title [console: PS5_Mupen64Plus]. Changes needed: none in the submodule |
| `retroachievements/rcheevos` | `14331732` (v12.4.0+16) | MIT | compiles; network use not started (D10) |
| `src/compat/sse2neon` | `b26afbb4` | MIT | AArch64 only; not built |
| `android-project/libadrenotools` | `1c49a786` | BSD-2 | Android only; not built |

## C and C++ built by `build.rs`

| Library | PS5 |
| --- | --- |
| volk | compiles. Its `dlopen` path is never taken: parallel-rdp is given a `vkGetInstanceProcAddr` (`SDL_Vulkan_GetVkGetInstanceProcAddr` = RADV's `vk_icdGetInstanceProcAddr`) |
| parallel-rdp + Granite's Vulkan layer + `interface.cpp` | compiles with `-march=x86-64-v3 -flto=thin` (upstream's flags; Zen 2 code runs [console: Crux2]) |
| rcheevos + `retroachievements.c` | compiles |

## The title's own libraries (the game list and the shares)

Fetched and built by `ps5/tools/build-deps.sh` into `.deps/` (each at its master, as the owner's rule
asks; `check-sources.sh` says when one moved on), linked into the title.

| Library | Revision | Licence | PS5 |
| --- | --- | --- | --- |
| libsmb2 | `5fdd128` (master of 2026-11-25) | LGPL-2.1-or-later | SMB2/3 shares for ps5vfs; built with the SDK's CMake toolchain, no patches (Crux2 and OGBox link its 6.2 tag the same way [console: both]) |
| libnfs | `3fb4f2a` (master of 2026-11-25) | LGPL-2.1-or-later, BSD | NFS shares for ps5vfs; Crux2's flags: no coarse clock, no portmapper scan, `getservbyport`/`getuid`/`getgid` answered by ps5vfs (Crux2 pins `7613236` [console: Crux2]) |
| Dear ImGui | `667c019` (master of 2026-11-25, 1.93.1 WIP) | MIT | the game list, with its Vulkan backend on volk; `IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS` (its default link opener calls `fork`, which no module a title loads exports) |
| Roboto Medium | Dear ImGui's `misc/fonts` | Apache-2.0 | the list's font, made into a header at build time |

The network shares also need what the console's libc lacks or refuses, as in Crux2 and OGBox
(`ps5/host/libc_gaps.c`, their `ps5_libc.c`): `getaddrinfo`/`freeaddrinfo` through the console's
resolver (libSceNet) in place of the platform layer's, which refuses every lookup, even of a
numeric address, which libsmb2 and libnfs make; `getprotobyname`; and three wraps, `lstat` (stat's
answer where the sandbox refuses lstat), `poll` (restarted after a signal) and `setsockopt` (large
TCP buffers before a share connects: OGBox measured 18 MB/s without them). `link-title.sh` binds
and wraps them, kept local.

## Crates

| Crate (as used) | Platform needs | PS5 status |
| --- | --- | --- |
| `std` | libc, pthreads, `_umtx_op` (FreeBSD's futex for `Mutex`/`Condvar`), `kevent`, `sysctl`, emulated TLS | rebuilt with `-Zbuild-std`; runs in OGBox [console]; `_umtx_op`, `kevent` have console stubs; [unknown] whether every std path Gopher64 reaches behaves (first console run) |
| `sdl3-sys` 0.7.3 (SDL 3.4.20), `sdl3-ttf-sys` 0.7.1 | SDL3, SDL3_ttf | bindings only (`no-link`); functions from `ps5/sdl3` (D2) |
| `slint`, `rfd`, `open`, `winit`/`wgpu` (via slint) | window system, GPU through wgpu | not built (`--no-default-features`, PS5 target table) |
| `mimalloc` | its own `mmap` heap | not built on the PS5 (would bypass the platform heap in direct memory) |
| `tokio` (rt-multi-thread, fs, process, macros) | threads, `kqueue` (mio), signals | compiles; used for file writes and timers. kqueue differs from FreeBSD: re-armed `EVFILT_USER` with `EV_ADD|NOTE_TRIGGER` does not wake [console: PROBE.md] [unknown] whether Tokio's waker path hits that: watch the first run's save writes |
| `spin_sleep` | `nanosleep`, spin | compiles; pacing accuracy [unknown]: measured by the benchmark mode |
| `serde`, `serde_json`, `postcard`, `serde-big-array` | none | compiles |
| `zip` 9 (deflate, deflate64, bzip2, lzma, ppmd, zstd) | `zstd-sys` C | compiles (zstd through the SDK's clang) |
| `sevenz-rust2` | none | compiles |
| `sha2`, `rand` 0.10, `rustc-hash`, `regex`, `encoding_rs`, `fatfs`, `time` | `getrandom` (rand's `SysRng`), `localtime_r` | compiles; `getrandom` has no console stub: PSGopher defines it over the platform's `arc4random_buf` |
| `clap` | none | compiles; `psgopher_main` passes its argv |
| `dirs` | `$HOME`, `getpwuid_r` | compiles; bypassed: folders are fixed (D8) |
| `reqwest`, `rustls`, `ring`, `tokio-tungstenite`, `hyper`, `h2` | sockets, TLS | compiles (`ring`'s C/asm through the SDK); never started (D10) |
| `matchbox_socket`, `ggrs`, webrtc stack | UDP, WebRTC | compiles; never started (D10) |
| `discord-rich-presence` | Unix socket to Discord | compiles; never started |
| `jni`, `webpki-roots` | Android | not built |
| `objc2*` | macOS | not built |

## The core's imports

`llvm-nm -u` of `libgopher64.a` less what it defines [here]: 258 SDL3/SDL3_ttf names, the C++
runtime and unwinder (libc++abi, libunwind from the SDK), and libc/kernel names. Against the SDK's
link stubs, 7 have none:

| Name | Who asks | Resolution |
| --- | --- | --- |
| `__assert` | rcheevos/C asserts | PS5_Vulkan's link recipe binds it to `ps5___assert` |
| `clock_nanosleep` | std (`sleep_until`) | link recipe → `ps5_clock_nanosleep` |
| `dirfd` | std (`fs::read_dir` metadata) | link recipe → `ps5_dirfd` (with the platform's `opendir` family) |
| `getpwuid_r` | `dirs` | link recipe → `ps5_getpwuid_r` |
| `gmtime_r` | `time` | link recipe → `ps5_gmtime_r` |
| `getrandom` | `getrandom` crate (rand) | PSGopher: `ps5/host/rust_stubs.c`, over `arc4random_buf` (bound to the platform's) |
| `dl_iterate_phdr` | std's backtraces | PSGopher: returns 0 (no symbols in a backtrace), as OGBox does |

The link then found three more that resolve only to `libkernel_sys`, whose imports are null in a
title, and two with no stub at all:

| Name | Who asks | Resolution |
| --- | --- | --- |
| `pthread_setname_np` | std's thread names | PSGopher: forwards to FreeBSD's `pthread_set_name_np` (exported) |
| `gai_strerror` | std's DNS errors | platform layer's `ps5_gai_strerror` |
| `umask` | PSGopher's own `main` | removed: a title's kernel applies no mode mask (`ps5platform/libc.h`) |
| `accept4` | std, mio | platform layer's `ps5_accept4` |
| `recvmmsg` | quinn-udp (netplay) | PSGopher: one `recvmsg` per message |
| `realpath` | std's `canonicalize` | platform layer's `ps5_realpath` (the console refuses `realpath` to titles) |
| `ZSTD_trace_*` (weak) | zstd | zstd built with `ZSTD_TRACE=0` |

All bound with `--defsym`, kept local (PS5_ARCHITECTURE.md D12). After that, all 351 imports of
the linked title come from modules a title loads [here]. Still unproven until a console run calls
them: `_umtx_op` (every contended std `Mutex`/`Condvar`), `kqueue`/`kevent` (Tokio; `kevent`
has console evidence in PROBE.md), `sysctl`, `sigaltstack`. The probe exercises the first two.

A stub existing at link time does not prove the console exports the function
(`ps5-homebrew` runtime-surface rule): the title's import check (`check-imports`) runs at every link.

## Platform matrix

| Facility | Desktop Gopher64 | PS5 |
| --- | --- | --- |
| Window, events | SDL3 video | none: one display through `VK_KHR_display`; events from the pad |
| Vulkan | system loader | RADV linked in |
| Audio | SDL3 device | sceAudioOut, 48 kHz S16 stereo, 256-frame grains |
| Gamepads | SDL3 gamepad/joystick (+ HIDAPI) | scePad through the PS5 layer, up to 4 users |
| Keyboard | SDL3 | none (state array all up) |
| Files | `$XDG_*`/portable | `/data/psgopher/` |
| Threads | std/pthreads | std on the console's pthreads (2 MiB stacks via the wrap) |
| Heap | mimalloc | the platform heap in direct memory (malloc wrap) |
| Time | `Instant` (CLOCK_MONOTONIC) | the same; resolution and cost [unknown]: the benchmark mode measures them |
| Networking | netplay, RA, Discord | off |
| GUI | Slint | none yet; boots the ROM named in `autostart.txt` |
