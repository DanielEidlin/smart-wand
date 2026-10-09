#!/usr/bin/env python3
"""Guided, button-gated gesture capture for bringup/ImuTest.ino.

Walks a gesture list, N casts each. For every cast: hold the cast button,
perform the gesture, release. Each gesture gets one CSV in
bringup/traces/<date>_<speaker>/<gesture>.csv holding the full sample stream
(including the still time between casts), with a `cast` column numbering
each press from 1 within the file and 0 between presses. gesture_lab.py
splits on that column, so no segmentation heuristic is involved.

After each release it prints the cast's length, peak rotation, and what
gesture_lab currently classifies it as -- a sanity check while you record,
not a target to perform towards. Record the motion you'd naturally make.

Presses shorter than MIN_CAST_MS are treated as accidental: they are kept in
the file but with cast=0, so they never count as a cast.

Resumable: progress is counted from casts already in the file, so Ctrl+C any
time and rerun the same command to continue. To redo a gesture from scratch,
delete its CSV.

Usage:
    python tools/capture_traces.py COM5 --speaker daniel
    python tools/capture_traces.py COM5 --speaker alise --casts 15
    python tools/capture_traces.py COM5 --speaker daniel --gestures zigzag circle

`idle` means: hold the button and keep the wand still (or speak, without
gesturing) -- the "pressed but no gesture" case the classifier must reject.
"""

import argparse
import csv
import datetime
import time
from pathlib import Path

import serial

import gesture_lab

GESTURES = ["flick_up", "flick_down", "thrust", "zigzag", "circle", "idle"]
LABELS = {"flick_up": "u", "flick_down": "d", "thrust": "t", "zigzag": "z",
          "circle": "c", "idle": "i"}   # 'i', not '0': '0' clears the label
HEADER = ["millis", "label", "ax", "ay", "az", "gx", "gy", "gz", "cast"]
MIN_CAST_MS = 150


def casts_in(path):
    if not path.exists():
        return 0
    with open(path, newline="") as f:
        return max((int(r["cast"]) for r in csv.DictReader(f)), default=0)


def summarize(rows):
    w = [[float(r[0])] + [float(x) for x in r[2:8]] + [1] for r in rows]
    f = gesture_lab.features(w)
    return (f"{f['dur']:.2f} s, peak {f['peak']:.0f} deg/s, "
            f"classified as {gesture_lab.classify(f)}")


def record(ser, path, gesture, target):
    done = casts_in(path)
    if done >= target:
        print(f"{gesture}: already {done}/{target}, skipping")
        return
    print(f"\n=== {gesture}: {done}/{target} done. Hold the button, "
          f"{'keep still' if gesture == 'idle' else 'perform'}, release. ===")

    ser.write(b"#" + LABELS[gesture].encode())   # reset firmware count, set label
    new = not path.exists()
    with open(path, "a", newline="") as f:
        out = csv.writer(f)
        if new:
            out.writerow(HEADER)
        pending = []
        while done < target:
            line = ser.readline().decode(errors="replace").strip()
            if not line or line.startswith("#") or line.startswith("millis"):
                continue
            row = line.split(",")
            if len(row) != len(HEADER):
                print(f"  ! skipping malformed line: {line!r}  (old firmware without "
                      f"the cast column? reflash bringup/ImuTest)")
                continue
            cast = int(row[8])
            if cast:
                pending.append(row)
                continue
            if pending:   # just released: decide whether it counts
                held = int(pending[-1][0]) - int(pending[0][0])
                if held >= MIN_CAST_MS:
                    done += 1
                    for r in pending:
                        out.writerow(r[:8] + [done])
                    print(f"  cast {done}/{target}: {summarize(pending)}")
                else:
                    out.writerows(r[:8] + [0] for r in pending)
                    print(f"  (ignored a {held} ms tap)")
                pending = []
            out.writerow(row[:8] + [0])
        f.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--speaker", required=True)
    ap.add_argument("--casts", type=int, default=10)
    ap.add_argument("--gestures", nargs="+", default=GESTURES, choices=GESTURES)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--outdir", type=Path, default=Path("bringup/traces"))
    args = ap.parse_args()

    outdir = args.outdir / f"{datetime.date.today().isoformat()}_{args.speaker}"
    outdir.mkdir(parents=True, exist_ok=True)

    ser = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(0.3)   # opening the port releases the sketch's while (!Serial)
    ser.reset_input_buffer()
    try:
        for g in args.gestures:
            record(ser, outdir / f"{g}.csv", g, args.casts)
        print(f"\nDone. Check them with: python tools/gesture_lab.py --traces {outdir}")
    except KeyboardInterrupt:
        print("\nStopped. Rerun the same command to resume.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
