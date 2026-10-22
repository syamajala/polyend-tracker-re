#!/usr/bin/env python3
"""Write the dx7_render note schedule as a MIDI file (to render in Dexed).

  compare_midi.py out.mid [--notes 48,60,72] [--len 1.5] [--vel 0.8]

Same timing as dx7_render: note k at 3 k s held --len s, then the chord
held --len s, 3 s of tail. 120 BPM, 480 ticks per beat (1 s = 960 ticks).
Velocity = round(--vel x 127), as the engine maps it.
"""
import argparse
import struct

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--notes", default="48,60,72")
ap.add_argument("--len", type=float, default=1.5)
ap.add_argument("--vel", type=float, default=0.8)
a = ap.parse_args()
notes = [int(n) for n in a.notes.split(",")]
vel = max(1, min(127, int(a.vel * 127 + 0.5)))
TPS = 960  # ticks per second at 120 BPM, 480 PPQ
SLOT, TAIL = 3.0, 3.0

ev = []  # (seconds, order, bytes)
for k, n in enumerate(notes):
    ev.append((SLOT * k, 1, bytes([0x90, n, vel])))
    ev.append((SLOT * k + a.len, 0, bytes([0x80, n, 0])))
chord = SLOT * len(notes)
for n in notes:
    ev.append((chord, 1, bytes([0x90, n, vel])))
    ev.append((chord + a.len, 0, bytes([0x80, n, 0])))
end = chord + a.len + TAIL
ev.sort(key=lambda e: (e[0], e[1]))


def vlq(x):
    out = [x & 0x7F]
    x >>= 7
    while x:
        out.insert(0, (x & 0x7F) | 0x80)
        x >>= 7
    return bytes(out)


trk = bytearray(b"\x00\xff\x51\x03\x07\xa1\x20")  # tempo 500000 us / beat
last = 0
for t, _, b in ev:
    tick = int(round(t * TPS))
    trk += vlq(tick - last) + b
    last = tick
trk += vlq(int(round(end * TPS)) - last) + b"\xff\x2f\x00"
with open(a.out, "wb") as f:
    f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480))
    f.write(b"MTrk" + struct.pack(">I", len(trk)) + trk)
print(f"{a.out}: notes {notes}, velocity {vel}, {end:.1f} s")
