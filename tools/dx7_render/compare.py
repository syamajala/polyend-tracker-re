#!/usr/bin/env python3
"""Compare dx7_render WAVs with Dexed renders of the same schedule.

  compare.py ours_dir dexed_dir [--plot out_dir]

Files pair up by their leading number ("01_..."). Any sample rate, mono or
stereo (Dexed is mixed to mono). Each file is aligned on its first onset and
level-matched overall (the engines' output gains differ), then for each of
the four segments (three notes, the chord) reports:
  pitch   strongest partial of the held part, ratio ours / Dexed (cents)
  bright  spectral centroid ratio (cents; + = ours brighter)
  env     mean |difference| of the 10 ms RMS envelopes, dB (held + release)
  trem    level wobble during the hold (std of the envelope, dB), ours / Dexed
A row is flagged '*' when |pitch| > 10 c, |bright| > 200 c, env > 3 dB or the
wobble differs by > 1.5 dB.
"""
import argparse
import glob
import os
import re
import wave

import numpy as np

SLOT, LEN = 3.0, 1.5


def load(path):
    w = wave.open(path)
    sr, ch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
    raw = w.readframes(w.getnframes())
    if sw == 2:
        x = np.frombuffer(raw, np.int16).astype(np.float64) / 32768
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        x = ((b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)) << 8 >> 8) / 8388608.0
    elif sw == 4:
        x = np.frombuffer(raw, np.int32).astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"{path}: {sw * 8}-bit not supported (export 16/24/32-bit PCM)")
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr


def onset(x):
    thr = np.max(np.abs(x)) * 1e-3
    i = np.argmax(np.abs(x) > thr)
    return i


def env_db(x, sr, hop=0.01):
    n = int(sr * hop)
    m = len(x) // n
    r = np.sqrt(np.mean(x[: m * n].reshape(m, n) ** 2, axis=1) + 1e-12)
    return 20 * np.log10(r)


def spectrum(x, sr):
    n = 1 << 16
    seg = x[:n] if len(x) >= n else np.pad(x, (0, n - len(x)))
    sp = np.abs(np.fft.rfft(seg * np.hanning(n)))
    f = np.fft.rfftfreq(n, 1 / sr)
    return f, sp


def cents(a, b):
    return 1200 * np.log2(a / b) if a > 0 and b > 0 else float("nan")


def features(x, sr, t0):
    out = []
    for k in range(4):
        a = t0 + int(SLOT * k * sr)
        hold = x[a + int(0.1 * sr): a + int(LEN * sr)]
        full = x[a: a + int((LEN + 1.4) * sr)]
        f, sp = spectrum(hold, sr)
        band = (f > 20) & (f < 16000)
        pk = f[band][np.argmax(sp[band])]
        p2 = sp[band] ** 2
        cen = float(np.sum(f[band] * p2) / max(np.sum(p2), 1e-30))
        e = env_db(full, sr)
        eh = env_db(hold, sr)
        out.append(dict(pk=pk, cen=cen, env=e, trem=float(np.std(eh[eh > eh.max() - 40])) if len(eh) else 0.0))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ours")
    ap.add_argument("dexed")
    a = ap.parse_args()
    num = lambda p: int(re.match(r"(\d+)", os.path.basename(p)).group(1))
    dex = {num(p): p for p in glob.glob(os.path.join(a.dexed, "*.wav")) if re.match(r"\d+", os.path.basename(p))}
    rows = 0
    print(f"{'voice':16s} {'level':>6s} | {'seg':>5s} {'pitch c':>8s} {'bright c':>9s} {'env dB':>7s} {'trem o/d':>10s}")
    for po in sorted(glob.glob(os.path.join(a.ours, "*.wav")), key=num):
        n = num(po)
        if n not in dex:
            continue
        xo, so = load(po)
        xd, sd = load(dex[n])
        to, td = onset(xo), onset(xd)
        go = np.sqrt(np.mean(xo[to:] ** 2)) + 1e-12
        gd = np.sqrt(np.mean(xd[td:] ** 2)) + 1e-12
        lvl = 20 * np.log10(go / gd)
        fo, fd = features(xo / go, so, to), features(xd / gd, sd, td)
        name = os.path.basename(po)[:-4]
        for k, seg in enumerate(["C3", "C4", "C5", "chord"]):
            o, d = fo[k], fd[k]
            pc = cents(o["pk"], d["pk"])
            bc = cents(o["cen"], d["cen"])
            m = min(len(o["env"]), len(d["env"]))
            eo, ed = np.maximum(o["env"][:m], -60), np.maximum(d["env"][:m], -60)
            ed_ = float(np.mean(np.abs(eo - ed)))
            flag = "*" if (abs(pc) > 10 and seg != "chord") or abs(bc) > 200 or ed_ > 3 or abs(o["trem"] - d["trem"]) > 1.5 else " "
            print(f"{name if k == 0 else '':16s} {f'{lvl:+.1f}' if k == 0 else '':>6s} | {seg:>5s} {pc:8.1f} {bc:9.0f} {ed_:7.1f} {o['trem']:4.1f}/{d['trem']:<4.1f} {flag}")
        rows += 1
    if not rows:
        print("no pairs: name the Dexed files 01_*.wav .. 32_*.wav (leading number = voice)")


if __name__ == "__main__":
    main()
