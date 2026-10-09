#!/usr/bin/env python3
"""Offline reference classifier for the five gestures, run against the
labelled IMU CSVs in bringup/traces/.

This is the Python twin of the heuristic rung of the GestureEngine (see
CLAUDE.md "Gestures are heuristic-first"). Thresholds get tuned HERE against
recorded traces, then ported to SmartWand/config.h -- so it is deliberately
dependency-free and written the way the firmware will have to compute it:
running sums over one window, no FFTs, no matrices beyond 2x2.

One window = one cast. Traces with a non-zero `cast` column (ImuTest with the
cast button, 2026-10-09 on) are split one window per press, exactly as the
wand will see them. The 2026-08-16 traces predate the button, so they are
split into reps by gyro-magnitude activity with a long hangover instead -- a
stand-in, not the real thing. Windows cut off by the start or end of a
capture are skipped.

The features (all computed over one window). "Pitch" is the wand tilting up
or down, "yaw" turning side to side; both are rotation ACROSS the wand, i.e.
the (gy, gz) components. gx is roll, twist about the wand's own length.

  cum     cumulative rotation, deg: sum of |gyro|*dt, counting only samples
          above DEADBAND_DPS. Without the deadband, an 8 s still hold
          integrates to 171 deg -- circle territory, and circle has no upper
          bound on hold length.
  planar  how single-line the (gy, gz) rotation vectors are, 0..1:
          (l1 - l2) / (l1 + l2) of their 2x2 covariance's eigenvalues.
          Flicks ~0.93-1.0, zigzag ~0.87-0.94 (the diagonal's pitch is what
          it loses), thrust <=0.68, circle <=0.27.
  phi     direction of that line, deg. 0 = pitch (flicks), +/-90 = yaw
          (zigzag). Sign-invariant, so it doesn't matter which way a Z starts.
  flips   sign changes of the (smoothed) rotation projected on the phi axis,
          counting only strong lobes. A Z is right-left-right: 2 flips. A
          flick's return stroke is at most 1. This is the vector-reversal
          idea measured along the cast's own main axis rather than along raw
          board axes, which is what made the 2026-08-16 attempt fail.
  smooth  mean speed while moving / peak speed. A circle is sustained motion
          (0.64-0.91), a thrust a burst (0.26-0.48). Unlike a peak-speed
          cutoff it doesn't move when a gesture is performed faster or slower.
  y1      signed rotation at the first strong lobe, projected on phi. Flick
          up negative, flick down positive (mount-specific, see CLAUDE.md
          board-axis mapping). Summing gy instead nets ~0: the return stroke
          cancels it.

Usage:
    python tools/gesture_lab.py                  # classify every rep
    python tools/gesture_lab.py --roll-sweep     # tolerance to wand twist
    python tools/gesture_lab.py --stress         # steep Zs, fast circles, slow thrusts
    python tools/gesture_lab.py --traces bringup/traces/2026-08-16_yuval
    python tools/gesture_lab.py --dump-windows windows.csv   # for the C++ parity test
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
MIN_CUM_DEG = 55.0      # below: no gesture. 8 s idle <=44, smallest real rep ~61
FAST_PEAK_DPS = 300.0   # flicks and zigzags are fast; circles peak 127-365
PHI_SPLIT_DEG = 45.0    # |phi| below: pitch (flick). above: yaw (zigzag)
FLICK_MIN_PLANAR = 0.80 # flicks 0.93-1.00; thrust <=0.68
ZIGZAG_MIN_PLANAR = 0.50   # loose on purpose: a steep diagonal adds pitch.
                           # Only here to keep circles (<=0.27) out
ZIGZAG_MIN_FLIPS = 2    # zigzag exactly 2 on every rep; everything else <=1
CIRCLE_MIN_CUM_DEG = 180.0  # one circle 214-296; more circles only add
CIRCLE_MIN_SMOOTH = 0.55    # circle 0.64-0.91; thrust 0.26-0.48
LOBE_DPS = 250.0        # "strong" for the first-lobe sign and flip count
SMOOTH_ALPHA = 0.4      # EMA weight for the flip counter's smoothing

TRUTH = {"flick_up": "flick_up", "flick_down": "flick_down",
         "thrust": "thrust", "zigzag": "zigzag", "circle": "circle",
         "circle_ccw": "circle", "idle": "none"}


def load(path):
    """Rows of [millis, ax, ay, az, gx, gy, gz, cast]. cast is the button
    press number (0 = released); traces from before the button read 0."""
    with open(path, newline="") as f:
        return [[float(r[k]) for k in ("millis", "ax", "ay", "az", "gx", "gy", "gz")]
                + [int(r.get("cast") or 0)]
                for r in csv.DictReader(f)]


def casts(d):
    """Split a button-gated capture into casts: one window per press.
    A press still held when the capture ended is marked truncated."""
    out, cur, cur_id = [], [], 0
    for s in d:
        if s[7] != cur_id and cur:
            out.append((cur, False))
            cur = []
        cur_id = s[7]
        if cur_id:
            cur.append(s)
    if cur:
        out.append((cur, True))
    return out


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


def project(w, phi_deg):
    """Per-sample rotation along the phi axis, deg/s, EMA-smoothed."""
    c, s = math.cos(math.radians(phi_deg)), math.sin(math.radians(phi_deg))
    f, out = None, []
    for r in w:
        p = r[5] * c + r[6] * s
        f = p if f is None else SMOOTH_ALPHA * p + (1 - SMOOTH_ALPHA) * f
        out.append(f)
    return out


def count_flips(proj):
    last, n = 0, 0
    for p in proj:
        if abs(p) > LOBE_DPS:
            sign = 1 if p > 0 else -1
            n += bool(last and sign != last)
            last = sign
    return n


def features(w):
    dt = [(w[i + 1][0] - w[i][0]) / 1000 for i in range(len(w) - 1)]
    gm = [mag(s[4:7]) for s in w]
    moving = [g for g in gm if g > DEADBAND_DPS]
    cum = sum(g * t for g, t in zip(gm, dt) if g > DEADBAND_DPS)
    peak = max(gm)

    syy = sum(s[5] ** 2 for s in w)
    szz = sum(s[6] ** 2 for s in w)
    syz = sum(s[5] * s[6] for s in w)
    tr, det = syy + szz, syy * szz - syz ** 2
    l1 = tr / 2 + math.sqrt(max(0.0, tr * tr / 4 - det))
    planar = (2 * l1 - tr) / tr if tr else 0.0
    phi = math.degrees(0.5 * math.atan2(2 * syz, syy - szz))   # [-90, 90]

    # first strong lobe, projected on the principal axis
    best = None
    for s in w:
        m = mag(s[4:7])
        if m > LOBE_DPS and (best is None or m > mag(best)):
            best = s[4:7]
        elif best is not None and m < LOBE_DPS / 2:
            break
    r = math.radians(phi)
    y1 = best[1] * math.cos(r) + best[2] * math.sin(r) if best else 0.0

    return dict(dur=(w[-1][0] - w[0][0]) / 1000, peak=peak, cum=cum,
                planar=planar, phi=phi, y1=y1,
                flips=count_flips(project(w, phi)),
                smooth=(sum(moving) / len(moving)) / peak if moving else 0.0)


def classify(f):
    if f["cum"] < MIN_CUM_DEG:
        return "none"
    if f["peak"] >= FAST_PEAK_DPS:
        if abs(f["phi"]) >= PHI_SPLIT_DEG and f["planar"] >= ZIGZAG_MIN_PLANAR \
                and f["flips"] >= ZIGZAG_MIN_FLIPS:
            return "zigzag"
        if abs(f["phi"]) < PHI_SPLIT_DEG and f["planar"] >= FLICK_MIN_PLANAR:
            return "flick_up" if f["y1"] < 0 else "flick_down"
    if f["cum"] >= CIRCLE_MIN_CUM_DEG and f["smooth"] >= CIRCLE_MIN_SMOOTH:
        return "circle"
    return "thrust"


# --- synthetic perturbations, for --roll-sweep and --stress ----------------

def roll(w, deg):
    """Rotate gyro about the board x axis -- simulates the wand held twisted."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [r[:5] + [c * r[5] - s * r[6], s * r[5] + c * r[6]] + r[7:] for r in w]


