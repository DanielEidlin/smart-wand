# Implementation plan

Plan of record. `CLAUDE.md` holds the specification — hardware, spell table, gesture
discriminators, board gotchas, electrical constraints. This file holds the **sequencing**: what
gets built in what order, and why. Where the two overlap, `CLAUDE.md` wins; this file points at it
rather than restating it.

Phase overview is in `CLAUDE.md` under **Roadmap**. Phase 1 is broken down here because it is
in progress.

## Phase 1 — spell spec + bench bring-up

**Goal: collect calibration data while the wand is still open.** Nothing here classifies a
gesture. `CLAUDE.md` is explicit that gesture recognition must not be written before real traces
exist to tune against, and re-recording after the wand is epoxied shut is the thing to avoid.

### Task 1 — record the spell spec ✅ done 2026-08-13

The vocabulary, the cast trigger, and the five gesture shapes now live in `CLAUDE.md` under
**Spells**; the prior art behind them is in `docs/spell-spec.md`. Decisions closed: incantation
list, LED pin (`D0`), double-tap arming, protected cell plus a latching ~3.0 V firmware floor.

### Task 2 — state machine skeleton, `SmartWand/`

`SmartWand.ino`, `config.h`, `gestures.{h,cpp}`, `effects.{h,cpp}`, `power.{h,cpp}`.
States `IDLE → ARMED → LISTENING → CASTING`, with `LISTENING` a no-op stub until Phase 4.

- **`IDLE`** — MCU asleep, woken by IMU tap + wake-on-motion on `PIN_LSM6DS3TR_C_INT1 (18)`.
- **`ARMED`** — entered on double-tap. **Give visible feedback** (a dim pulse) so the user knows
  the tap registered, then hold a ~2 s window for a gesture before falling back to `IDLE`.
- **`CASTING`** — play the matched effect. Lumos is the one state that persists indefinitely;
  everything else self-terminates back to `IDLE`.

