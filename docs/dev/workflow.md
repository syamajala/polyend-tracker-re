# Workflow and file formats

## Branches

- **`main`** is the fork's integration branch: what gets flashed and released. It's never
  rebased; upstream releases are merged into it.
- **`v2`** mirrors upstream's `v2` (`sandlbn/polyend-tracker-re`) and is fast-forwarded
  when upstream moves.
- **Feature branches** come into `main` through pull requests on the fork, with CI. A
  change meant for upstream starts from `upstream/v2`, so it can be offered upstream as it
  is; fork-only work (the DX7 engine, song scenes …) can start from `main`.
- **Upstream pull requests** are opened one at a time. Upstream squash-merges, so a
  branch stacked on another is rebased after the first one lands.

## Merging an upstream release

1. Fast-forward the mirror: `git push origin upstream/v2:v2`.
2. Merge `upstream/v2` into a branch off `main`, resolve, then build with `make clean`
   and the new `VERSION`, run the host tests, `make -C sim check` and
   `tools/sim_remote_check.sh`.
3. Open a pull request into `main`; flash it together with whatever else waits for a
   device test.

When upstream squash-merged one of our own pull requests, its conflicts resolve to our
side, and only upstream's other changes need applying on top
(`git diff <their squash> <their release>`).

## Compatibility rules

Projects must keep opening in stock firmware, upstream OpenTracker and the Tracker Mini:

- **Stock files keep their layout.** `project.mt`, pattern files, `.pti` instruments and
  `.patch` files are written exactly as the stock / Mini formats define them.
- **Fork data goes into side files** next to `project.mt`, which other firmware ignores:

| File | Holds |
|---|---|
| `perf_scenes.bin` | the Performance scene bank, A / B, and the fork's column types (DJ filter, tape stop) |
| `song.bin` | per-slot Count, Scene, Scene B and Ramp |
| `midi_lfo.bin` | one LFO per MIDI instrument |
| `.patch` field 100 | a DX7 voice (155 bytes), past every Mini field |

- Side files are versioned and written only when they hold something. The fork has no
  installed base yet, so their layouts can still change without migration code; stock
  compatibility can't.
- A new Performance column type is **not** written into `project.mt`'s type bytes (stock
  reads them); it lives in `perf_scenes.bin`, and stock sees an empty column.

## Code conventions

- `src/core` is platform-independent (shared by the firmware, the simulator and the host
  tests); `src/hal` is the K66 hardware layer.
- Comments mark the origin of behaviour: `[I]` the fork's own (not in stock), spec
  references (`spec 12 §E.2`) for reverse-engineered stock behaviour.
- The audio path has no allocation, no I/O and no locks. Anything on it goes through
  `make perf-gate`; new screens get a display budget entry.
- No static constructors: a namespace-scope object or pointer must not need dynamic
  initialisation (for example `T* const p = f();` with `f()` in another file). Use
  `constexpr`, an `extern` array with `inline` helpers, or set it up in `start()`. Check
  with `arm-none-eabi-nm -C build/opentracker.elf | grep _GLOBAL__sub_I`.
