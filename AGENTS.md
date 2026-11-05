# AGENTS.md

OpenTracker fork: reverse-engineered firmware for the original Polyend Tracker (NXP K66,
Cortex-M4, 256 KB SRAM, 16 MB SDRAM). The developer docs in `docs/dev/` are the
reference; this file lists the rules that are easy to break.

## Vision

A Polyend Tracker that stays **cohesive, immediate and fun**. Check every feature idea
against it.

**Cohesive: it feels like one instrument, not a pile of features.**
- Reuse what's there: store or extend existing concepts (Performance mode, step FX,
  swing) before inventing new ones.
- One way to do each thing: if a feature overlaps an existing one, one replaces the
  other or it isn't built.
- It's still a Tracker: an idea that mainly makes it more like an M8 or an Octatrack
  doesn't belong.
- Stock projects still load and play; fork data degrades gracefully on stock.

**Intuitive and immediate: the core is usable within a minute, without the manual.**
- Defaults mean stock behaviour: a plain song or pattern looks and works as on stock;
  depth shows up only when used.
- One key away, and it sounds right: no menu diving (e.g. Performance pad rows are
  curated presets).
- Key combos are shortcuts only: every action is also reachable without one; check the
  screen's key map before adding another.
- Keep each screen readable: new columns and cells have to earn their space.
- Depth features (setup pages such as Fader Ranges) may need the manual, but one read of
  the guide should make them click: lead with a plain example of what it does.

**Fun: it makes you want to pick it up and play.**
- Sound and play first (synths, FX, performance, flipping samples): ideas that make
  music sound or feel better beat ideas that are merely more complete.
- Making music decides: friction found while making real tracks ranks the work, not the
  issue list.

**The test:** does it make the Tracker more fun to play without making it less cohesive
or less immediate? If it costs one of the three, it needs a very good reason.

## Clean room

OpenTracker is a clean-room implementation. Polyend has published its own firmware as
Tracker CE (`polyend/tracker-ce`) under a noncommercial licence; its code stays out of
OpenTracker (one MIT exception below).

- Never clone, fetch, open, search or quote CE source code, headers or binaries, and don't
  port, translate or paraphrase them. This includes code search, web pages that show CE
  code, and snippets pasted into a prompt.
- CE's user-facing docs (manual, changelog, release notes) and on-device behaviour are
  fine as a reference, written up in your own words.
- Developer docs that describe internals or contain code count as source. Plain hardware
  facts from them (pins, part numbers, buses, memory sizes) are fine.
- `.ptfx` interface docs (file format, plugin API) are fine: under the SDK exception below
  when they're MIT, and as interface facts when they're not. How CE's own code loads
  plugins stays out.
- The one exception is CE's sample editor SDK (the `.ptfx` plugin SDK), which is MIT
  licensed. It may be read and reused, as long as it goes in `third_party/` with its
  licence and copyright notice. Check the licence of the exact version you take. The
  exception covers only the SDK: nothing else in the CE repository.
- Spec references cite stock behaviour or published docs, never CE code.
- If a task looks like it needs CE source, stop and say so instead of looking.

## Build and test

- Firmware: `make clean && make -j` (needs the module repositories `opentracker-synths` and
  `opentracker-fx`; set `MODULES_DIR` when they aren't two levels up). Outputs go to
  `build/<profile>/` (default `full`). Don't pass `VERSION`: the Makefile default follows the
  last upstream merge (bump it there when merging; run `make clean` whenever it changes).
  See `docs/dev/building.md`.
- Host tests: `make host-test`. Simulator: `make -C sim check`, then `tools/sim_remote_check.sh`.
- Audio changes: `make perf-gate`. Size checks: `tools/telemetry_check.py size` and `frames`.
- Test in the simulator and on the host first. Use the device only for sound, real CPU and
  stack use, pads/LEDs, codec and SD timing, and batch device tests into one flash.
- Games are off by default (`OT_GAMES=1` builds them into `build/games`). Say which one
  you measured when you quote memory numbers.
- Stack guard: `/Firmware/ot_noguard.txt` on the card turns it off, and
  `make OT_STACK_GUARD_TEST=1` builds the overflow self-test image.
- Memory tools: `tools/size_report.py <base.elf> <head.elf>` prints the same table as the CI
  size report. `pahole -C App build/full/opentracker.elf` shows struct layouts (gdb isn't
  ARM-aware here).

## Code rules

- **No static constructors.** No namespace-scope object or pointer with a dynamic
  initializer (e.g. `T* const p = f();` with `f()` in another file). Use `constexpr`/
  `constinit`, an `extern` array in a header with `inline` helpers, or init in `start()`.
  They run before SDRAM is up: `tools/check_ctors.py` fails the build if one touches SDRAM.
  Check: `arm-none-eabi-nm -C build/full/opentracker.elf | grep _GLOBAL__sub_I`, compared with `main`.
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
