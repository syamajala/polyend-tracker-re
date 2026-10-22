#!/bin/sh
# Runs remote-control scripts (tools/tracker_remote.py) against the
# simulator in serve mode, each on a fresh empty card, so the scripts that
# drive a real Tracker are checked without one:
#   tools/sim_remote_check.sh [SCRIPT ...]   (default: tools/remote_scripts/*.txt)
# Needs build/sim/opentracker-sim (make -C sim). Exit status 1 if a check failed.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
sim="$root/build/sim/opentracker-sim"
[ -x "$sim" ] || { echo "sim_remote_check: build the simulator first (make -C sim)" >&2; exit 2; }
[ $# -gt 0 ] || set -- "$root"/tools/remote_scripts/*.txt
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0
for script in "$@"; do
    name=$(basename "$script" .txt)
    mkdir -p "$work/$name/sd"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$sim" --sd "$work/$name/sd" --serve > "$work/$name/serve.log" 2>&1 &
    pid=$!
    pty=""
    for _ in $(seq 50); do
        pty=$(sed -n 's/^serve: //p' "$work/$name/serve.log")
        [ -n "$pty" ] && break
        sleep 0.1
    done
    if [ -z "$pty" ]; then
        echo "FAIL  $name: the simulator did not start" >&2
        fail=1
    elif python3 "$root/tools/tracker_remote.py" "$pty" "$script" --out "$work/$name" > "$work/$name/out.txt" 2>&1; then
        echo "ok    $name ($(grep -c '^ok' "$work/$name/out.txt") checks)"
    else
        echo "FAIL  $name" >&2
        grep -v '^ok' "$work/$name/out.txt" >&2
        fail=1
    fi
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
done
exit $fail
