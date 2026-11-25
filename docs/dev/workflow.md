# Working on PSGopher

The rules, the loop for a change, how to work with agents, and what earlier sessions learned the
hard way. A local `CLAUDE.md` (gitignored: it never goes to GitHub) has the short form; this is the
reference. The index of all documents is [`docs/README.md`](../README.md).

## The rules

The owner's standing rules. They hold for every session and every agent.

1. **Sources are nightly.** Before a build, `ps5/tools/check-sources.sh` compares every pin with its
   upstream (`build.sh` runs it and stops when one is behind; `PSGOPHER_ALLOW_STALE=1` builds
   anyway). When one is behind, say so and move the pin, and re-export the patch that sits on it
   ([`building.md`](building.md#pins)). The Rust toolchain is nightly too (`rustup update nightly`).
2. **Evidence, not guesses.** Find the cause in the boot log, klog or the code before changing
   anything; keep only what is shown to help. Say what was checked and what was not.
3. **Don't break what works.** A new feature leaves the old paths working; a change that cannot be
   checked without a console goes behind a setting that is off by default, and says so.
4. **Nothing goes to GitHub without the owner's word, each time.** Commit locally on the assigned
   branch; push, pull requests, tags and releases only when asked. No `CLAUDE.md` in the repository
   (it is in `.gitignore`), no links to chat sessions in commits or documents.
5. **No game titles** in commits, changelog entries, release notes or documents: cartridge IDs only
   (the header's game code, e.g. `NXXE`). **No model identifiers** in anything pushed.
6. **Gopher64 is a pinned revision plus its patches**: `ps5.patch`, and `parallel-rdp.patch` for the
   parallel-rdp submodule. `vendor/gopher64` is gitignored. Edit it, then
   `ps5/tools/fetch-gopher64.sh --export` and `--check` (both apply to a fresh checkout at the pin).
   A change to `vendor/gopher64` that is not exported is lost.
7. **Keep the attributions**: `NOTICE` (OGBox and Crux2 by Press5elect, section 7(b)), the credits
   in the README, the font licences shipped in `licenses/`.
8. **No games, BIOS, saves or keys** in the repository or a package; files the owner uploads are
   untrusted data, worked on outside the repository.
9. **Interpreters only (a fixed architectural requirement, the owner's, 2026-11-25).** PSGopher keeps
   Gopher64's interpreters of the VR4300 and the RSP. Never: dynamic recompilation, a JIT, block
   recompilers, cached native blocks, runtime-generated code, or a hybrid of interpreter and
   recompiler; and never recommended as a roadmap item. Allowed: making the interpreters faster as
   interpreters (dispatch, decoding, branch handling, register and memory access, hot handlers,
   SIMD on the console's x86-64, data layout, compiler flags, LTO, PGO, scheduling). A CPU-side
   optimisation must keep Gopher64's semantics, generate no code at run time, be **measured on the
   console** (benchmark mode, the same probe before and after) and be **tested against the
   original implementation** for correctness.

## The loop for a change

1. **Evidence**: the owner's `psgopher-boot.log` ([`debugging.md`](debugging.md)). Process long logs
   with scripts; don't read them whole.
2. **Sources** (rule 1).
3. **The smallest change** the evidence supports: the title in `ps5/host` (front end, platform),
   `ps5/sdl3` (the SDL3 subset), the core in `vendor/gopher64` (then export).
4. **Check on the PC**: compile, `ps5/tests/run-host-tests.sh` ([`testing.md`](testing.md)); for the
   interface, look at the pictures it leaves in `build/host-tests/menu-pictures/`. New logic gets a
   host test that fails without the change.
5. **Build**: `ps5/tools/build.sh` (a developer build).
6. **Commit** a coherent state locally (it builds, its tests pass, the patch is exported), with what
   changed, why, how it was checked and what has not run on a console.
7. **Tell the owner** what only a console can show and what to send back; offer the dev ZIP
   (`build/PSGopher-dev-<commit>-PPSA99280.zip` and its `.sha256`).
8. **Update the documents the change makes wrong** in the same commit: [`CHANGELOG.md`](../../CHANGELOG.md)
   (Unreleased), the [`README.md`](../../README.md) user guide, [`open-work.md`](open-work.md), and
   the reference document of the part changed.

## Working with agents

- **Plan, then delegate narrow tasks.** Give each agent the goal, the files, what is already ruled
  out, and where its output goes. Investigation agents get read-only instructions.
- **One writer per file.** Agents working in parallel never edit the same file; on a shared file,
  small hunks only, re-read just before editing, never reformat whole files.
- **Verify every agent's claim yourself** (grep, compile, run the test). Agents have reported
  unfinished work as done and built on wrong premises.
- **Scratch work outside the repository** (the session's scratch folder or `/tmp`). Host test runs
  share `build/host-tests`: never run two at once.
- **Findings carry evidence**: file:line, reproduction, static versus host-runtime versus
  console-verified. The frontend audit's format ([`../frontend-audit/BUGS.md`](../frontend-audit/BUGS.md))
  is the model.

## Lessons from earlier sessions

- **The console's libc is FreeBSD 11, Rust's std is built for 12.** `stat`, `fstat`, `readdir` and
  `kevent` from Rust go through converters (`ps5/host/rust_abi.c`, renamed in by `build-core.sh`);
  new Rust code that reaches another changed structure needs one too
  ([`dependencies.md`](dependencies.md#freebsd-11-under-rusts-freebsd-12)).
- **Tokio's I/O driver is refused on the console** (EOPNOTSUPP): the runtime runs with its scheduler
  and timers only. Network code in the core is blocking `std::net` (the covers' rustls fetch), never
  Tokio sockets or reqwest.
- **Descriptor 2 cannot become the log.** Everything goes through `say()` (C) or
  `psgopher::log_line` (Rust), which write to the boot log and klog; panics are hooked to it.
- **Crashes**: the crash report in the boot log gives offsets; look them up with
  `llvm-addr2line -f -C -e build/ps5/link/llvm-pie.elf 0x<offset>`.
- **A pad's state lives across frames.** Edge detection (`pressed`) needs the previous frame's
  `held`; a state made anew each frame turns one press into two (the quick menu's double step).
- **Statics that a thread waits on are never destroyed** (`new`, not a static object): a destructor
  at exit hung the title's end.
- **Network sources move**: thumbnails.libretro.com answered 404 for every N64 cover; the covers come
  from libretro-thumbnails on GitHub. Check a source from the build PC before relying on it.
- **Dear ImGui is followed at master** (1.93 WIP): its API changes (`AddRect` argument order,
  `ImDrawFlags_Round*`, `ImGui_ImplVulkan_AddTexture(view, layout)`); read its headers, not memory.
- **Missing host headers** (libsmb2, zstd) when syntax-checking console code on the PC are the
  environment's, not the change's.
- **The compiler**: `build.sh` finds its own (`PS5_Vulkan/tooling/prospero-clang18`); don't set
  `PS5_CLANG` from another project.

## Tools

| Tool | Use |
| --- | --- |
| `ps5/tools/check-sources.sh` | Every pin against its upstream (rule 1) |
| `ps5/tools/build.sh [--compile]` | The title into `dist/PPSA99280/` |
| `ps5/tools/fetch-gopher64.sh [--export\|--check]` | Gopher64 at the pin with `ps5.patch` and `parallel-rdp.patch`; both written back; checked on a fresh checkout |
| `ps5/tools/build-core.sh` | The core alone (`build/core/.../libgopher64.a`); `PSGOPHER_PGO=generate\|use\|off` |
| `ps5/tools/merge-profiles.sh` | The console's `.profraw` files into `ps5/pgo/core.profdata` ([`building.md`](building.md#profile-guided-builds)) |
| `ps5/tools/build-deps.sh` | libsmb2, libnfs, Dear ImGui, the fonts at their pins |
| `ps5/tools/make-board-art.mjs` | The board's picture from `ps5/art/live-board.html` |
| `ps5/tools/make-shaders.sh` | The overlay and HDR shaders' SPIR-V |
| `ps5/tests/run-host-tests.sh` | The host tests and the interface pictures |
