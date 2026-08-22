#!/usr/bin/env python3
"""Host-side capture tool for bringup/MicTest.ino.

Two modes:

  monitor -- live mic level bar, no recording. Use this FIRST, to position
      the board the way it'll sit in the wand and confirm your voice
      registers at real casting distance (arm's length, mic pointed away
      from your mouth) rather than at desk-close range.

  session -- guided recording run. Walks a word list, N reps each, shows
      progress, and flags takes that clipped or came out too quiet so you
      can redo them on the spot instead of discovering it at training time.

Usage:
    python tools/capture_audio.py COM5 monitor
    python tools/capture_audio.py COM5 session --speaker daniel
    python tools/capture_audio.py COM5 session --speaker yuval --reps 60
    python tools/capture_audio.py COM5 session --speaker daniel --words nox --reps 40

Output goes to bringup/traces_audio/<date>_<speaker>/<label>_NN.wav,
mirroring the bringup/traces/<date>_<speaker>/ layout the gesture CSVs
already use.

Exists because MicTest.ino streams raw binary PCM, not text CSV like
ImuTest -- there's no way to eyeball or copy-paste this data by hand, so
unlike the IMU traces (captured without any tools/ script so far), audio
capture needs a real host-side counterpart from the start. See CLAUDE.md
Roadmap step 1.
"""

import argparse
import datetime
import re
import struct
import sys
import wave
from pathlib import Path

import serial

CAPTURE_HEADER_RE = re.compile(rb"^CAPTURE,(\d+),(\d+),(\d+)$")
LEVEL_RE = re.compile(rb"^LEVEL,(\d+),(\d+)$")

# The five spell words plus the negative classes. Vocabulary is closed per
# CLAUDE.md (2026-08-13): five incantations plus a noise/other class.
#   silence -- room tone, say nothing at all
#   other   -- ordinary speech/handling noise that must NOT trigger a spell
DEFAULT_WORDS = [
    "lumos",
    "nox",
    "expelliarmus",
    "avada_kedavra",
    "expecto_patronum",
    "silence",
    "other",
]

# Measured silent-room noise floor at MicTest's MIC_GAIN=40 (2026-08-22,
# nothing playing, 20 s sample): min 232 / median 678 / p90 1062 / max 1367.
# That ~1400 ceiling is the mic's self-noise plus room ambience, and every
# threshold below is placed relative to it. Re-measure with `monitor` if the
# gain changes or the room does.
NOISE_FLOOR_MAX = 1400

# int16 full scale is 32767.
CLIP_PEAK = 30000

# The boundary between "room tone" and "something actually happened", set
# ~2x above the loudest observed room tone. A spell take below it probably
# missed the word; a silence take above it caught something real.
SPEECH_MIN_PEAK = 3000
SILENCE_MAX_PEAK = 3000


def read_line(ser: serial.Serial) -> bytes:
    """Read one \\n-terminated line, stripped of the trailing newline(s)."""
    return ser.readline().rstrip(b"\r\n")


def next_take_path(outdir: Path, label: str) -> Path:
    n = 1
    while True:
        path = outdir / f"{label}_{n:02d}.wav"
        if not path.exists():
            return path
        n += 1


def take_count(outdir: Path, label: str) -> int:
    return len(list(outdir.glob(f"{label}_*.wav")))


def do_capture(ser: serial.Serial, outdir: Path, label: str):
    """Trigger one recording, write it to a WAV, return (path, peak, samples)."""
    ser.reset_input_buffer()
    ser.write(b"r")
    print("   >>> SPEAK NOW <<<", flush=True)

    while True:
        line = read_line(ser)
        if not line:
            continue
        m = CAPTURE_HEADER_RE.match(line)
        if m:
            num_samples, sample_rate, channels = (int(x) for x in m.groups())
            break
        if line.startswith(b"#") and b"RECORDING" not in line:
            print(line.decode(errors="replace"))

    payload = ser.read(num_samples * 2)
    if len(payload) != num_samples * 2:
        print(
            f"! short read: expected {num_samples * 2} bytes, got {len(payload)}",
            file=sys.stderr,
        )
        num_samples = len(payload) // 2
        payload = payload[: num_samples * 2]

    # Drain the trailing blank line + "# CAPTURE END" marker.
    read_line(ser)
    read_line(ser)

    samples = struct.unpack(f"<{num_samples}h", payload)
    peak = max((abs(s) for s in samples), default=0)

    path = next_take_path(outdir, label)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)  # 16-bit PCM, matches int16_t samples on the wire
        w.setframerate(sample_rate)
        w.writeframes(payload)

    return path, peak, num_samples