Gesture classification is a **stub returning "no match"** at this stage — it is Phase 2 work and
blocked on task 3's data. Keep the `GestureEngine` seam (`sample ring buffer → (GestureId,
confidence)`) so the heuristic → DTW → ML rungs stay swappable.

`power.cpp` owns the low-voltage floor: sample battery voltage on a slow cadence (seconds, not
milliseconds), extinguish Lumos below the floor, and **latch** — refuse to relight until charge.
See **Electrical constraints** in `CLAUDE.md` for why latching is mandatory.

All pins and thresholds in `config.h`; `millis()` state machines only, no `delay()`.

### Task 3 — bring-up sketches, `bringup/`

Each in its own directory matching the `.ino` name.

- **`TapTest/`** — **run this first.** It is load-bearing for the arming decision: verify hardware
  double-tap fires on `INT1`, and tune threshold and the double-tap window **through a wand-shaped
  object**, not a bare board. Note the false-positive rate from ordinary handling. Register recipe
  and traps are in `CLAUDE.md` **Board gotchas**; `Seeed_Arduino_LSM6DS3`'s `FreeFallDetect`
  example is the template. **A negative result reopens the cast-trigger decision**, which is why
  it runs before anything is built on top of it.
- **`ImuTest/`** — the important one. Stream accel+gyro as CSV over serial at fixed ODR (start
  104 Hz) with keypress labelling to tag traces. Must also **establish the board-axis → wand-axis
  mapping empirically** — every gesture threshold depends on getting this right, and it depends on
  how the board sits in the bore. `PIN_LSM6DS3TR_C_POWER (15)` HIGH, and the IMU is on `Wire1`.
- **`LedTest/`** — WS2812B on `D0`: primary colours, brightness sweep, and the five candidate
  effects — including finding a blue-silver that reads as *silver* on a single pixel behind epoxy
  rather than as plain blue. **Characterise behaviour on `BAT+` from 4.2 V down toward 3.3 V**;
  the part is specified 3.5–5.3 V and this is the open electrical risk. Measure two currents, both
  load-bearing now that Lumos never times out: **draw at the chosen brightness cap** (sets lit
  runtime) and **quiescent draw with the pixel written to `0,0,0`** (the driver IC still pulls
  roughly 1 mA when dark — if that holds it dominates idle current and makes further MCU sleep
  optimisation pointless until the LED can be power-gated).
- **`BatteryTest/`** — `VBAT_ENABLE (14)` LOW, `analogReference(AR_INTERNAL_3_0)`,
  `analogReadResolution(12)`, read `PIN_VBAT (32)`. Calibrate the divider constant against a
  multimeter. Calibrate the ~3.0 V floor **under LED load**, not on a resting cell. Also
  **establish whether the 14500 in hand is a protected cell** — it decides whether the firmware
  floor is a second line of defence or the only guard against destroying it.
- **`MicTest/`** — dump raw PDM audio over serial, `PIN_PDM_PWR (19)` HIGH. Record ~50 utterances
  each of the five spell words plus a noise/other class, varying distance, volume and speed. Extra
  samples for the two known weak spots: the *Expelliarmus* / *Expecto Patronum* pair, which share
  their first two syllables, and *Nox*, which is short enough to need more examples. Capture
  deliberate near-misses for Nox ("not", "knocks", "no") into the noise class — that is where its
  false triggers will come from.

### Task 4 — host-side capture scripts, `tools/`

Plain Python on the laptop, not on the wand.

- `capture_traces.py` — read serial, write labelled CSV per gesture, one file per session. Output
  directly ingestible by Edge Impulse in case Phase 2 escalates to a model.
- `capture_audio.py` — reassemble the raw PDM stream into per-utterance WAV files.

### Exit criteria

Phase 1 closes when all six hold:

1. Double-tap fires reliably **through a wand-like enclosure**, with the false-positive rate from
   ordinary handling written down.
2. `ImuTest` produces clean CSV with no dropped samples, and the axis mapping is documented.
3. Several traces per gesture recorded **from more than one person**, so Phase 2 tunes against
   real variation rather than one person's idealised motion.
4. LED colours correct, and behaviour as supply sags toward 3.3 V is **measured and written
   down** — this decides whether a diode drop or level shifter is needed. Lit-at-cap and
   dark-quiescent currents recorded, giving a real figure for how long a Lumos-lit wand survives.
5. Battery voltage matches a multimeter across a charge/discharge range, and whether the cell is
   protected is established.
6. At least 50 utterances per keyword captured and playable as WAV, with the extra sets for Nox
   and the *Exp-* pair.

### Verification

The board is **not connected yet**, so compile first and treat upload as a separate step.

```bash
arduino-cli compile --fqbn Seeeduino:nrf52:xiaonRF52840Sense bringup/TapTest
arduino-cli compile --fqbn Seeeduino:nrf52:xiaonRF52840Sense bringup/ImuTest
arduino-cli compile --fqbn Seeeduino:nrf52:xiaonRF52840Sense bringup/LedTest
arduino-cli compile --fqbn Seeeduino:nrf52:xiaonRF52840Sense bringup/BatteryTest
arduino-cli compile --fqbn Seeeduino:nrf52:xiaonRF52840Sense bringup/MicTest
arduino-cli compile --fqbn Seeeduino:nrf52:xiaonRF52840Sense SmartWand

arduino-cli board list                     # find the port before first upload
arduino-cli upload -p COM<N> --fqbn Seeeduino:nrf52:xiaonRF52840Sense bringup/ImuTest
arduino-cli monitor -p COM<N> -c baudrate=115200
```

Check reported flash/RAM on every compile as a routine habit — Phase 4 needs headroom for a model.
If a port vanishes or upload fails, double-tap RESET for the UF2 bootloader (a `XIAO-SENSE` drive
appears) and retry.

## Phases 2–4

See **Roadmap** in `CLAUDE.md`. Phase 2 (gesture recognition) is blocked on Phase 1's traces and
is **heuristic-first** — windowed features and thresholds, escalating to DTW then Edge
Impulse/TFLite-Micro only when the evidence demands it. Segmentation, deciding where a gesture
starts and ends, is shared by all three rungs and is the harder half. Phase 4 (incantations) is
where the ML budget belongs, since no heuristic exists for keyword spotting.