def steepen(w, deg):
    """Add downward pitch to a zigzag's middle stroke, `deg` of extra tilt on
    top of whatever the performer already put there -- simulates a taller Z."""
    k = math.tan(math.radians(deg))
    proj = project(w, features(w)["phi"])
    last, stroke, out = 0, 0, []
    for r, p in zip(w, proj):
        if abs(p) > LOBE_DPS:
            sign = 1 if p > 0 else -1
            stroke += bool(last and sign != last)
            last = sign
        out.append(r[:5] + [r[5] + k * abs(r[6]) if stroke == 1 else r[5], r[6]] + r[7:])
    return out


def retime(w, k):
    """Perform the same motion k times faster (k < 1: slower)."""
    t0 = w[0][0]
    return [[t0 + (r[0] - t0) / k] + r[1:4] + [x * k for x in r[4:7]] + r[7:] for r in w]


def windows(dirs):
    for d in dirs:
        for p in sorted(Path(d).glob("*.csv")):
            data = load(p)
            if any(s[7] for s in data):
                reps = casts(data)   # the button marked the windows
            elif p.stem == "idle":   # a still hold is one whole (non-)cast
                reps = [(data, False)]
            else:
                reps = segment(data)
            for w, trunc in reps:
                yield d.name[11:], p.stem, w, trunc


