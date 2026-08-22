#!/usr/bin/env python3
"""Host-side capture tool for bringup/MicTest.ino.

Three modes:

  monitor -- live mic level bar, no recording. Use this FIRST, to position
      the board the way it'll sit in the wand and confirm your voice
      registers at real casting distance (arm's length) rather than at
      desk-close range.

  calibrate -- measures the noise floor and your speech level, reports the
      in-band SNR, and checks for clipping. Re-run after a MIC_GAIN change.

  session -- guided recording run. Walks a word list, N reps each, shows
      progress, and flags takes that clipped or came out too quiet so you
      can redo them on the spot instead of discovering it at training time.
      Each take is started AND stopped by you (Enter, then Enter), mirroring
      the cast button's press/release, so a take is as long as the
      performance needs.

Usage:
    python tools/capture_audio.py /dev/cu.usbmodem101 monitor
    python tools/capture_audio.py /dev/cu.usbmodem101 calibrate
    python tools/capture_audio.py /dev/cu.usbmodem101 session --speaker daniel
    python tools/capture_audio.py /dev/cu.usbmodem101 session --speaker yuval --reps 60
    python tools/capture_audio.py /dev/cu.usbmodem101 session --speaker daniel --words nox --reps 40

Output goes to bringup/traces_audio/<date>_<speaker>/<label>_NN.wav,
mirroring the bringup/traces/<date>_<speaker>/ layout the gesture CSVs
already use.

Exists because MicTest.ino streams raw binary PCM, not text CSV like
ImuTest -- there's no way to eyeball or copy-paste this data by hand, so
unlike the IMU traces (captured without any tools/ script so far), audio
capture needs a real host-side counterpart from the start. See CLAUDE.md
Roadmap step 1.

EVERY level judgement in here is made on BAND-LIMITED energy, not on raw
sample amplitude. That is not a detail -- see the block comment above the
constants. Judging raw amplitude is what made three calibration sessions
report 9-15 dB SNR for audio that actually carries 24-38 dB (2026-08-22).
"""

import argparse
import datetime
import math
import re
import struct
import sys
import time
import wave
from pathlib import Path

import serial

CAPTURE_HEADER_RE = re.compile(rb"^CAPTURE,(\d+),(\d+),(\d+)$")
# MicTest emits "LEVEL,<ms>,<peak>,<rms>" -- peak and rms are both of the
# HIGH-PASSED signal. Older firmware emitted only "LEVEL,<ms>,<peak>" with no
# filtering at all; the third group is optional so this script still runs
# against a board that hasn't been reflashed, with a warning.
LEVEL_RE = re.compile(rb"^LEVEL,(\d+),(\d+)(?:,(\d+))?$")

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

# ---------------------------------------------------------------------------
# Levels, and why they are all band-limited
#
# Measured 2026-08-22 from six diagnostic takes at arm's length, MIC_GAIN=40:
#
#                          broadband        300-3400 Hz
#   noise floor RMS            375.1                4.9
#   noise floor peak            1430                460
#
# 100% of the noise energy sits BELOW 100 Hz. It is not a DC offset (the mean
# is only -18..-81 counts); it is real infrasonic rumble -- hand tremor, air
# movement, body motion, and the mic's own 1/f noise, none of which anything
# in the chain was high-passing. Speech lives at 300-3400 Hz, which is also
# all an MFCC front-end ever looks at, so that rumble is invisible to the
# Phase 4 model and must be invisible to these thresholds too.
#
# Judging raw amplitude instead measures the rumble and calls it noise. That
# reported 0-4 dB SNR for takes carrying 24-38 dB, and would have rejected a
# perfectly good quiet "nox" (raw peak 895, in-band SNR 31 dB) as silence.
# ---------------------------------------------------------------------------
BAND_LO_HZ = 300
BAND_HI_HZ = 3400

# In-band noise floor, counts RMS. From the two silence takes above.
NOISE_BAND_RMS = 5.0

