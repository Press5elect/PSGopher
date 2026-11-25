# PSGopher: build guide

How the title is built on an x86-64 Linux PC (Ubuntu 24.04 here). Tests are in [`testing.md`](testing.md),
running it on the console in [`debugging.md`](debugging.md).

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
| the core | `ps5/tools/build-core.sh` | `build/core/x86_64-sie-ps5/psgopher/libgopher64.a` (the `psgopher` profile: release with fat LTO and one codegen unit) |
| libsmb2, libnfs, Dear ImGui, the fonts (Bricolage Grotesque, JetBrains Mono) at their pins | `ps5/tools/build-deps.sh` | `.deps/src`, `.deps/ports` |
| the sandbox exit's helper | `ps5/tools/build-elevator.sh` | `build/sandbox-elevator.elf` |
| the PS5 code | `build.sh` | `build/ps5/obj/*.o`, the fonts as headers `build/ps5/gen/font_*.h` |
| link, sign, lay out | `ps5/tools/link-title.sh` | `dist/PPSA99280/` (`eboot.bin`, `sce_sys/`, `sce_module/libc.prx`, `sandbox-elevator.elf`, `art/`, `licenses/`, `NOTICE.txt`), `build/ps5/imports.txt` |

`ps5/tools/build.sh --compile` stops before the link.

## Profile-guided builds

The core can be compiled with what it does on the console (Crux2's method, its
`docs/dev/building.md`). Three steps, the middle one on the console:

1. `PSGOPHER_PGO=generate ps5/tools/build.sh`: a profiling build. Gopher64's crate alone is
   instrumented (`-Cprofile-generate` through cargo's per-package profile flags: the standard
   library's crates cannot take it). LLVM's profile runtime is built for the console by
   `ps5/tools/build-profile-runtime.sh` from compiler-rt at **rustc's** LLVM release (23.1.6 when this
   was written; the raw format is that release's) with Crux2's stand-ins (no build IDs, no `uname`,
   `procctl`, `getpagesize`), and linked by `link-title.sh`, which also places the counters with
   the writable data.
2. Play the games the build should be good at. After each game the title writes its counts to
   `/data/psgopher/pgo/core-<time>-<n>.profraw` (the boot log's `pgo:` lines): the title never
   returns from `main`, so the runtime's own write at exit never happens.
3. `ps5/tools/merge-profiles.sh <files or folders>` merges them into `ps5/pgo/core.profdata` with
   rustc's own `llvm-profdata` (rustup's `llvm-tools`). Every build uses it from then on
   (`PSGOPHER_PGO=use`, the default when the file is there; `off` builds without).

## Pins

Every source is followed at its newest revision (the owner's rule: [`workflow.md`](workflow.md#the-rules)).
`ps5/tools/check-sources.sh` compares each pin with its upstream and `build.sh` stops when one is
behind (`PSGOPHER_ALLOW_STALE=1` builds anyway):

| Source | Pinned in |
| --- | --- |
| Gopher64 | `revision=` in `ps5/tools/fetch-gopher64.sh` |
| libsmb2, libnfs, Dear ImGui, the fonts | `ps5/tools/build-deps.sh` |
| The payload SDK fork | `ps5/tools/setup-sdk.sh` |
| RADV | PS5_Vulkan's Mesa pin with Crux2's patch series (D13 in [`architecture.md`](architecture.md)); the check compares Crux2's patch list |
| OGBox's share code | compared with OGBox's current files |
| Rust | nightly (`rustup update nightly`) |

**Moving Gopher64's pin**: in `vendor/gopher64`, fetch and check out the new revision (the working
tree's changes come along when they don't conflict), set `revision=` in `fetch-gopher64.sh`, then
`fetch-gopher64.sh --export` and `--check` (the patch applies to a fresh checkout at the pin), build,
run the host tests, and note the move in the changelog.

**Changing Gopher64**: edit `vendor/gopher64`, then:

```bash
ps5/tools/fetch-gopher64.sh --export
```

```bash
ps5/tools/fetch-gopher64.sh --check
```

The first writes `ps5/patches/gopher64/ps5.patch`, and `parallel-rdp.patch` for the changes inside
the parallel-rdp submodule (`parallel-rdp/parallel-rdp-standalone`, at the commit Gopher64's pin
records; `ps5.patch` leaves it out); the second applies both to a fresh checkout at the pin. A
change that is not exported is lost at the next fetch.
