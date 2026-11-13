# Workflow and file formats

## Branches

- **`main`** is the fork's integration branch: what gets flashed and released. It's on
  upstream's 3.x (synth and FX modules, build profiles) and never rebased; upstream's
  `v3` is merged into it.
- **`main-v2`** is the fork on upstream 2.x, as it was when `main` moved to 3.x
  (2026-11-08). It's kept for reference and gets no upstream merges.
- **`v2`** mirrors upstream's `v2` (`sandlbn/polyend-tracker-re`) and is fast-forwarded
  when upstream moves.
- **Feature branches** come into `main` through pull requests on the fork, with CI. A
  change meant for upstream starts from `upstream/v3` (or `upstream/v2` for a 2.x fix), so
  it can be offered upstream as it is; fork-only work (card modules, song scenes …) starts
  from `main`.
- **Upstream pull requests** are opened one at a time. Upstream squash-merges, so a
  branch stacked on another is rebased after the first one lands.

## Merging upstream

1. Update the module repositories (`opentracker-synths`, `opentracker-fx`) to upstream's
   `main`, since upstream v3 can depend on module changes.
2. Merge `upstream/v3` into a branch off `main`, resolve, then build with `make clean`,
   run the host tests, `make -C sim check` and `tools/sim_remote_check.sh`. Upstream
   merges its `v2` fixes into `v3`, so they arrive this way too. Check a v3 merge for
   deletions of fork work (`git diff main <merge>`): upstream's first v3 snapshot rolled
   back some 2.2.x work.
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
| `perf_scenes.bin` | the Performance scene bank, A / B, and the fork's column types (DJ filter, tape stop, MIDI CC), MIDI CC targets and the fader ranges (#12) |
| `song.bin` | per-slot Count, Scene, Scene B and Ramp |
| `midi_lfo.bin` | one LFO per MIDI instrument |
| `usb_audio.bin` | the Global Mixer USB level and mute, and whether the Sample Recorder records USB |
| `timing.bin` | each pattern's per-track Speed and Groove (#43 / #83), one record per pattern off the defaults |
| `multisample.bin` | the root notes of each multisample instrument's zones and whether it plays in Multi mode (the `.pti` is a plain Slice instrument) |
| `.patch` field 100 | a DX7 voice (155 bytes), past every Mini field |

- Side files are versioned and written only when they hold something. The fork has no
  installed base yet, so their layouts can still change without migration code; stock
  compatibility can't.
- A new Performance column type is **not** written into `project.mt`'s type bytes (stock
  reads them); it lives in `perf_scenes.bin`, and stock sees an empty column.
- Files on the card outside a project are text, so people can edit them on a computer:
  `MidiPresets/<name>.txt` (`formats/midi_preset.h`) and `Performance/Scenes/<name>.txt`
  scene banks (`formats/perf_bank.h`). Both use `key = value` lines, `#` comments and
  ignore unknown keys, so later versions can add keys. The shipped MIDI presets are in
  `midi_presets/`; host tests parse each one.
- `Progressions/<Group>.txt` (Song mode progressions, `ui/progression.h`) is one
  progression per line in scale numbers, not `key = value`. The built-in files are
  compiled in (`prog::defaultText`) and written only when the folder has no `.txt` file,
  never over an existing one.
- `Themes/<name>.json` are Tracker CE's colour themes, read and written in CE's format so
  themes move between CE's web editor and OpenTracker (`ui/theme_file.h`). The CE key ->
  slot map is generated: edit `tools/ce_theme_map.py` and run it to rewrite
  `src/core/ui/ce_theme_map.inc`. The map came from black-box observation only: a theme
  with one colour per CE key was screenshotted in CE's web editor and matched pixel by pixel
  against the simulator with one colour per slot. Slots 277..284 are fork slots for parts
  CE colours apart from the stock sets; the stock themes copy the colour each one replaces.
  The example themes are in `themes/`; host tests parse each one.

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
  with `arm-none-eabi-nm -C build/full/opentracker.elf | grep _GLOBAL__sub_I`.
