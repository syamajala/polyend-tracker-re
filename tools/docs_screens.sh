#!/bin/sh
# Screenshots for the documentation site (docs/images/), taken in the
# headless simulator so they can be regenerated whenever a screen changes:
#   make testsongs && make -C sim && tools/docs_screens.sh [NAME ...]
# Two scratch cards: "project" (the generated test songs, with "BG World 1"
# open as the workspace) and "empty" (a fresh project, plus the generated
# example DX7 bank, sim/fixtures/make_dx7_bank.py). No third-party content.
# Each shot is its own run from a fresh card, so they don't depend on each
# other. PNGs are optimised with Pillow when it is installed.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
sim="$root/build/sim/opentracker-sim"
songs="$root/build/testsongs"
out="$root/docs/images"
[ -x "$sim" ] || { echo "docs_screens: build the simulator first (make -C sim)" >&2; exit 2; }
[ -d "$songs/Projects" ] || { echo "docs_screens: build the test songs first (make testsongs)" >&2; exit 2; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$out"

card() {   # card KIND DIR
    rm -rf "$2" "$2.eeprom"   # the sim keeps Config in DIR.eeprom: start clean
    mkdir -p "$2/Patches/DX7"
    python3 "$root/sim/fixtures/make_dx7_bank.py" "$2/Patches/DX7/Example bank.syx"
    mkdir -p "$2/MidiPresets"   # an example MIDI preset (#13)
    printf '# OpenTracker MIDI preset\ncc A = 74 Cutoff\ncc B = 71 Resonance\ncc C = 73 Attack\ncc D = 72 Release\ncc E = 1 Mod\ncc F = PC\nlfo target = CC 74\nlfo depth = 30\n' \
        > "$2/MidiPresets/Example synth.txt"
    if [ "$1" = project ]; then
        cp -R "$songs/." "$2/"
        mkdir -p "$2/Workspace"
        cp -R "$songs/Projects/BG World 1/." "$2/Workspace/"
    fi
}

shot() {   # shot NAME KIND < script
    name=$1
    if [ -n "$only" ] && ! echo " $only " | grep -q " $name "; then cat > /dev/null; return; fi
    card "$2" "$work/card"
    cat > "$work/$name.txt"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$sim" --sd "$work/card" --no-audio --no-midi \
        --script "$work/$name.txt" --screenshot "$out/$name.png" > "$work/$name.log" 2>&1 ||
        { echo "docs_screens: $name failed" >&2; tail -5 "$work/$name.log" >&2; exit 1; }
    echo "docs_screens: $name.png"
}
only="${*:-}"

shot pattern project <<'EOF'
wait 500
tap PLAY
wait 3000
EOF

# Song mode: slot 1 plays 4 times with scene 3 on A and 4 on B (ramp: Slot);
# the screenshot is taken during the second pass.
shot song project <<'EOF'
wait 300
tap SONG
wait 300
tap REC
tap RIGHT            # Count
enc 3                # x4
tap RIGHT            # Scene
enc 3
press SHIFT
enc 4                # 3>4
release SHIFT
tap LEFT
tap LEFT
tap REC
tap F2               # Play Song
wait 4500             # the third of four passes
EOF

# Performance with the master FX: DJ filter (column 1) and Tape stop
# (column 2) set in edit mode, scene A stored, then played with both pads.
shot performance project <<'EOF'
wait 300
tap PERFORMANCE
wait 300
tap REC
tap DOWN             # type list on column 1 (Volume)
enc 21
tap F8               # DJ filter
tap RIGHT
tap DOWN             # column 2 (Panning)
enc 21
tap F8               # Tape stop
pad 12 tap
enc 3
pad 13 tap
tap REC
press SHIFT
tap LEFT             # store scene A
release SHIFT
tap PLAY
wait 1500
pad 12 down
wait 800
EOF

# The FX type list (#89): Down in edit mode on column 3, moved to Overdrive.
shot perf-type-list project <<'EOF'
wait 300
tap PERFORMANCE
wait 300
tap REC
tap RIGHT
tap RIGHT
tap DOWN
enc 18
wait 300
EOF

# The scene bank page: scenes 3 and 4 edited, on A and B.
shot scene-bank empty <<'EOF'
wait 300
tap PERFORMANCE
wait 300
tap PERFORMANCE      # bank page
wait 200
tap RIGHT
tap RIGHT
tap F3               # edit scene 3
pad 12 tap
tap PERFORMANCE
tap F1               # scene 3 on A
tap RIGHT
tap F3               # edit scene 4
pad 24 tap
pad 25 tap
tap PERFORMANCE
tap F2               # scene 4 on B
wait 2500            # let the notices go
EOF

# Resample: Sample Recorder while playing -> Resample (F6).
shot resample project <<'EOF'
wait 300
tap PLAY
wait 500
tap SAMPLERECORDER
wait 300
tap F6
wait 600
EOF

# DX7: Synth 1 switched to DX7 on a fresh project, then Load Patch with the
# example bank, and the bank opened.
shot dx7-params empty <<'EOF'
wait 300
press INSTR
wait 400
enc -70              # Synth 1
release INSTR
wait 100
tap INSTRPARAMS
wait 200
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN             # Model: DX7
wait 2500
EOF

shot dx7-load empty <<'EOF'
wait 300
press INSTR
wait 400
enc -70
release INSTR
wait 100
tap INSTRPARAMS
wait 200
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
wait 2500
tap F7               # Load Patch
wait 400
EOF

shot dx7-bank empty <<'EOF'
wait 300
press INSTR
wait 400
enc -70
release INSTR
wait 100
tap INSTRPARAMS
wait 200
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
wait 2500
tap F7
wait 300
tap F8               # open the bank
wait 400
tap DOWN
tap DOWN
wait 300
EOF

# DX7 Edit Patch: the Tone section (the controls on top of the voice), and
# the Synth Macros page with M1-M6 aimed at them.
shot dx7-tone empty <<'EOF'
wait 300
press INSTR
wait 400
enc -70
release INSTR
wait 100
tap INSTRPARAMS
wait 200
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
wait 2500
tap F8               # Edit Patch
wait 300
tap RIGHT            # Tone
wait 300
EOF

shot dx7-macros empty <<'EOF'
wait 300
press INSTR
wait 400
enc -70
release INSTR
wait 100
tap INSTRPARAMS
wait 200
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
wait 2500
tap INSTRPARAMS      # Synth Macros
wait 200
enc 20               # M1 Bright up a little
wait 300
EOF

# MIDI 1's Preset page (#13) with the example preset loaded.
shot midi-presets empty <<'EOF'
wait 300
press INSTR
wait 400
enc -70
enc 51               # MIDI 1
release INSTR
wait 100
tap INSTRPARAMS
wait 200
tap INSTRPARAMS
wait 200
tap INSTRPARAMS      # Preset page
wait 200
tap F1               # Load list
wait 200
tap F8               # Load
wait 3500            # the notice fades
EOF

# MIDI 1's LFO page, target CC 1.
shot midi-lfo empty <<'EOF'
wait 300
press INSTR
wait 400
enc -70
enc 51               # MIDI 1 (Synth 1-3, samples 1-48, MIDI 1-16)
release INSTR
wait 100
tap INSTRPARAMS
wait 200
tap INSTRPARAMS      # LFO page
wait 200
enc 2                # Target: CC 1 (Mod)
wait 300
EOF

shot config-note-names empty <<'EOF'
wait 300
tap CONFIG
wait 300
tap DOWN             # General
tap RIGHT
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN
tap DOWN             # Note names (the 15th row, after Export sample rate)
wait 300
EOF

shot config-remote empty <<'EOF'
wait 300
tap CONFIG
wait 300
tap DOWN
tap DOWN
tap DOWN             # USB
tap RIGHT
tap DOWN             # Remote control
wait 300
EOF

if python3 -c "import PIL" 2>/dev/null; then
    python3 - "$out" <<'EOF'
import sys, glob
from PIL import Image
for f in glob.glob(sys.argv[1] + "/*.png"):
    Image.open(f).save(f, optimize=True)
EOF
fi
