# smart-wand

A spell-reactive magic wand: motion gestures trigger RGB lighting effects at the wand tip.
Spoken incantations are planned as a later phase — gesture-only ships first, but the
firmware must not foreclose voice. Arduino/C++ on a Seeed XIAO nRF52840 Sense.

## Audience

The author is an experienced full-stack developer and new to embedded work. **Gloss every
embedded, hardware, or electronics term in one short sentence the first time it appears in
a reply** — parts, protocols, register/pin concepts, electrical units, fabrication steps
(e.g. "ODR (output data rate — how many samples per second the sensor produces)"). Do not
explain general programming terms; those land as condescending.

## Hardware

| Role | Part | Notes |
| --- | --- | --- |
| MCU | Seeed XIAO nRF52840 **Sense** | nRF52840 Cortex-M4F @64 MHz, 1 MB flash, 256 KB RAM, 2 MB QSPI flash (P25Q16H) |
| IMU | LSM6DS3TR-C (onboard) | 6-axis accel + gyro, gesture source |
| Mic | PDM MEMS (onboard) | Unused until Phase 4 (incantations). Keep its pins and power budget reserved. |
| Tip LED | WS2812B Mini RGB board | 10 mm round tile, single addressable pixel |
| Battery | 14500 Li-ion 3.7 V 1000 mAh | In a 1-slot AA/14500 holder (no direct soldering to cell) |
| Switch | SS12D00 1P2T slide | Breaks the battery line into BAT+ |
| Consumables | 30 AWG silicone wire, heat shrink, epoxy, hot glue | 20 mm wand bore — no pin headers fit |

Wiring: battery → slide switch → `BAT+`/`BAT-` pads on the XIAO underside. LED taps board
power and one digital pin for data. USB-C charges the cell through the onboard BQ25101.

## Toolchain

`arduino-cli` (v1.5.2) — already configured on this machine. Do **not** assume the Arduino IDE.

**Arduino C++ everywhere, including throwaway bring-up sketches** (decided 2026-08-13). The
board does have an official CircuitPython build and it would iterate faster during Phase 1,
but it was rejected: Edge Impulse and TFLite-Micro deploy only as C++, so Python forecloses
the Phase 4 voice path outright; the CircuitPython VM idles at milliamps against a
microamp-scale budget; and its deep sleep restarts `code.py`, which cannot carry the
`IDLE → ARMED → LISTENING → CASTING` state machine across a wake. A C++-only bring-up phase
was chosen over a CircuitPython/C++ hybrid to keep to one toolchain and one mental model.
Don't re-propose Python; do expect Phase 1 to need host-side scripts for serial capture,
since there is no USB-drive filesystem to drop CSV and WAV files onto.

```
FQBN:  Seeeduino:nrf52:xiaonRF52840Sense
Core:  Seeeduino:nrf52 @ 1.1.13   (via files.seeedstudio.com board manager URL)
Libs:  Adafruit NeoPixel 1.10.6, Seeed Arduino LSM6DS3 2.0.3
Sketchbook: C:\Users\danie\OneDrive\Documents\Arduino
```

```bash
arduino-cli board list                                          # find the port
arduino-cli compile --fqbn Seeeduino:nrf52:xiaonRF52840Sense <sketch-dir>
arduino-cli upload -p COM<N> --fqbn Seeeduino:nrf52:xiaonRF52840Sense <sketch-dir>
arduino-cli monitor -p COM<N> -c baudrate=115200
```

The board is **not connected yet**; run `board list` before the first upload rather than
guessing the port. If upload fails or the port vanishes, **double-tap RESET** to enter the
UF2 bootloader (a `XIAO-SENSE` drive appears), then retry.

## Board gotchas

These are verified against the installed core (`variants/Seeed_XIAO_nRF52840_Sense/`), not
from memory. They cost hours if you get them wrong.

- **Onboard RGB LED is active-LOW.** `digitalWrite(LED_RED, LOW)` lights it. `variant.h`
  defines `LED_STATE_ON (1)`, which is wrong — `initVariant()` writes all three HIGH to
  turn them off. Pins: `LED_RED=11`, `LED_GREEN=13`, `LED_BLUE=12`.
- **The IMU is on `Wire1`, not `Wire`.** The Seeed LSM6DS3 library handles this itself via
  `#define Wire Wire1` under `TARGET_SEEED_XIAO_NRF52840_SENSE`. If you talk to the IMU
  directly, use `Wire1`. Address `0x6A`, `WHO_AM_I` also reads `0x6A`.
