#!/bin/sh
# [I] #21 benchmark: opens and plays the "OT Pool N" songs over remote control
# and keeps each song's telemetry (cyc_per_voice) in its own log.
#   tools/pool_bench.sh PORT IMAGE N [N ...]
#   e.g. tools/pool_bench.sh /dev/ttyACM0 h300 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16
# The songs (make testsongs, songs 23-38) live in Projects/Bench/ on the card.
# Each is opened with "open" (@RC DO OPEN, unsaved changes dropped), so the
# script needs no File screen keys.
# BENCH_PLAY_MS (default 20000): how long each song plays.
# Logs: bench/IMAGE/poolN.log (under the current directory); table:
# tools/pool_bench_summary.py bench
set -e
tools=$(cd "$(dirname "$0")" && pwd)
port=$1
image=$2
shift 2
mkdir -p "bench/$image"
names="ACD PERC FM4S CS-Type 8-Type DX7 VAP FAT WTFM JU6 D-Type A-Type T-Type Hand B-Type ARC"
for n in "$@"; do
    name=$(echo $names | cut -d' ' -f"$n")
    [ -n "$name" ] || { echo "pool_bench: no song $n (1..16)"; exit 1; }
    echo "== OT Pool $n - $name ($image)"
    python3 "$tools/tracker_remote.py" "$port" --stop-on-fail --log "bench/$image/pool$n.log" -c "
        echo POOL $n $image; open -f Projects/Bench/OT Pool $n - $name;
        play; wait ${BENCH_PLAY_MS:-20000}; stop" \
        || { echo "pool $n: remote run failed (see bench/$image/pool$n.log)"; exit 1; }
done