def stress(reps):
    def tally(label, ws, want):
        got = [classify(features(w)) for w in ws]
        ok = sum(g == want for g in got)
        wrong = sorted({g for g in got if g != want})
        print(f"  {label:10} {ok}/{len(ws)}" + (f"  -> {', '.join(wrong)}" if wrong else ""))

    zz = [w for _, g, w, _ in reps if g == "zigzag"]
    cc = [w for _, g, w, _ in reps
          if TRUTH[g] == "circle" and features(w)["cum"] >= CIRCLE_MIN_CUM_DEG]
    tt = [w for _, g, w, _ in reps
          if g == "thrust" and features(w)["cum"] >= MIN_CUM_DEG]
    print("zigzag, diagonal steepened by extra tilt (natural tilt is 6-18 deg):")
    for d in (0, 15, 30, 40, 45, 50, 60):
        tally(f"+{d} deg", [steepen(w, d) for w in zz], "zigzag")
    print("circle, performed faster:")
    for k in (1.0, 1.5, 2.0, 3.0):
        tally(f"x{k}", [retime(w, k) for w in cc], "circle")
    print("thrust, performed slower:")
    for k in (1.0, 0.75, 0.5):
        tally(f"x{k}", [retime(w, k) for w in tt], "thrust")


def dump_windows(reps, path):
    """Write every window -- real reps plus the --stress and --roll-sweep
    variants -- with this script's features and verdict, for
    tests/gesture_parity.cpp to replay through the C++ GestureEngine."""
    variants = []
    for who, g, w, _ in reps:
        variants.append((f"{who}/{g}", w))
        for deg in (-30, -15, 15, 30):
            variants.append((f"{who}/{g}/roll{deg:+d}", roll(w, deg)))
        for k in (0.5, 0.75, 1.5, 2.0, 3.0):
            variants.append((f"{who}/{g}/x{k}", retime(w, k)))
        if g == "zigzag":
            for d in (15, 30, 45, 50, 60):
                variants.append((f"{who}/{g}/steep{d}", steepen(w, d)))
    with open(path, "w", newline="") as f:
        for name, w in variants:
            # the firmware only ever sees whole milliseconds; retime() makes
            # fractional ones, so round before computing the reference
            w = [[float(round(r[0]))] + r[1:] for r in w]
            ft = features(w)
            f.write(f"W,{name},{classify(ft)},{ft['dur']:.6f},{ft['peak']:.6f},"
                    f"{ft['cum']:.6f},{ft['planar']:.6f},{ft['phi']:.6f},"
                    f"{ft['y1']:.6f},{ft['flips']},{ft['smooth']:.6f}\n")
            for r in w:
                f.write("S," + ",".join(f"{x:.6f}" for x in r[:7]) + "\n")
    print(f"wrote {len(variants)} windows to {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--traces", nargs="*", type=Path,
                    default=sorted(Path("bringup/traces").glob("*_*")))
    ap.add_argument("--roll-sweep", action="store_true")
    ap.add_argument("--stress", action="store_true")
    ap.add_argument("--dump-windows", type=Path, metavar="CSV",
                    help="export windows for tests/gesture_parity.cpp")
    args = ap.parse_args()

    reps = [r for r in windows(args.traces) if not r[3]]

    if args.roll_sweep:
        print("roll   correct")
        for deg in range(-60, 61, 5):
            ok = sum(classify(features(roll(w, deg))) == TRUTH[g] for _, g, w, _ in reps)
            print(f"{deg:+4d}   {ok}/{len(reps)}")
        return
    if args.stress:
        stress(reps)
        return
    if args.dump_windows:
        dump_windows(reps, args.dump_windows)
        return

    print(f"   {'who':6} {'gesture':11} {'got':10} {'dur':>4} {'peak':>5} {'cum':>4}"
          f" {'planar':>6} {'phi':>6} {'flips':>5} {'smooth':>6} {'y1':>5}")
    ok = 0
    for who, g, w, _ in reps:
        f = features(w)
        got = classify(f)
        ok += got == TRUTH[g]
        print(f"{'  ' if got == TRUTH[g] else 'XX'} {who:6} {g:11} {got:10} {f['dur']:4.2f}"
              f" {f['peak']:5.0f} {f['cum']:4.0f} {f['planar']:6.2f} {f['phi']:6.1f}"
              f" {f['flips']:5d} {f['smooth']:6.2f} {f['y1']:5.0f}")
    print(f"\n{ok}/{len(reps)} correct (reps truncated by capture start/end skipped)")


if __name__ == "__main__":
    main()