- **The IMU has a power-enable pin:** `PIN_LSM6DS3TR_C_POWER (15)` must be driven HIGH
  before the IMU responds. Interrupt on `PIN_LSM6DS3TR_C_INT1 (18)` — use this for
  wake-on-motion instead of polling.
- **The IMU has no Machine Learning Core.** The LSM6DS3TR-C is not an MLC part — ST ships
  that block on the LSM6DSOX/LSM6DSV, and publishes MLC application notes only for those.
  Verified against the register map in `Seeed_Arduino_LSM6DS3/LSM6DS3.h`: registers stop at
  `0x5F` with one embedded-functions bank, no `MLC*`/`EMB_FUNC*` registers. On-sensor
  classification is therefore off the table; any ML runs in software on the M4F. What the
  part *does* offer, all usable while the MCU sleeps: wake-up/activity, single & double tap,
  6D/4D orientation, free-fall, pedometer, significant motion, sensor hub.
- **Run the IMU FIFO continuously — wake-on-motion alone loses the start of every gesture.**
  By the time INT1 fires, the opening tens of milliseconds have already happened, and that
  is exactly the part that separates a jab from a swish. The 4 KB FIFO retains pre-trigger
  samples; drain it on wake. It also lets the MCU wake at ~10 Hz instead of once per sample.
- **The PDM mic has a power-enable pin too:** `PIN_PDM_PWR (19)`, clock `20`, data `21`.
- **Battery sense is disabled at boot.** `initVariant()` drives `VBAT_ENABLE (14)` HIGH,
  which *disables* reading. Drive it LOW, then read `PIN_VBAT (32)`. Divider is 1 MΩ/510 kΩ,
  so roughly `vbat = analogRead(PIN_VBAT) * (3.0/4096) * (1510.0/510.0)` with
  `analogReference(AR_INTERNAL_3_0)` and `analogReadResolution(12)` — **calibrate against a
  multimeter**, don't trust the constant.
- **Charge current defaults to 50 mA.** `initVariant()` leaves `PIN_CHARGING_CURRENT (22)`
  as INPUT (high-Z) = 50 mA, which is a ~20 hour charge for a 1000 mAh cell. Drive it LOW
  as OUTPUT for 100 mA if that's too slow.
- Free digital pins: `D0`–`D3` (also `A0`–`A3`). Avoid `D4`/`D5` (I2C), `D6`/`D7` (UART),
  `D8`–`D10` (SPI) unless you're deliberately reusing them.

## Electrical constraints

- **WS2812B on a 3.7 V supply is the main open risk.** The part is specified 3.5–5.3 V. Off
  `BAT+` it sees 4.2 V falling to ~3.3 V as the cell drains — expect dimming and color
  shift near end of charge, and possible dropout below 3.5 V. Off the regulated `3V3` pin
  it is out of spec from the start. Test this early on the bench before committing to a
  wiring plan; a single-diode drop or a level shifter on DIN are the usual fixes.
- One WS2812B at full white is ~60 mA — comparable to the whole MCU's active draw. Cap
  brightness and keep effects short; this dominates the battery budget.
- 30 AWG is ~0.05 Ω per 10 cm. Fine for signal and for one LED, but keep the LED power
  wires short.

## Firmware conventions

- Sketch directory name **must** match the `.ino` filename (arduino-cli requirement).
- Never use `delay()` in animation or gesture code — it stalls sampling and BLE. Use
  `millis()`-based state machines. `delay()` is acceptable only in throwaway bring-up sketches.
- Keep gesture thresholds and pin assignments in a single `config.h`, not scattered as
  literals — they will be retuned constantly during calibration.
- Prefer fixed-size buffers and avoid heap churn in the sample path. 256 KB RAM is roomy,
  but fragmentation on a long-running battery device is not worth the risk.
- Idle power is a first-class requirement (target 8–12+ h). Prefer IMU wake-on-interrupt
  over a polling loop, and gate the mic/IMU power pins when unused.

## Intended layout

Nothing but `README.md` exists yet. Planned structure:

