# Debugging PSGopher on the console

How to read what a console run says, and how to make one. Evidence first: a change starts from the
boot log, not from a guess ([`workflow.md`](workflow.md#the-loop-for-a-change)).

## The boot log

`psgopher-boot.log` is `psgopher-boot.log` in the title folder (else in `/data/psgopher/`), the run before
kept as `psgopher-boot.previous.log`: every line the title says, and what Gopher64 writes to
standard error (descriptor 2 is the log file; a title may not `dup2`, so klog's capture moves only
C's `stderr` stream). The same lines go to klog.

What Gopher64 writes there: `config/config.json` (BENCH writes it too), `data/saves/`,
`data/states/`, `cache/` (RADV's shader cache in `cache/radv`). The front end keeps `cache/library.tsv`,
`cache/played.tsv`, `cache/covers/` (covers and descriptions) and copies a share's ROM to `cache/play/`.


What to look for:

| Line | Says |
| --- | --- |
| `build:` | the commit of PSGopher, Gopher64, RADV and the build time: check it is the build you think |
| `sandbox:` | whether the title left its sandbox (USB drives, the whole `/data`) |
| `shares:`, `vfs:` | each share mounted, lost and reconnected |
| `library:` | the search: places, budgets, headers read, the time it took |
| `psgopher:` | Gopher64's runtime (the Tokio fallback), its messages and panics |
| `rdp:` | the RDRAM path parallel-rdp took (imported host memory or the staging copy) |
| `video:` | every 10 s: VIs (against 50 or 59.94), speed, the game's own frame rate, CPU and present time a VI, RDRAM syncs, audio queue and underruns |
| `quickmenu:` | the quick menu opened and what was chosen |
| `covers:`, `about:` | each cover and description fetched, missing, or why the network failed |
| `crash report:` | a fault: signal, registers, stack offsets |

**Crashes**: the report's offsets are into the linked title; look them up on the build PC with

```bash
llvm-addr2line -f -C -e build/ps5/link/llvm-pie.elf 0x<offset>
```

with the same build's `build/` (the `build:` line says which commit).

**Speed**: the `video:` line separates the causes: CPU time a VI near the frame's budget means the
emulation thread is the limit; present wait means the GPU or the display; many RDRAM syncs mean the
CPU waits for the GPU. The stats overlay shows the same live ([`performance.md`](performance.md)).

## Running a build on the console

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
| `roms/<file>` | ROMs (`.z64 .n64 .v64`, `.zip`/`.7z` of one); in the library, as is every ROM found in `/data`, on USB drives and shares |
| `autostart.txt` | first line: a ROM to boot before the menu (a file in `roms/` or a full path) |
| `config/network.cfg` | the network shares (OGBox's format); the front end writes it |
| `elevation-off.txt` (in the title folder) | stay in the sandbox (no USB drives); BENCH, This console writes it |
| `probe.txt` | run the console probe instead of a game, then exit (`probe:` lines) |
| `config/psgopher.ini` | the display (`display = 60`, `120` or `vrr`), HDR10 (`hdr = on`, `hdr_white`, `hdr_peak`), the stats overlay (`stats = on`); BENCH and the quick menu write it |
| `benchmark.txt` | benchmark mode: lines `rom <file>`, `seconds <n>`, `unlimited` (`bench:` lines, `bench-<time>.csv`) |


## Test files

The benchmark mode (`benchmark.txt`) and the console probe (`probe.txt`) are described in
[`performance.md`](performance.md#benchmark-conditions).