def quality_note(peak: int, label: str) -> str:
    """Return a warning string for a suspect take, or '' if it looks fine."""
    if peak >= CLIP_PEAK:
        return f"CLIPPED (peak {peak}) -- back off or lower gain, redo this one"
    if label == "silence":
        if peak >= SILENCE_MAX_PEAK:
            return f"not silent (peak {peak}) -- something made noise, redo"
        return ""
    if peak < SPEECH_MIN_PEAK:
        return (f"barely above room tone (peak {peak}, floor ~{NOISE_FLOOR_MAX}) "
                f"-- did it catch you? redo")
    return ""


def run_monitor(ser: serial.Serial) -> None:
    print("Live mic level. Position the board as it'll sit in the wand and talk")
    print("from real casting distance. Ctrl+C to stop and print a summary.\n")
    print(f"reference: silent-room floor measured at max ~{NOISE_FLOOR_MAX}; "
          f"aim for speech peaks {SPEECH_MIN_PEAK}-{CLIP_PEAK}\n")
    peaks = []
    try:
        while True:
            line = read_line(ser)
            m = LEVEL_RE.match(line) if line else None
            if not m:
                continue
            peak = int(m.group(2))
            peaks.append(peak)
            bar_len = min(50, peak * 50 // 32767)
            flag = "  CLIP" if peak >= CLIP_PEAK else ""
            print(f"\r{peak:6d} |{'#' * bar_len:<50}|{flag}", end="", flush=True)
    except KeyboardInterrupt:
        pass

    print("\n")
    if not peaks:
        return
    peaks.sort()
    n = len(peaks)
    print(f"samples={n}  min={peaks[0]}  p50={peaks[n // 2]}  "
          f"p90={peaks[int(n * 0.9)]}  max={peaks[-1]}")
    if peaks[-1] >= CLIP_PEAK:
        print("-> clipping: lower MIC_GAIN in MicTest.ino")
    elif peaks[-1] < SPEECH_MIN_PEAK:
        print("-> nothing above room tone was captured "
              "(if you were speaking, raise MIC_GAIN)")
    elif peaks[-1] < 6000:
        print("-> speech is registering but low; consider raising MIC_GAIN")
    else:
        print("-> levels look usable for recording")


def run_session(ser: serial.Serial, outdir: Path, words, reps: int) -> None:
    print(f"Recording to {outdir}")
    print(f"{reps} reps per word, {len(words)} words: {', '.join(words)}\n")
    print("Enter = record   s = skip this rep   n = next word   q = quit\n")

    for label in words:
        already = take_count(outdir, label)
        if already >= reps:
            print(f"[{label}] already has {already} takes, skipping\n")
            continue

        if label == "silence":
            print(f"\n=== {label} === say NOTHING; just capture room tone")
        elif label == "other":
            print(f"\n=== {label} === ordinary speech, NOT spell words "
                  f"(count, chat, read something)")
        else:
            print(f"\n=== {label} === vary speed, loudness and intonation between takes")

        while True:
            done = take_count(outdir, label)
            if done >= reps:
                print(f"[{label}] done: {done}/{reps}\n")
                break

            cmd = input(f"  {label} {done + 1}/{reps} > ").strip().lower()
            if cmd == "q":
                print("stopping.")
                return
            if cmd == "n":
                print()
                break
            if cmd == "s":
                continue

            path, peak, n = do_capture(ser, outdir, label)
            note = quality_note(peak, label)
            status = f"   {path.name}  peak={peak}"
            if note:
                print(f"{status}   !! {note}")
            else:
                print(f"{status}   ok")

    print("\nSession complete.")
    for label in words:
        print(f"  {label:20s} {take_count(outdir, label)} takes")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="serial port, e.g. COM5")
    parser.add_argument("mode", choices=["monitor", "session"])
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--speaker", help="speaker name, used in the session dir name")
    parser.add_argument("--reps", type=int, default=50, help="takes per word (default 50)")
    parser.add_argument("--words", nargs="+", default=DEFAULT_WORDS,
                        help="words to record (default: the full spell + negative set)")
    parser.add_argument("--outdir", default="bringup/traces_audio")
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=5)
    ser.reset_input_buffer()

    try:
        if args.mode == "monitor":
            run_monitor(ser)
            return

        if not args.speaker:
            parser.error("session mode needs --speaker")

        today = datetime.date.today().isoformat()
        outdir = Path(args.outdir) / f"{today}_{args.speaker}"
        outdir.mkdir(parents=True, exist_ok=True)
        run_session(ser, outdir, args.words, args.reps)
    except (KeyboardInterrupt, EOFError):
        print()
    finally:
        ser.close()


if __name__ == "__main__":
    main()