# A take's level is the loudest 50 ms frame of its band-limited signal, in
# counts RMS, expressed as dB over NOISE_BAND_RMS. Measured on the diagnostic
# takes, which is where these thresholds come from:
#   silence_01       11.5  ->  7.2 dB      expelliarmus_01  376.3 -> 37.6 dB
#   silence_02       46.2 -> 19.3 dB       expelliarmus_02  926.1 -> 45.4 dB
#   (silence_02 caught a real transient)   nox_01           225.6 -> 33.1 dB
#                                          nox_02 (quiet)   175.2 -> 30.9 dB
# 20 dB leaves ~11 dB of margin under the quietest good take.
SPEECH_MIN_SNR_DB = 20.0
SILENCE_MAX_SNR_DB = 22.0

# Clipping IS a raw-amplitude phenomenon -- int16 saturates at 32767 whatever
# the frequency -- so this one is deliberately not band-limited.
CLIP_PEAK = 30000

# Live-meter reference values, in the firmware's own units. These are NOT the
# same scale as NOISE_BAND_RMS above: MicTest only high-passes at 300 Hz, so
# its RMS still includes 3400-8000 Hz, while the WAV analysis here applies the
# full band-pass. Measured on-device at rest, 2026-08-22: peak 45-96, RMS
# 13-21. Don't unify these two constants -- they measure different bands.
LIVE_NOISE_RMS = 15.0
LIVE_BAR_FULL = 1000
LIVE_SPEECH_RMS = 150


