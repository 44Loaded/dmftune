#!/usr/bin/env python3
"""Synthetic-voice regression test for the DMF TUNE DSP core.

Generates a sung melody (formant-filtered harmonic source, off-pitch notes,
vibrato, breathy gaps), runs it through the C++ engine and measures what comes out.
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.signal import lfilter

ROOT = Path(__file__).resolve().parent
BIN = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT.parent / "build_test" / "test_engine"
SR = 48000
A_MINOR = [0, 2, 4, 5, 7, 9, 11]


def m2f(m):
    return 440.0 * 2 ** ((np.asarray(m) - 69) / 12)


def resonator(f, bw, sr):
    r = np.exp(-np.pi * bw / sr)
    th = 2 * np.pi * f / sr
    return [1 - r], [1, -2 * r * np.cos(th), r * r]


def make_voice(melody, vib_cents=25, vib_hz=5.5, seed=1):
    """melody: list of (sung_midi, dur_s). 60 ms breathy gap between notes."""
    rng = np.random.default_rng(seed)
    segs, midi_track, voiced = [], [], []
    for sung, dur in melody:
        n = int(dur * SR)
        t = np.arange(n) / SR
        vib = np.clip((t - 0.15) / 0.1, 0, 1) * vib_cents / 100 * np.sin(2 * np.pi * vib_hz * t)
        midi_track.append(sung + vib)
        voiced.append(np.ones(n, bool))
        g = int(0.06 * SR)
        midi_track.append(np.full(g, sung, float))
        voiced.append(np.zeros(g, bool))
    midi = np.concatenate(midi_track)
    vflag = np.concatenate(voiced)
    # smooth pitch transitions a little (glide)
    k = int(0.03 * SR)
    midi = np.convolve(np.pad(midi, (k, k), mode="edge"), np.ones(k) / k, mode="same")[k:-k]
    f0 = m2f(midi)
    phase = np.cumsum(2 * np.pi * f0 / SR)
    src = np.zeros_like(phase)
    for h in range(1, 40):
        fh = f0 * h
        amp = (1.0 / h ** 1.4) * (fh < 0.45 * SR)
        src += amp * np.sin(h * phase)
    # vocal tract: /a/-ish formants
    y = np.zeros_like(src)
    for f, bw, gain in [(700, 110, 1.0), (1150, 120, 0.7), (2600, 160, 0.4)]:
        b, a = resonator(f, bw, SR)
        y += gain * lfilter(b, a, src)
    env = np.convolve(vflag.astype(float), np.hanning(int(0.02 * SR)) / np.hanning(int(0.02 * SR)).sum(), mode="same")
    y *= env
    # breath noise in gaps
    noise = rng.standard_normal(len(y))
    b, a = resonator(3500, 1500, SR)
    y += 0.08 * (1 - env) * lfilter(b, a, noise) * np.max(np.abs(y)) * 0.3
    y = 0.5 * y / np.max(np.abs(y))
    return y.astype(np.float32), midi, vflag


def f0_track(x, hop=0.01, win=0.04):
    """Autocorrelation f0 per frame, returns (times, midi or nan)."""
    n = int(win * SR)
    h = int(hop * SR)
    lo, hi = int(SR / 600), int(SR / 70)
    out_t, out_m = [], []
    for s in range(0, len(x) - n, h):
        fr = x[s:s + n].astype(np.float64)
        fr = fr - fr.mean()
        if np.sqrt(np.mean(fr ** 2)) < 0.01:
            out_t.append((s + n / 2) / SR); out_m.append(np.nan); continue
        spec = np.fft.rfft(fr * np.hanning(n), 2 * n)
        ac = np.fft.irfft(spec * np.conj(spec))[:n]
        ac /= ac[0] + 1e-12
        seg = ac[lo:hi]
        # first strong peak (avoid octave-down errors)
        peaks = [i for i in range(1, len(seg) - 1) if seg[i] > seg[i - 1] and seg[i] >= seg[i + 1]]
        if not peaks:
            out_t.append((s + n / 2) / SR); out_m.append(np.nan); continue
        top = max(seg[p] for p in peaks)
        p = next(p for p in peaks if seg[p] > 0.85 * top)
        a, b, c = seg[p - 1], seg[p], seg[p + 1]
        den = a - 2 * b + c
        d = 0.5 * (a - c) / den if abs(den) > 1e-12 else 0
        lag = lo + p + d
        out_t.append((s + n / 2) / SR)
        out_m.append(69 + 12 * np.log2(SR / lag / 440) if top > 0.5 else np.nan)
    return np.array(out_t), np.array(out_m)


def nearest_scale(m, mask_pcs):
    best = None
    for k in range(int(m) - 12, int(m) + 14):
        if k % 12 in mask_pcs and (best is None or abs(k - m) < abs(best - m)):
            best = k
    return best


def run_engine(x, args, tag):
    with tempfile.TemporaryDirectory() as td:
        fin, fout = Path(td, "in.f32"), Path(td, "out.f32")
        x.astype(np.float32).tofile(fin)
        res = subprocess.run([str(BIN), str(fin), str(fout), str(SR)] + args, capture_output=True, text=True, check=True)
        y = np.fromfile(fout, dtype=np.float32)
    lat = int(res.stdout.split("latency=")[1].split()[0])
    print(f"[{tag}] {res.stdout.strip()}")
    return y, lat


def main():
    # sung notes (MIDI, fractional) = intended A-minor note +/- off-pitch error
    intended = [57, 60, 64, 62, 60, 57, 55, 57]
    errors = [+0.35, -0.30, +0.22, -0.40, +0.28, -0.18, +0.42, -0.25]
    melody = [(n + e, 0.55) for n, e in zip(intended, errors)]
    x, midi_true, vflag = make_voice(melody)
    print(f"test signal: {len(x)/SR:.1f}s, {len(melody)} notes")

    fails = 0

    def evaluate(tag, y, lat, human_flat=True, tol=10.0):
        nonlocal fails
        ya = y[lat:]
        t, m = f0_track(ya)
        seg_start = 0.0
        ok_notes, rows = 0, []
        for i, (n, e) in enumerate(zip(intended, errors)):
            start = seg_start + 0.20
            end = seg_start + 0.55 - 0.05
            sel = (t >= start) & (t <= end) & ~np.isnan(m)
            if sel.sum() < 5:
                rows.append((i, n, None, None)); seg_start += 0.61; continue
            med = np.median(m[sel])
            sd = np.std(m[sel]) * 100
            err = (med - n) * 100
            good = abs(err) < tol and (sd < 12 if human_flat else True)
            ok_notes += good
            rows.append((i, n, err, sd))
            seg_start += 0.61
        for i, n, err, sd in rows:
            if err is None:
                print(f"   note {i} ({n}): NO PITCH FOUND")
            else:
                print(f"   note {i} target {n}: error {err:+5.1f} c, wobble {sd:4.1f} c")
        n_ok = ok_notes
        status = "PASS" if n_ok >= len(intended) - 1 else "FAIL"
        if status == "FAIL":
            fails += 1
        print(f"[{tag}] {n_ok}/{len(intended)} notes locked -> {status}")

    # 1) HARD tune, no vibrato kept: every note must land on the scale note and be flat
    y, lat = run_engine(x, ["speed=0", "human=0", "mode=1", "hold=0.3", "range=100", "mask=0xAB5"], "HARD a-minor")
    evaluate("HARD a-minor", y, lat, human_flat=True)
    rms_in = np.sqrt(np.mean(x[: len(y) - lat] ** 2))
    rms_out = np.sqrt(np.mean(y[lat:] ** 2))
    print(f"   level in/out: {20*np.log10(rms_out/rms_in):+.1f} dB")
    if abs(20 * np.log10(rms_out / rms_in)) > 3:
        print("   level drift too large -> FAIL"); fails += 1

    # 2) bypass: output must be the input delayed by exactly `lat`
    y, lat = run_engine(x, ["bypass=1"], "bypass")
    d = np.max(np.abs(y[lat:] - x[: len(y) - lat]))
    print(f"   bypass max difference vs delayed input: {d:.2e}")
    if d > 1e-3:
        print("   bypass is not transparent -> FAIL"); fails += 1

    # 3) mix=0 also transparent
    y, lat = run_engine(x, ["mix=0"], "mix0")
    d = np.max(np.abs(y[lat:] - x[: len(y) - lat]))
    print(f"   mix=0 max difference vs delayed input: {d:.2e}")
    if d > 1e-3:
        print("   mix=0 is not transparent -> FAIL"); fails += 1

    # 4) SHIFT +12 -> everything one octave up
    y, lat = run_engine(x, ["speed=0", "human=0", "shift=12", "range=0", "mode=1"], "shift +12")
    ya = y[lat:]
    t, m = f0_track(ya)
    t0, m0 = f0_track(x[: len(ya)])
    sel = ~np.isnan(m) & ~np.isnan(m0)
    diff = np.nanmedian((m[sel] - m0[sel])) if sel.sum() else float("nan")
    print(f"   median pitch change with shift=+12: {diff:+.2f} semitones")
    if not abs(diff - 12) < 0.3:
        print("   octave shift failed -> FAIL"); fails += 1

    # 5) formant shift keeps pitch
    y, lat = run_engine(x, ["speed=0", "human=0", "formant=4", "mask=0xAB5", "mode=1"], "formant +4")
    evaluate("formant +4 keeps tuning", y, lat, human_flat=True, tol=12.0)

    # 6) CLEAN mode, slow retune + human: should land closer but keep movement
    y, lat = run_engine(x, ["speed=120", "human=0.6", "mode=0", "mask=0xAB5"], "CLEAN slow")
    evaluate("CLEAN slow", y, lat, human_flat=False, tol=28.0)

    # 7) note sitting on the edge of RANGE with vibrato (sung 60.85 -> C4, vibrato peaks pass 100 c):
    #    the correction must not switch on and off, so the output stays flat on C4
    edge, _, _ = make_voice([(57.0, 0.5), (60.85, 1.2)], vib_cents=25)
    y, lat = run_engine(edge, ["speed=0", "human=0", "mode=1", "hold=0.4", "range=100", "mask=0xAB5"], "RANGE edge")
    t, m = f0_track(y[lat:])
    start = 0.56 + 0.35
    sel = (t >= start) & (t <= 0.56 + 1.1) & ~np.isnan(m)
    if sel.sum() < 10:
        print("   no pitch found on the edge note -> FAIL"); fails += 1
    else:
        sd = np.std(m[sel]) * 100
        worst = np.max(np.abs(m[sel] - 60.0)) * 100
        print(f"   edge note: wobble {sd:4.1f} c, worst deviation from C4 {worst:5.1f} c")
        if sd > 12 or worst > 45:
            print("   correction flutters at the RANGE edge -> FAIL"); fails += 1

    print("\nRESULT:", "ALL PASS" if fails == 0 else f"{fails} FAILURE(S)")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
