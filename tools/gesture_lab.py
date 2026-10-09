#!/usr/bin/env python3
"""Offline reference classifier for the five gestures, run against the
labelled IMU CSVs in bringup/traces/.

This is the Python twin of the heuristic rung of the GestureEngine (see
CLAUDE.md "Gestures are heuristic-first"). Thresholds get tuned HERE against
recorded traces, then ported to SmartWand/config.h -- so it is deliberately
dependency-free and written the way the firmware will have to compute it:
running sums over one window, no FFTs, no matrices beyond 2x2.

One window = one cast. On the wand the cast button defines the window. The
2026-08-16 traces predate the button, so each file is split into reps by
gyro-magnitude activity with a long hangover instead -- a stand-in, not the
real thing. Reps cut off by the start or end of a capture are skipped.

The features (all computed over one window):

  cum     cumulative rotation, deg: sum of |gyro|*dt, counting only samples
          above DEADBAND_DPS. Without the deadband, gyro noise and bias
          integrate to ~100 deg over an 8 s idle hold -- and circle has no
          upper bound on hold length.
  planar  how single-plane the rotation is, 0..1, from the 2x2 covariance of
          (gy, gz) -- the rotation vector's components across the wand.
          (l1 - l2) / (l1 + l2) of its eigenvalues. Flicks and zigzag sweep
          the vector back and forth along ONE line (~0.9-1.0); a circle
          sweeps it around (~0.2).
  phi     direction of that line in the y-z plane, deg. 0 = pitch (flick up/
          down), +/-90 = yaw (side to side = zigzag). Both planar and |phi|
          are invariant to the SIGN of rotation, which is why it doesn't
          matter which way a zigzag starts.
  y1      signed rotation at the first strong lobe, projected on the phi
          axis. Flick up is negative, flick down positive (mount-specific,
          see CLAUDE.md board-axis mapping).
  rev     diagnostic only: count of 3D gyro-vector direction reversals
          (successive strong lobes with negative dot product). The original
          zigzag idea. Reported, not used -- circles score as high as zigzags.

Usage:
    python tools/gesture_lab.py                  # classify every rep
    python tools/gesture_lab.py --roll-sweep     # tolerance to wand roll
    python tools/gesture_lab.py --traces bringup/traces/2026-08-16_yuval
"""

import argparse
import csv
import math
from pathlib import Path

# --- segmentation stand-in (replaced by the cast button on the wand) -------
SEG_ON_DPS = 120.0      # rep starts above this gyro magnitude
SEG_OFF_DPS = 60.0      # ...and ends once below this for SEG_HANG_S
SEG_HANG_S = 0.8        # long enough to keep a flick's return and a thrust's
                        # wind-up inside the same rep, as a held button would
SEG_PREROLL = 15        # samples kept before the trigger (the IMU FIFO's job)

# --- classifier thresholds (candidates for config.h) -----------------------
DEADBAND_DPS = 75.0     # an 8 s still hold integrates to 171 deg at 30 dps (Daniel)
                        # but <=44 at 75; his slow circles stay >60 dps 99% of the time
MIN_CUM_DEG = 55.0      # below: no gesture. 8 s idle <=44, smallest real rep ~64
PLANAR_MIN = 0.80       # flick/zigzag 0.87-1.00; thrust <=0.68; circle <=0.27
PLANAR_PEAK_DPS = 300.0 # a planar window must also be fast to be a flick/zig
PHI_SPLIT_DEG = 45.0    # |phi| below: flick. above: zigzag
CIRCLE_MAX_PEAK_DPS = 400.0   # circle peaks 127-365; thrust 420-679
CIRCLE_MIN_CUM_DEG = 180.0    # circle 218-296 (one rev); thrust <=140 (Yuval)
LOBE_DPS = 250.0        # "strong" for the first-lobe sign and reversal count

TRUTH = {"flick_up": "flick_up", "flick_down": "flick_down",
         "thrust": "thrust", "zigzag": "zigzag", "circle": "circle",
         "circle_ccw": "circle", "idle": "none"}


def load(path):
    with open(path, newline="") as f:
        return [[float(r[k]) for k in ("millis", "ax", "ay", "az", "gx", "gy", "gz")]
                for r in csv.DictReader(f)]


def mag(v):
    return math.sqrt(sum(x * x for x in v))


def segment(d):
    """Split a capture into reps. Returns (window, truncated) pairs."""
    reps, start, last = [], None, None
    for i, s in enumerate(d):
        g = mag(s[4:7])
        if g > SEG_ON_DPS:
            start = i if start is None else start
            last = i
        elif start is not None and g < SEG_OFF_DPS \
                and (s[0] - d[last][0]) / 1000 > SEG_HANG_S:
            reps.append((start, last))
            start = None
    if start is not None:
        reps.append((start, last))
    out = []
    for a, b in reps:
        a, b = max(0, a - SEG_PREROLL), min(len(d) - 1, b + 5)
        out.append((d[a:b + 1], a == 0 or b == len(d) - 1))
    return out