def biquad(x, b0, b1, b2, a1, a2):
    """Direct-form-I biquad. Coefficients already normalised by a0."""
    out = [0.0] * len(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, s in enumerate(x):
        y = b0 * s + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, s
        y2, y1 = y1, y
        out[i] = y
    return out


def highpass(x, fc, sr, q=0.7071):
    w0 = 2 * math.pi * fc / sr
    a = math.sin(w0) / (2 * q)
    c = math.cos(w0)
    a0 = 1 + a
    return biquad(x, (1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0,
                  (-2 * c) / a0, (1 - a) / a0)


def lowpass(x, fc, sr, q=0.7071):
    w0 = 2 * math.pi * fc / sr
    a = math.sin(w0) / (2 * q)
    c = math.cos(w0)
    a0 = 1 + a
    return biquad(x, (1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0,
                  (-2 * c) / a0, (1 - a) / a0)


def band_limit(samples, sr):
    return lowpass(highpass(samples, BAND_LO_HZ, sr), BAND_HI_HZ, sr)


def rms(values):
    if not values:
        return 0.0
    return math.sqrt(sum(v * v for v in values) / len(values))


def loudest_frame_rms(samples, sr, frame_ms=50):
    """Loudest 50 ms of the signal, counts RMS.

    A whole-take RMS would be diluted by however much of the 2 s window was
    silence, which makes a short word look quiet for reasons that have
    nothing to do with level -- the same duty-cycle trap that broke the
    speech statistic in run_calibrate().
    """
    n = sr * frame_ms // 1000
    if len(samples) < n:
        return rms(samples)
    return max(rms(samples[i:i + n]) for i in range(0, len(samples) - n + 1, n))


def snr_db(level_rms):
    return 20 * math.log10(max(level_rms, 1e-9) / NOISE_BAND_RMS)


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


# The keypress that triggers a take, and the desk thump it makes, must not
# land inside the recording. collect_peaks() has always discarded a settle
# window for the same reason; do_capture() did not, until 2026-08-22.
CAPTURE_SETTLE_S = 0.4

# A capture is 96 KB and takes several seconds to arrive. serial.Serial's own
# `timeout` is a deadline for the WHOLE read() call, not an idle timeout, so
# `ser.read(96000)` silently returns a partial buffer the moment that deadline
# passes -- which is how three takes landed truncated at ~30 KB and one earlier
# take lost 512 bytes without anyone noticing (2026-08-22). Read in chunks
# instead and only give up after the board has genuinely gone quiet.
CAPTURE_READ_IDLE_S = 3.0


def read_exact(ser: serial.Serial, need: int):
    """Read exactly `need` bytes, tolerating however long the board takes.

    Returns (data, elapsed_seconds). Short only if the stream stalls for
    CAPTURE_READ_IDLE_S with nothing arriving.
    """
    buf = bytearray()
    started = time.time()
    last_progress = started
    while len(buf) < need:
        chunk = ser.read(min(4096, need - len(buf)))
        if chunk:
            buf.extend(chunk)
            last_progress = time.time()
        elif time.time() - last_progress > CAPTURE_READ_IDLE_S:
            break
    return bytes(buf), time.time() - started


def do_capture(ser: serial.Serial, outdir: Path, label: str):
    """Trigger one recording, write it to a WAV.

    Returns (path, peak, num_samples, sample_rate, samples).
    """
    time.sleep(CAPTURE_SETTLE_S)
    ser.reset_input_buffer()
    ser.write(b"r")

    # The operator ends the take, not a timer. This mirrors the cast button:
    # press starts the window, release ends it. No fixed window is correct --
    # "Expecto Patronum" performed with the circle gesture runs 2150-3000+ ms
    # and the circle accepts any number of revolutions, so the utterance has no
    # upper bound. Both fixed windows tried (2 s, then 3 s) truncated every
    # Patronum take while it still scored 42-51 dB SNR (2026-08-22).
    input("   >>> SPEAK NOW <<<   (Enter when done) ")
    ser.write(b"s")

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

    payload, elapsed = read_exact(ser, num_samples * 2)
    if len(payload) != num_samples * 2:
        rate = len(payload) / max(elapsed, 1e-6)
        print(
            f"! short read: expected {num_samples * 2} bytes, got {len(payload)} "
            f"in {elapsed:.1f}s ({rate/1024:.1f} KB/s) -- take is TRUNCATED, redo it",
            file=sys.stderr,
        )
        num_samples = len(payload) // 2
        payload = payload[: num_samples * 2]

    # Drain the trailing blank line + "# CAPTURE END" marker.
    read_line(ser)
    read_line(ser)

    samples = list(struct.unpack(f"<{num_samples}h", payload))
    peak = max((abs(s) for s in samples), default=0)

    path = next_take_path(outdir, label)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)  # 16-bit PCM, matches int16_t samples on the wire
        w.setframerate(sample_rate)
        w.writeframes(payload)

    return path, peak, num_samples, sample_rate, samples


MANIFEST = "takes.csv"


def append_manifest(outdir: Path, row: dict):
    """Append one take's metadata to takes.csv beside the WAVs.

    The condition a take was recorded under (gesture/quiet/loud/...) is not
    recoverable from the audio, and neither is the SNR we compute at capture
    time. Both matter later: if the Phase 4 model turns out to fail on quiet
    speech or on takes recorded while casting, that question can only be asked
    if the answer was written down here.
    """
    path = outdir / MANIFEST
    header = ["file", "label", "tag", "samples", "duration_ms",
              "raw_peak", "inband_snr_db", "recorded_at"]
    exists = path.exists()
    with path.open("a", encoding="utf-8") as f:
        if not exists:
            f.write(",".join(header) + "\n")
        f.write(",".join(str(row[k]) for k in header) + "\n")


def quality_note(samples, sample_rate, peak: int, label: str):
    """Return (snr_db, warning) for a take; warning is '' if it looks fine."""
    level = loudest_frame_rms(band_limit(samples, sample_rate), sample_rate)
    snr = snr_db(level)

    if peak >= CLIP_PEAK:
        return snr, f"CLIPPED (raw peak {peak}) -- back off or lower gain, redo"
    if label == "silence":
        if snr > SILENCE_MAX_SNR_DB:
            return snr, f"not silent ({snr:.0f} dB in-band) -- something made noise, redo"
        return snr, ""
    if snr < SPEECH_MIN_SNR_DB:
        return snr, (f"only {snr:.0f} dB in-band (want >{SPEECH_MIN_SNR_DB:.0f}) "
                     f"-- did it catch you? redo")
    return snr, ""


SETTLE_SECONDS = 2


def collect_levels(ser: serial.Serial, seconds: int):
    """Read LEVEL lines for a fixed duration.

    Returns (levels, peaks, filtered) where `levels` is the band-limited RMS
    per window when the firmware reports it, `peaks` is the raw peak, and
    `filtered` says whether the firmware is the high-passing version.

    Discards the first SETTLE_SECONDS so the keypress that started the phase
    -- and the desk thump it makes through the board -- never lands in the
    measurement.
    """
    print(f"  settling ({SETTLE_SECONDS}s)...", end="", flush=True)
    settle_until = time.time() + SETTLE_SECONDS
    while time.time() < settle_until:
        read_line(ser)
    print("\r" + " " * 30 + "\r", end="", flush=True)

    levels, peaks = [], []
    filtered = True
    ser.reset_input_buffer()
    deadline = time.time() + seconds
    while time.time() < deadline:
        line = read_line(ser)
        m = LEVEL_RE.match(line) if line else None
        if not m:
            continue
        peak = int(m.group(2))
        if m.group(3) is None:
            filtered = False
            level = peak
        else:
            level = int(m.group(3))
        peaks.append(peak)
        levels.append(level)
        remaining = int(deadline - time.time())
        bar_len = min(40, level * 40 // LIVE_BAR_FULL)
        print(f"\r  {remaining:2d}s left  {level:6d} |{'#' * bar_len:<40}|",
              end="", flush=True)
    print()
    return levels, peaks, filtered


def stats(values: list) -> dict:
    values = sorted(values)
    n = len(values)
    return {
        "n": n,
        "min": values[0],
        "p50": values[n // 2],
        "p90": values[int(n * 0.9)],
        "max": values[-1],
    }


def warn_unfiltered():
    print("! This board is running pre-2026-08-22 MicTest firmware: it reports")
    print("! raw peak with no high-pass, so every level below is dominated by")
    print("! sub-100 Hz rumble rather than speech. Reflash bringup/MicTest.")
    print()


def run_calibrate(ser: serial.Serial) -> None:
    """Measure the noise floor and speech level, then report SNR and headroom.

    Deliberately does NOT recommend a gain change on the basis of level. The
    PDM gain is digital -- it scales signal and noise identically, so it
    cannot improve SNR, and no value in the register's range can clip speech
    at arm's length. The only gain verdict worth printing is "you clipped".
    """
    print("Calibration has two phases. Keep the board wherever it will actually")
    print("sit in the wand for BOTH -- moving it between phases invalidates the")
    print("comparison.\n")

    input("Phase 1/2: SILENCE. Be quiet, no music/TV/fan. Enter to start 12 s > ")
    floor_levels, floor_peaks, filtered = collect_levels(ser, 12)
    if not filtered:
        warn_unfiltered()
    floor = stats(floor_levels)
    print(f"  floor (in-band RMS): p50={floor['p50']}  p90={floor['p90']}  "
          f"max={floor['max']}\n")

    input("Phase 2/2: SPEECH. Say the spell words at casting volume and distance,\n"
          "including your LOUDEST realistic delivery. Enter to start 15 s > ")
    speech_levels, speech_peaks, _ = collect_levels(ser, 15)

    # Measure speech only over the windows that actually CONTAIN speech.
    # collect_levels() yields one value per 200 ms window, so a 15 s phase is
    # ~75 windows and a naive p90 is the 8th-largest of them -- which sits in
    # the speech distribution only if at least 8 windows (1.6 s) were loud.
    # Pause longer between words and the 8th-largest window becomes a pause,
    # so the "speech level" slides toward room tone. That made the statistic
    # track talking DURATION rather than level: three runs at a fixed gain
    # drifted 4.5 dB down purely because the delivery got more deliberate
    # (2026-08-22). Gating on presence first makes it a level again.
    gate = max(int(floor["p90"] * 1.5), 1)
    active = [v for v in speech_levels if v > gate]
    duty = 100.0 * len(active) / max(len(speech_levels), 1)

    if len(active) < 10:
        print(f"  only {len(active)} of {len(speech_levels)} windows rose above the")
        print(f"  floor -- too little speech to measure. Talk through more of the")
        print(f"  15 s (pauses are fine, but keep words coming) and rerun.\n")
        return

    speech = stats(active)
    print(f"  speech (in-band RMS): p50={speech['p50']}  p90={speech['p90']}  "
          f"max={speech['max']}   ({duty:.0f}% of windows had speech)\n")

    print("=" * 62)
    snr = 20 * math.log10(max(speech["p90"], 1) / max(floor["p50"], 1))
    peak_p90 = stats(speech_peaks)["p90"]
    headroom_db = 20 * math.log10(32767 / max(peak_p90, 1))
    print(f"In-band SNR (speech p90 over floor p50): {snr:5.1f} dB")
    print(f"Headroom on raw speech peaks:            {headroom_db:5.1f} dB")
    print()

    if max(speech_peaks) >= CLIP_PEAK:
        print("VERDICT: CLIPPING. Lower MIC_GAIN by ~6 (about 3 dB) and rerun.")
    elif snr < 15:
        print("VERDICT: low SNR. Note that raising MIC_GAIN will NOT help -- it")
        print("scales signal and noise identically. Get closer, speak up, or")
        print("find a quieter room.")
    else:
        print("VERDICT: good. Speech sits well clear of the noise floor in the")
        print("band that matters, with no clipping.")
    print()
    print("If the floor moved, update tools/capture_audio.py:")
    print(f"  NOISE_BAND_RMS = {max(floor['p50'], 1)}")
    print("=" * 62)


def run_monitor(ser: serial.Serial) -> None:
    print("Live mic level (band-limited RMS, 300-3400 Hz). Position the board as")
    print("it'll sit in the wand and talk from real casting distance.")
    print("Ctrl+C to stop and print a summary.\n")
    print(f"reference: noise floor ~{LIVE_NOISE_RMS:.0f}; "
          f"speech at arm's length reads {LIVE_SPEECH_RMS}+\n")
    levels = []
    warned = False
    try:
        while True:
            line = read_line(ser)
            m = LEVEL_RE.match(line) if line else None
            if not m:
                continue
            peak = int(m.group(2))
            if m.group(3) is None:
                if not warned:
                    warn_unfiltered()
                    warned = True
                level = peak
            else:
                level = int(m.group(3))
            levels.append(level)
            bar_len = min(50, level * 50 // LIVE_BAR_FULL)
            flag = "  CLIP" if peak >= CLIP_PEAK else ""
            print(f"\r{level:6d} |{'#' * bar_len:<50}|{flag}", end="", flush=True)
    except KeyboardInterrupt:
        pass

    print("\n")
    if not levels:
        return
    s = stats(levels)
    print(f"samples={s['n']}  min={s['min']}  p50={s['p50']}  "
          f"p90={s['p90']}  max={s['max']}")
    live_snr = 20 * math.log10(max(s["max"], 1) / LIVE_NOISE_RMS)
    print(f"loudest window is {live_snr:.0f} dB over the noise floor")


def run_session(ser: serial.Serial, outdir: Path, words, reps: int, tag: str) -> None:
    print(f"Recording to {outdir}")
    if tag:
        print(f"condition tag: {tag}")
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

            path, peak, n, sr, samples = do_capture(ser, outdir, label)
            snr, note = quality_note(samples, sr, peak, label)
            append_manifest(outdir, {
                "file": path.name,
                "label": label,
                "tag": tag or "",
                "samples": n,
                "duration_ms": round(1000 * n / sr),
                "raw_peak": peak,
                "inband_snr_db": round(snr, 1),
                "recorded_at": datetime.datetime.now().isoformat(timespec="seconds"),
            })
            status = f"   {path.name}  {snr:4.0f} dB in-band  (raw peak {peak})"
            if note:
                print(f"{status}   !! {note}")
            else:
                print(f"{status}   ok")

    print("\nSession complete.")
    for label in words:
        print(f"  {label:20s} {take_count(outdir, label)} takes")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="serial port, e.g. /dev/cu.usbmodem101")
    parser.add_argument("mode", choices=["monitor", "calibrate", "session"])
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--speaker", help="speaker name, used in the session dir name")
    parser.add_argument("--reps", type=int, default=50, help="takes per word (default 50)")
    parser.add_argument("--words", nargs="+", default=DEFAULT_WORDS,
                        help="words to record (default: the full spell + negative set)")
    parser.add_argument("--tag", default="",
                        help="condition this batch was recorded under (e.g. gesture, "
                             "quiet, loud, fast). Becomes part of the output directory "
                             "name and a column in takes.csv.")
    parser.add_argument("--outdir", default="bringup/traces_audio")
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=5)
    ser.reset_input_buffer()

    try:
        if args.mode == "monitor":
            run_monitor(ser)
            return

        if args.mode == "calibrate":
            run_calibrate(ser)
            return

        if not args.speaker:
            parser.error("session mode needs --speaker")

        today = datetime.date.today().isoformat()
        name = f"{today}_{args.speaker}"
        if args.tag:
            name += f"_{args.tag}"
        outdir = Path(args.outdir) / name
        outdir.mkdir(parents=True, exist_ok=True)
        run_session(ser, outdir, args.words, args.reps, args.tag)
    except (KeyboardInterrupt, EOFError):
        print()
    finally:
        ser.close()


if __name__ == "__main__":
    main()
