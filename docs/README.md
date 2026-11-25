# PSGopher documentation

The index of everything written about PSGopher. The user's guide is the repository's
[`README.md`](../README.md); every change is in [`CHANGELOG.md`](../CHANGELOG.md).

## Start here

| Document | Read it when |
| --- | --- |
| [`dev/workflow.md`](dev/workflow.md) | Before any change: the owner's rules, the loop, working with agents, lessons learned |
| [`dev/open-work.md`](dev/open-work.md) | Choosing what to do next: known problems, what no console has run, unfinished work, decisions |

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