def roll(w, deg):
    """Rotate gyro about the board x axis -- simulates the wand held twisted."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [r[:5] + [c * r[5] - s * r[6], s * r[5] + c * r[6]] for r in w]


def reversals(w, alpha=0.4):
    f, ref, n = None, None, 0
    for s in w:
        g = s[4:7]
        f = g[:] if f is None else [alpha * x + (1 - alpha) * y for x, y in zip(g, f)]
        m = mag(f)
        if m < LOBE_DPS:
            continue
        if ref is None:
            ref = f[:]
            continue
        cos = sum(x * y for x, y in zip(f, ref)) / (m * mag(ref))
        if cos < -0.3:
            n, ref = n + 1, f[:]
        elif cos > 0.5 and m > mag(ref):
            ref = f[:]
    return n


def features(w):
    dt = [(w[i + 1][0] - w[i][0]) / 1000 for i in range(len(w) - 1)]
    gm = [mag(s[4:7]) for s in w]
    cum = sum(g * t for g, t in zip(gm, dt) if g > DEADBAND_DPS)

    syy = sum(s[5] ** 2 for s in w)
    szz = sum(s[6] ** 2 for s in w)
    syz = sum(s[5] * s[6] for s in w)
    tr, det = syy + szz, syy * szz - syz ** 2
    l1 = tr / 2 + math.sqrt(max(0.0, tr * tr / 4 - det))
    planar = (2 * l1 - tr) / tr if tr else 0.0
    phi = 0.5 * math.atan2(2 * syz, syy - szz)      # [-90, 90] deg, cos >= 0

    # first strong lobe, projected on the principal axis
    best = None
    for s in w:
        m = mag(s[4:7])
        if m > LOBE_DPS and (best is None or m > mag(best)):
            best = s[4:7]
        elif best is not None and m < LOBE_DPS / 2:
            break
    y1 = best[1] * math.cos(phi) + best[2] * math.sin(phi) if best else 0.0

    return dict(dur=(w[-1][0] - w[0][0]) / 1000, peak=max(gm), cum=cum,
                planar=planar, phi=math.degrees(phi), y1=y1, rev=reversals(w))


def classify(f):
    if f["cum"] < MIN_CUM_DEG:
        return "none"
    if f["planar"] >= PLANAR_MIN and f["peak"] >= PLANAR_PEAK_DPS:
        if abs(f["phi"]) >= PHI_SPLIT_DEG:
            return "zigzag"
        return "flick_up" if f["y1"] < 0 else "flick_down"
    if f["peak"] < CIRCLE_MAX_PEAK_DPS and f["cum"] >= CIRCLE_MIN_CUM_DEG:
        return "circle"
    return "thrust"


def windows(dirs):
    for d in dirs:
        for p in sorted(Path(d).glob("*.csv")):
            data = load(p)
            reps = segment(data)
            if p.stem == "idle":     # a still hold is one whole (non-)cast
                reps = [(data, False)]
            for w, trunc in reps:
                yield d.name[11:], p.stem, w, trunc


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--traces", nargs="*", type=Path,
                    default=sorted(Path("bringup/traces").glob("*_*")))
    ap.add_argument("--roll-sweep", action="store_true")
    args = ap.parse_args()

    reps = [r for r in windows(args.traces) if not r[3]]

    if args.roll_sweep:
        print("roll   correct")
        for deg in range(-60, 61, 5):
            ok = sum(classify(features(roll(w, deg))) == TRUTH[g] for _, g, w, _ in reps)
            print(f"{deg:+4d}   {ok}/{len(reps)}" + ("" if ok == len(reps) else "  <--"))
        return

    print(f"   {'who':6} {'gesture':11} {'got':10} {'dur':>4} {'peak':>5} {'cum':>4}"
          f" {'planar':>6} {'phi':>6} {'y1':>5} rev")
    ok = 0
    for who, g, w, _ in reps:
        f = features(w)
        got = classify(f)
        ok += got == TRUTH[g]
        print(f"{'  ' if got == TRUTH[g] else 'XX'} {who:6} {g:11} {got:10} {f['dur']:4.2f}"
              f" {f['peak']:5.0f} {f['cum']:4.0f} {f['planar']:6.2f} {f['phi']:6.1f}"
              f" {f['y1']:5.0f} {f['rev']:3d}")
    print(f"\n{ok}/{len(reps)} correct (reps truncated by capture start/end skipped)")


if __name__ == "__main__":
    main()