```
smart-wand/
├── CLAUDE.md
├── docs/implementation-plan.md   # the source plan document
├── SmartWand/                    # main firmware sketch
│   ├── SmartWand.ino
│   ├── config.h                  # pins, thresholds, tunables
│   ├── gestures.{h,cpp}
│   ├── effects.{h,cpp}
│   └── power.{h,cpp}
└── bringup/                      # Phase 1 throwaway test sketches
    ├── LedTest/LedTest.ino
    ├── ImuTest/ImuTest.ino
    └── BatteryTest/BatteryTest.ino
```

## Roadmap

1. **Bench bring-up** — LED colors/patterns on flying leads; stream IMU data over serial to
   collect real gesture traces; verify battery sense and WS2812B behavior at 3.7 V.
   **Also dump raw PDM audio to serial and record incantation samples while the board is on
   the bench** — this is nearly free now and annoying to redo once the wand is epoxied shut.
2. **Firmware** — gesture recognition from accel+gyro; map gestures to spell animations
   (*Lumos*, *Expelliarmus*, …); idle power optimization.
3. **Assembly** — solder 30 AWG to castellated pads, heat shrink every joint, epoxy the LED
   into the tip as a diffuser, hot-glue the stack into the 20 mm bore. Keep the USB-C port
   and switch lever accessible.
4. **Incantations (deferred)** — gesture-gated keyword spotting on the PDM mic. See below.

Phase 1 exists to gather calibration data. Don't write gesture-classification logic before
there are recorded traces to tune against.

**Gestures are heuristic-first, not ML-first** (decided 2026-08-13). With no MLC on the IMU,
a model would run on the MCU with no power advantage over hand-written logic, so it has to
win on accuracy alone — and a small vocabulary of deliberately dissimilar gestures does not
need it. Escalate only when the evidence demands it: windowed features and thresholds → DTW
template matching → Edge Impulse/TFLite-Micro. Triggers for moving up a rung are more than
~6 gestures, gestures that resemble each other, or false positives from ordinary handling.
Keep a `GestureEngine` seam that takes a sample ring buffer and returns
`(GestureId, confidence)` so the rungs are swappable. Segmentation — deciding where a
gesture starts and ends — is shared by all three and is the harder half of the problem.
The project's ML budget belongs to Phase 4 keyword spotting, where no heuristic exists.

## Designing for voice (Phase 4, deferred)

Voice is wanted but explicitly **not** in the first build. It is feasible on this chip — the
Cortex-M4F has DSP instructions, CMSIS-NN is available, and the XIAO nRF52840 Sense is a
first-class Edge Impulse target with an existing keyword-spotting path. A small KWS model
(a handful of keywords, 16 kHz mono, MFCC features) is on the order of tens of KB of flash
plus a tensor arena in the tens of KB — comfortable against 1 MB / 256 KB. Measure rather
than trust those figures.

Four constraints to honor **now**, so Phase 4 is additive instead of a rewrite:

- **Gate the mic on a gesture; never listen continuously.** Always-on wake-word detection
  keeps the MCU out of low-power states and will destroy the 8–12 h runtime target.
  "Raise the wand, then speak" is both the natural interaction and the cheap one. The
  gesture engine becomes the trigger for the audio path.
- **Model a spell as `(gesture, optional incantation)` from the very first commit.** If the
  spell table is keyed on gesture alone, adding a word later means reworking every call
  site. Leave the field present and unused.
- **Structure the firmware as an explicit state machine** with `IDLE → ARMED → LISTENING →
  CASTING` states. `LISTENING` is a no-op stub in Phases 1–3 and gets filled in at Phase 4.
- **Keep flash and RAM headroom.** Don't let gesture code, animation tables, or logging
  sprawl to fill the chip. Check `compile` output for usage as a routine habit.

The 2 MB onboard QSPI flash is a good home for models and recorded samples, and is otherwise
unused — don't repurpose it casually.

## Open decisions

- **Which incantations, and how many?** Vocabulary size drives model size and training
  effort. Worth fixing the word list early even though Phase 4 is far off, since Phase 1
  should record samples for exactly those words.
- **Which pin drives the LED?** Not yet specified. `D0` is the suggested default — corner
  castellated pad, easy to solder, no peripheral conflict.
- **Is BLE used?** The nRF52840 supports it and the plan never mentions it. It costs power
  and complexity; worth having only if there's a use (config, OTA, wand-to-wand duels).
- **How many pixels at the tip?** The plan says one WS2812B. More would allow richer
  effects at a real power cost.
