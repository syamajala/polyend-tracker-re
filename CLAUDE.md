# CLAUDE.md

OpenTracker fork: reverse-engineered firmware for the original Polyend Tracker (NXP K66,
Cortex-M4, 256 KB SRAM, 16 MB SDRAM). The developer docs in `docs/dev/` are the
reference; this file lists the rules that are easy to break.

## Build and test

- Firmware: `make clean && make -j VERSION=2.1.2` (the Makefile default VERSION is stale;
  run `make clean` whenever VERSION changes). See `docs/dev/building.md`.
- Host tests: `make host-test`. Simulator: `make -C sim check`, then `tools/sim_remote_check.sh`.
- Audio changes: `make perf-gate`. Size checks: `tools/telemetry_check.py size` and `frames`.
- Test in the simulator and on the host first. Use the device only for sound, real CPU and
  stack use, pads/LEDs, codec and SD timing, and batch device tests into one flash.
- Games are off by default (`OT_GAMES=1` builds them into `build/games`). Say which one
  you measured when you quote memory numbers.
- Stack guard: `/Firmware/ot_noguard.txt` on the card turns it off, and
  `make OT_STACK_GUARD_TEST=1` builds the overflow self-test image.
- Memory tools: `tools/size_report.py <base.elf> <head.elf>` prints the same table as the CI
  size report. `pahole -C App build/opentracker.elf` shows struct layouts (gdb isn't
  ARM-aware here).

## Code rules

- **No static constructors.** No namespace-scope object or pointer with a dynamic
  initializer (e.g. `T* const p = f();` with `f()` in another file). Use `constexpr`/
  `constinit`, an `extern` array in a header with `inline` helpers, or init in `start()`.
  Check: `arm-none-eabi-nm -C build/opentracker.elf | grep _GLOBAL__sub_I`, compared with `main`.
- **Memory:** only per-block audio state goes in SRAM. Data, UI state and buffers go in
  SDRAM (`.sdram_bss`). New audio code starts in flash, not `OT_FASTRUN_FN`, unless
  perf-gate shows it's needed. Check the CI size report on every PR. See `docs/dev/memory.md`.
- **Audio path:** no allocation, no I/O, no locks.
- `src/core` is platform-independent (firmware, sim, host tests); `src/hal` is the K66 layer.
- Mark fork-only behaviour with `[I]` comments, and reverse-engineered stock behaviour
  with spec references.

## Compatibility

- Stock files (`project.mt`, patterns, `.pti`, `.patch`) keep their exact stock/Mini layout.
  Fork data goes in side files (see `docs/dev/workflow.md`). These have no installed base
  yet, so their layouts can change without migration.

## Branches and PRs

- `main` is the integration branch and is never rebased. Upstream releases are merged into it.
- A change meant for upstream (`sandlbn/polyend-tracker-re`) branches from `upstream/v2`;
  fork-only work can branch from `main`. Upstream PRs go one at a time, and upstream
  squash-merges, so rebase stacked branches after each merge.
- Docs change with features: update `docs/guide` / `docs/dev` in the same PR.
- Rebasing onto `upstream/v2`: git rerere can replay a fork-only resolution (e.g. the
  fork's SDRAM layout) onto an upstream branch. Check every rerere resolution there.

## Before opening a PR

1. `make host-test`, `make -C sim check`, `tools/sim_remote_check.sh`.
2. `make perf-gate` if anything on the audio path changed.
3. Check the size-report delta and the `_GLOBAL__sub_I` count against the base.
4. Update the docs.
