# PSGopher documentation

The index of everything written about PSGopher. The user's guide is the repository's
[`README.md`](../README.md); every change is in [`CHANGELOG.md`](../CHANGELOG.md).

## Start here

| Document | Read it when |
| --- | --- |
| [`dev/workflow.md`](dev/workflow.md) | Before any change: the owner's rules, the loop, working with agents, lessons learned |
| [`dev/open-work.md`](dev/open-work.md) | Choosing what to do next: known problems, what no console has run, unfinished work, decisions |
| [`dev/handover.md`](dev/handover.md) | Picking the work up: where it stands, the latest console findings, the next steps in order |

## Reference (`dev/`)

| Document | What it holds |
| --- | --- |
| [`dev/architecture.md`](dev/architecture.md) | The layers and the numbered decisions (D1-D18) that code comments cite; the milestones |
| [`dev/building.md`](dev/building.md) | The sibling checkouts, host packages, the build's steps, the pins and moving them, changing Gopher64 |
| [`dev/testing.md`](dev/testing.md) | The host tests, the interface pictures, the renderer's conformance suites |
| [`dev/debugging.md`](dev/debugging.md) | Reading the boot log (each prefix), crash reports, running a build on the console, the files the title reads |
| [`dev/dependencies.md`](dev/dependencies.md) | Every dependency, what it needs from the console, FreeBSD 11 under Rust's 12, the imports |
| [`dev/graphics.md`](dev/graphics.md) | parallel-rdp on RADV: why, where it comes from, the PS5 pieces, candidates |
| [`dev/performance.md`](dev/performance.md) | Method, instrumentation, console observations, baseline, the optimisation log |
| [`dev/compatibility.md`](dev/compatibility.md) | The benchmark set (by slot), the conformance results, the per-game record (by ID) |
| [`dev/mupen64-ps5-comparison.md`](dev/mupen64-ps5-comparison.md) | PSGopher beside the PS5 port of Mupen64Plus-Next: recompilers against interpreters, 8x on the GPU, recommendations |

## Performance programme (`dev/perf/`)

Phase A of the performance programme (2026-11-26): an audit by specialists, each finding checked by an
independent reviewer. The interpreters stay ([`dev/workflow.md`](dev/workflow.md) rule 9).

| Document | What it holds |
| --- | --- |
| [`dev/perf/performance-audit.md`](dev/perf/performance-audit.md) | The work per VI on the emulation thread and the GPU, measured against inferred, the unknowns |
| [`dev/perf/benchmark-analysis.md`](dev/perf/benchmark-analysis.md) | The measurements so far and their caveats, benchmark mode's defects, the Phase B protocol |
| [`dev/perf/gpu-optimisation-plan.md`](dev/perf/gpu-optimisation-plan.md) | parallel-rdp: depth blending, Pre/Post, synchronisation, the upscale changed in a game, 4x as default |
| [`dev/perf/interpreter-optimisation-plan.md`](dev/perf/interpreter-optimisation-plan.md) | The RSP and VR4300 interpreters, and the correctness tests against the original |
| [`dev/perf/ps5-platform-optimisation.md`](dev/perf/ps5-platform-optimisation.md) | Threads and priorities, the GPU wait policy, memory, compiler target, PGO |
| [`dev/perf/implementation-roadmap.md`](dev/perf/implementation-roadmap.md) | Phases B-E in priority order, each item with problem, evidence, change, risks, method |

## Frontend audit (`frontend-audit/`)

The multi-agent audit of the front end (2026-11-25). Its bug IDs (`FE-nnn`) are cited by fixes.

| Document | What it holds |
| --- | --- |
| [`frontend-audit/MASTER_PLAN.md`](frontend-audit/MASTER_PLAN.md) | The plan, agent assignments, status, outstanding investigation |
| [`frontend-audit/ARCHITECTURE.md`](frontend-audit/ARCHITECTURE.md) | The front end's structure and its inventory (screens, controls, threads, states) |
| [`frontend-audit/BUGS.md`](frontend-audit/BUGS.md) | Verified and suspected defects with evidence |
| [`frontend-audit/IMPROVEMENTS.md`](frontend-audit/IMPROVEMENTS.md) | Improvement proposals, prioritised |
| [`frontend-audit/COVERAGE.md`](frontend-audit/COVERAGE.md) | What was inspected (the coverage figure) and what was not |
| [`frontend-audit/ROADMAP.md`](frontend-audit/ROADMAP.md) | The order to fix and improve in |

## History (`history/`)

| Document | What it holds |
| --- | --- |
| [`history/porting-analysis.md`](history/porting-analysis.md) | The assessment written before any console run (2026-11-24): hypotheses, the twelve questions, ranked optimisations |

## Elsewhere in the tree

| Where | What |
| --- | --- |
| `ps5/art/live-board.html` | The front end's design: the specification D17 follows |
| `ps5/patches/gopher64/ps5.patch`, `parallel-rdp.patch` | Every change to Gopher64, and to its parallel-rdp submodule ([`dev/building.md`](dev/building.md#pins)) |
| `NOTICE` | Attributions (OGBox and Crux2 by Press5elect) and every other author |
