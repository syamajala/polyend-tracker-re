# Testing and CI

The rule of thumb: **test in the simulator and on the host first**, and use the device for
what only hardware shows (sound quality and clicks, real CPU and stack use, pads and LEDs,
the codec and Line in, SD timing). Batch device tests so one flash covers several changes.

## Layers

| Layer | Command | Covers |
|---|---|---|
| Host tests | `make host-test` | ~100 test binaries: models, formats, sequencer, audio engines, UI screens, display budgets (ASan + UBSan) |
| Simulator checks | `make -C sim check` | smoke runs, display budgets and fragment checks per screen, resampling end to end, the test songs open and play |
| Remote scripts | `tools/sim_remote_check.sh` | `tools/remote_scripts/*.txt` against the simulator, each on a fresh card; the same files run on a Tracker |
| M4 benchmark | `make perf-gate` | audio instruction counts on an emulated Cortex-M4 against a baseline, engine limits, size checks |
| Device | `tools/tracker_remote.py`, `tools/telemetry_check.py log` | scripts and telemetry on a real Tracker |

## Remote scripts

Scripts in `tools/remote_scripts/` drive the app through
[`tracker_remote.py`](../guide/remote-control.md#drive-mode-scripts) and are written to run
on both the simulator and a Tracker:

- Wait on the sequencer, not the clock: `wait-for pass == 2`, `wait-for slot == 3 20000`,
  so a script works at any tempo.
- A script's card starts with `SCRIPT.sd/` (copy it to a Tracker's card too) and, in the
  simulator only, `SCRIPT.sim-sd/`.

On a device:

```sh
tools/tracker_remote.py /dev/ttyACM0 tools/remote_scripts/song_scene.txt --log tracker.log
tools/telemetry_check.py log tracker.log
```

`tools/device_soak.txt` is a navigation-only soak for a loaded project (no edits, saves or
loads).

Gotchas when writing scripts:

- Opening the Sample Recorder while a pattern plays pops a "stop the pattern?" dialog
  (Resample / Cancel / Yes) that takes the F keys. Stop playback first, or answer it.
- Config reopens on the category it was last on, so start from a known place (e.g. Up × 7).
- On a device, the diag ring drops early boot lines. Send a byte on the CDC port to replay
  the boot log.

## CI

Every push and pull request runs `.github/workflows/ci.yml`:

| Job | Does |
|---|---|
| **host-tests** | `make host-test` with GCC 14, plus `telemetry_check.py selftest` |
| **sim-check** | builds the simulator, `make -C sim check`, the remote scripts; uploads screenshots |
| **firmware** | builds the image, size / stack / libm checks, the stack frame check; uploads the `.ptf` as an artifact |
| **size-report** | pull requests only: memory use of the PR against its base, as a table in the job summary |

Error messages are shown as annotations on the run, so they're visible without opening the
logs.

## Releases

Pushing a tag `vMAJOR.MINOR.PATCH[-suffix]` (e.g. `v2.1.2-ot1`) runs
`.github/workflows/release.yml`: host tests, the firmware with the size checks, and a
GitHub release with the `.ptf` image. The image uses the numeric version only
(`v2.1.2-ot1` → `PolyendTrackerOTv212.ptf`); the suffix names the release.
