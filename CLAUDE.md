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

## Spells

A spell is `(gesture, optional incantation)` from the first commit — the incantation field
exists and goes unused until Phase 4, and stays **per-spell optional**.

| Spell | Gesture | Incantation (Phase 4) | Effect |
| --- | --- | --- | --- |
| Lumos | Flick up | "Lumos" | Steady warm white, **on indefinitely — no timeout** |
| Nox | Flick down | "Nox" | Fast fade, ends **fully off** (pixel written to `0,0,0`) |
| Expelliarmus | Forward thrust | "Expelliarmus" | Sharp red-white flash, ~300 ms |
| Avada Kedavra | Angular zigzag (Z) | "Avada Kedavra" | Harsh green strobe, ~600 ms |
| Expecto Patronum | Circle, either direction | "Expecto Patronum" | Light blue-silver shimmer, ~2 s |

**Hold a physical button to cast** (decided 2026-08-15, reversing the double-tap decision of
2026-08-13 below). Press = start of cast (gesture + optional incantation recognized while held),
release = end. Reason for the reversal: hardware double-tap detection was fully debugged and
made to work (see `TAP_CFG1`/`TAP_THS_6D` gotcha below — that fix is real and stays documented
in case tap is ever revisited for something else), single-tap detection is completely reliable,
but *pairing* two taps into a deliberate "double" proved genuinely fragile on the bench —
threshold, debounce, and window tuning all interact, cross-axis ringing from one strike can
mimic two, and getting a clean double-tap without dropped or spurious pairings took far longer
than the gesture-recognition problem it was meant to gate. A button is a deterministic signal
with none of that: no timing windows, no false positives, near-trivial firmware (a debounced
`digitalRead`). The cost moves from firmware to mechanical: a momentary tactile switch small
enough to fit the 20 mm bore, reachable during a natural grip without interfering with the
gesture swings (flick/thrust/zigzag/circle all still have to happen one-handed while held), and
a precisely-placed hole in the wand shell, planned before the enclosure is epoxied shut in
**Roadmap** step 3. None of that is hard, just undesigned — and worth doing now, before
anything is glued. See **Hardware** for a candidate part found 2026-08-15, not yet ordered.

**Fit math against the 20 mm bore (2026-08-15, not yet bench-verified):** the candidate's
7.8 mm thread and 11.8 mm shoulder both clear a 20 mm bore easily — the number that matters is
the 15.5 mm total length including its solder-lug legs. That figure overstates the real rigid
protrusion, though: the legs are flat stamped metal, meant to be soldered to (tin the lug, tin
the wire, join with the iron — the punched hole lets you hook the wire through first for a
mechanical grip before soldering, worth doing since this assembly gets handled and swung
around). **Trim the legs to length with flush cutters and bend each one ~90° once** (a single
deliberate fold, not repeated flexing which risks fatigue-fracturing the lug) to route them
along the bore's length instead of straight across its diameter. That leaves only the body +
thread (~10-12 mm) as the real radial protrusion, not the full 15.5 mm. Still unverified:
whether the XIAO board/battery/wiring occupy the bore cross-section directly opposite wherever
the button ends up mounted along its length — that's exactly what the bore mock-up in
**Roadmap** step 1 needs to check before drilling anything real.

<details>
<summary>Original 2026-08-13 double-tap rationale (superseded, kept for context)</summary>

Double-tap used the IMU's hardware tap detector, so it cost no extra parts, no pin, no hole in
the 20 mm bore, and no CPU while idle — and would have become the Phase 4 mic gate for free.
Neither commercial motion wand does free-running recognition (Kano gates on a held button,
Universal on standing at a medallion), so declaring a cast rather than free-running recognition
is still the right call — the reversal is about *how* you declare it, button vs. tap, not
whether to. Full prior-art rationale in `docs/spell-spec.md` — still relevant background, just
not the mechanism in use.
</details>

**Lumos has no timeout.** It burns until Nox is cast; the pair is the point. Two consequences to
handle rather than design away: the brightness cap becomes the only thing setting how long a lit
wand lasts, so pick it from measured `LedTest` numbers rather than by guess — and a low-voltage
floor is mandatory, see **Electrical constraints**.

**Nox's incantation is at-risk.** One syllable at ~300 ms is the weakest keyword in the set:
little acoustic evidence, and near-misses all over ordinary speech ("not", "knocks"). Worth
trying, but the per-spell-optional incantation field means dropping Nox to gesture-only is a
one-line config change and not a redesign. Nox works by gesture regardless.

**Expecto Patronum accepts either rotation direction** (revised 2026-08-16, was "clockwise"
only). Bench captures from two people (see the finding below) showed nothing in the pipeline
actually depends on rotation sign for circle — the feature that identifies it (sustained
low-angular-velocity motion with large total angular displacement) is direction-agnostic, and
one of the two testers naturally circled counter-clockwise. Requiring clockwise specifically
bought no separability, just a way to reject a valid, natural gesture. Revisit only if a future
spell needs the opposite direction to mean something else.

### Why these five gestures separate

The shapes are an engineering choice optimised for sensor separability, not a reconstruction of
canon — **no canonical wand movements exist** for these spells (see `docs/spell-spec.md`). Each
occupies a different corner of feature space, so thresholds can reach it:

| Gesture | Primary discriminator |
| --- | --- |
| Flick up | Short, single-axis rotation, **positive** sign, no reversals |
| Flick down | Same axis, **negative** sign — plus lit/unlit context as a tiebreak |
| Thrust | **Elevated linear accel** vs. that person's own flicks — *not* near-zero rotation, provisional, see finding below |
| Zigzag | **Higher peak gyro magnitude than thrust** (provisional); direction-reversal counting not yet working, see finding below |
| Circle | **Long duration + large total angular displacement**, returns to start, either rotation direction |

Only the two flicks share a shape, and they differ in sign. The zigzag exists specifically so
Avada Kedavra cannot collide with the Expelliarmus thrust — the failure mode if both were
"point and stab", as the films depict them.

All five are **wrist-scale, not arm-scale**, so the gyroscope carries most of the signal and the
thrust is the deliberate exception.

**Bench finding (2026-08-16, two people, `bringup/ImuTest/ImuTest.ino`, raw traces in
`bringup/traces/2026-08-16_daniel/` and `bringup/traces/2026-08-16_yuval/`): the thrust/zigzag
discriminators above are revised from the original design, not confirmed as originally written.**
Board mounted via rubber band to a toy wand (not the final bore), USB-C toward the handle, flat
side down — kept consistent across both sessions.

- **Board-axis → wand-axis mapping (established, both people agree):** flick up and flick down
  both load onto the board's `gy` axis (rotation about the board's Y axis), distinguished purely
  by sign — negative for up, positive for down. Held consistently across two people, so treat
  this as settled for this mount orientation.
- **"Near-zero rotation" for thrust is wrong.** Every thrust rep carried substantial rotation
  (peak gyro 426–679°/s across both people) — comparable to the flicks, not near zero. A natural
  stabbing thrust winds up and recoils at the wrist, and that's real rotation, not sensor noise.
- **Accelerometer-magnitude separation between thrust and the flicks is person-dependent, not a
  safe fixed threshold.** For one person thrust's peak accel (6.63 g) was a clean 1.7–2.8× outlier
  over both flicks; for the other it was only ~20% higher than his own flick_down (3.23 g vs.
  2.68 g) — nearly indistinguishable. A single global accel threshold would misclassify one of
  the two people.
- **What held up across both people: zigzag's peak gyro magnitude consistently beat thrust's, by
  roughly 1.5–2.3×** (1188 vs. 679°/s; 1000 vs. 426°/s), even though the absolute numbers varied a
  lot person to person. This is the one clean, repeatable signal from this session — likely the
  real thrust/zigzag discriminator, not accel dominance as originally assumed.
- **Naive per-axis gyro sign-reversal counting does not work as a zigzag detector**, even with a
  peak-to-peak swing threshold added to reject hand-tremor noise (the swing-threshold fix itself
  is sound — same debounce logic as the button). On raw multi-rep captures it counted *more*
  reversals for thrust (wind-up/recoil repeated across several reps in one window) than for
  zigzag. The Z-shape likely reverses through a combined multi-axis direction change rather than
  flipping sign cleanly on one axis; a working detector probably needs to test the 3D gyro
  vector's *direction* for a reversal (e.g. successive-sample dot product going negative) rather
  than watching each axis independently, and needs per-rep segmentation first so reps aren't
  averaged together. **Not built — real Phase 2 work, not solved by this bring-up session.**
- **Idle/rest is a solid, person-independent floor.** Both people's idle baseline landed around
  1.1 g peak accel / <125°/s peak gyro, well clear of every real gesture (next-lowest was circle
  at 196–365°/s). Safe to use as a wake/trigger threshold regardless of who's holding the wand.

Raw labelled traces: six CSVs per person (`flick_up`, `flick_down`, `thrust`, `zigzag`, `circle`
or `circle_ccw`, `idle`), one row per sample at 104 Hz, format `millis,label,ax,ay,az,gx,gy,gz`
(accel in g, gyro in °/s).

## Hardware

| Role | Part | Notes |
| --- | --- | --- |
| MCU | Seeed XIAO nRF52840 **Sense** | nRF52840 Cortex-M4F @64 MHz, 1 MB flash, 256 KB RAM, 2 MB QSPI flash (P25Q16H) |
| IMU | LSM6DS3TR-C (onboard) | 6-axis accel + gyro, gesture source |
| Mic | PDM MEMS (onboard) | Unused until Phase 4 (incantations). Keep its pins and power budget reserved. |
| Tip LED | WS2812B Mini RGB board | 10 mm round tile, single addressable pixel |
| Cast button | Momentary metal panel-mount switch — **candidate found, not yet ordered** | Held during a cast; gates the mic in Phase 4. AliExpress listing (2026-08-15): "8mm Buttons Metal Power On Off Push Button Mini Switch Momentary Self-reset ... Waterproof", ~₪4/$1.15, 4.8★/29 reviews/500+ sold. Confirmed momentary (not latching — verify this on any alternate listing, the two look identical in photos). Dimensions from listing photo: Φ7.8 mm thread, Φ11.8 mm shoulder, Φ7.9 mm cap, 5.8 mm thread length, 15.5 mm total incl. legs. **Verify a mounting nut is included before ordering** — not visible in listing photos. |
| Battery | 14500 Li-ion 3.7 V 1000 mAh | In a 1-slot AA/14500 holder (no direct soldering to cell) |
| Switch | SS12D00 1P2T slide | Breaks the battery line into BAT+ |
| Consumables | 30 AWG silicone wire, heat shrink, epoxy, hot glue | 20 mm wand bore — no pin headers fit |

Wiring: battery → slide switch → `BAT+`/`BAT-` pads on the XIAO underside. LED taps board
power and one digital pin for data. Cast button wires to a free digital pin and `GND`, read as
`INPUT_PULLUP` (pressed = LOW) — no external resistor needed. USB-C charges the cell through
the onboard BQ25101.

## Toolchain

`arduino-cli` (v1.5.2) — already configured on this machine. Do **not** assume the Arduino IDE.

**Arduino C++ everywhere, including throwaway bring-up sketches** (decided 2026-08-13). The
board does have an official CircuitPython build and it would iterate faster during Phase 1,
but it was rejected: Edge Impulse and TFLite-Micro deploy only as C++, so Python forecloses
the Phase 4 voice path outright; the CircuitPython VM idles at milliamps against a
microamp-scale budget; and its deep sleep restarts `code.py`, which cannot carry the
`IDLE ⇄ CASTING` state machine (see **Designing for voice**) across a wake. A C++-only bring-up phase
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
- **Hardware tap detection works, but is no longer used for arming** (superseded 2026-08-15 by
  the cast button — see **Spells**). Kept here because the register-level fix was hard-won and
  may be useful if tap is ever revisited for something else (e.g. a low-power wake source).
  **Single-tap detection is fully reliable**, confirmed on bench. **The chip's own hardware
  double-tap classification (`DOUBLE_TAP_EV_STATUS`) never once fired** across extensive bench
  testing, even for deliberate taps well inside a generous timing window — every real double-tap
  showed up as two separate single-tap events. A software layer (pairing two single-tap events
  within a tuned window, plus a debounce floor to reject one strike's cross-axis ringing from
  being mistaken for two taps) got double-tap working, but stayed noticeably more fragile to
  tune than single-tap ever was — see `bringup/TapTest/TapTest.ino`'s `w`/`g` commands and git
  history for that tuning work if it's ever needed again.
  The library gives you no API for tap, but every register and bit-mask needed
  is defined — `TAP_CFG1 (0x58)`, `TAP_THS_6D (0x59)`, `INT_DUR2 (0x5A)` (shock/quiet/dur fields),
  `WAKE_UP_THS (0x5B)`, `MD1_CFG (0x5E)`, `TAP_SRC (0x1C)` — and `readRegister`/`writeRegister` are
  public on `LSM6DS3`, so tap is configured with raw register writes.
  **The fix that actually made it work, after a long bench debugging session:** two bits, both
  bit 7 of their register, neither documented correctly (or at all) by the library, both required
  or the tap engine stays completely silent no matter how correct every other register is:
  - **`TAP_CFG1` bit 7 must be SET.** The library's header calls this bit `TIMER_EN` — that name
    is wrong. Per a screenshot of ST's own datasheet page, the comment on this exact bit is
    "Enable interrupts and tap detection on X, Y, Z axis": it's the master interrupt-enable for
    the whole 6D/tap/wake-up/free-fall block, not a timer. Left clear (the library's default
    framing), TAP_SRC never asserts, ever.
  - **`TAP_THS_6D` bit 7 must be SET.** Not named anywhere in the library's header at all — only
    `TAP_THS[4:0]` and `SIXD_THS[6:5]` are documented there. ST's own worked example sets it
    regardless of what threshold value bits 4:0 hold.
  Proof these two were the actual gap: a build using ST's literal datasheet single-tap example
  (`CTRL1_XL=60h, TAP_CFG1=8Eh, TAP_THS_6D=89h, INT_DUR2=06h, WAKE_UP_THS=00h, MD1_CFG=40h` — see
  `bringup/TapMinimal/TapMinimal.ino`) fired single-tap events on the first try. Every earlier
  attempt — SparkFun's reference sequence, plus `CTRL8_XL` FDS, plus `CTRL4_C` BW_SCAL_ODR, plus
  `CTRL10_C` FUNC_EN, all individually readback-verified as correctly written, against a
  confirmed-live and motion-reactive accelerometer — produced zero tap events because none of
  them ever touched these two bits.
  **`CTRL8_XL` FDS, `CTRL4_C` BW_SCAL_ODR, and `CTRL10_C` FUNC_EN are confirmed NOT required for
  tap** — the working datasheet-literal sequence above touches none of them. An earlier version of
  this doc guessed FUNC_EN might be needed; that guess is now disproven. Don't re-add them.
  Other traps, still valid: macros carry an `LSM6DS3_ACC_GYRO_` prefix and the register is
  `TAP_CFG1`, not `TAP_CFG`; **`MD1_CFG` bit 3 is named `INT1_TAP_ENABLED` but routes *double*-tap**
  per the ST datasheet (there is no `INT1_DOUBLE_TAP` macro — bit 6 is single-tap); **`WAKE_UP_THS`
  bit 7 must be SET (0x80), not clear, to enable double-tap** — the library's own enum names are
  backwards here (`SINGLE_DOUBLE_TAP_DOUBLE_TAP = 0x00`, `SINGLE_DOUBLE_TAP_SINGLE_TAP = 0x80`;
  trust the numeric value, not the name).
  **General lesson from this debugging session, worth remembering beyond tap specifically:** the
  `Seeed_Arduino_LSM6DS3` header is not a reliable source of bit *meaning*, only of bit
  *position/register* — it gets names backwards or wrong more than once in this one register
  block alone. When a register write's effect doesn't match its library-given name, check the
  actual ST datasheet before trusting the header comment.
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
- **`Serial` needs `#include <Adafruit_TinyUSB.h>`, or the link fails.** On this core, `Serial`
  is the USB-CDC object (`Adafruit_USBD_CDC`) bundled as a *library* under
  `libraries/Adafruit_TinyUSB_Arduino`, not compiled into the core unconditionally — the
  library's source (and the global `Serial` object itself) is only pulled into the build when
  something in the sketch's dependency graph includes `Adafruit_TinyUSB.h`. Skip the include
  and the linker fails with `undefined reference to Serial` / `Adafruit_USBD_CDC::begin` even
  though `Serial.begin()`/`.print()` compile fine — the error only shows up at link time. A
  sketch that calls `Serial.begin()` but never anything else touching `Serial` (e.g. an early
  `blink.ino` bring-up) can link by accident with no explanation as to why, which makes this
  easy to miss until a sketch that actually uses `Serial.println()`/`while (!Serial)` hits it.
  Found bringing up `bringup/ButtonTest/ButtonTest.ino` (2026-08-16).
- **`arduino-cli monitor` prints nothing and exits immediately when stdin isn't a real
  terminal** (e.g. run from a non-interactive shell/tool). It needs a TTY. Read the port with a
  small pyserial script instead (`serial.Serial(port, baud).readline()` in a loop) when
  monitoring from a non-interactive context.
- Free digital pins: `D0`–`D3` (also `A0`–`A3`). Avoid `D4`/`D5` (I2C), `D6`/`D7` (UART),
  `D8`–`D10` (SPI) unless you're deliberately reusing them. `D0` is the LED (see **Closed
  2026-08-13** in **Open decisions**); **the cast button is provisionally `D1`** (decided
  2026-08-15) — picked only because it's next in the free-pin list, no peripheral conflict.
  Unlike `D0`'s pad, this hasn't been checked against the board's physical castellated-pad
  layout for solder accessibility — do that before committing to it.

## Electrical constraints

- **WS2812B on a 3.7 V supply is the main open risk.** The part is specified 3.5–5.3 V. Off
  `BAT+` it sees 4.2 V falling to ~3.3 V as the cell drains — expect dimming and color
  shift near end of charge, and possible dropout below 3.5 V. Off the regulated `3V3` pin
  it is out of spec from the start. Test this early on the bench before committing to a
  wiring plan; a single-diode drop or a level shifter on DIN are the usual fixes.
- One WS2812B at full white is ~60 mA — comparable to the whole MCU's active draw. Cap
  brightness and keep effects short; this dominates the battery budget.
- **The 14500 must be a protected cell, and the firmware needs a low-voltage floor as well.**
  Li-ion is permanently damaged below ~2.5 V — lost capacity, plus dissolved copper that can
  plate into internal shorts — and the onboard BQ25101 is a *charger* with **no discharge
  protection**, so nothing on the board stops a cell being drained flat. Because Lumos never
  times out this is the normal use case, not an edge case: cast it, set the wand down, and ~60 mA
  walks the cell past 2.5 V unattended. Two layers doing two different jobs:
  - **Protected cell (~2.5 V, hardware).** A protection PCB in the cell's cap end that hard
    disconnects. Survives a firmware hang — which is the failure most likely to actually happen.
    Establish on the bench whether the cell in hand has one; if not, it's a part to buy before
    assembly.
  - **Firmware floor (~3.0 V, provisional).** Extinguishes Lumos and refuses to relight, so the
    wand degrades gracefully instead of slamming into the hard cutoff and appearing dead. It
    **must latch** — but not mainly because of IR drop, which is small here: 60 mA through a
    healthy 14500's ~150 mΩ is only ~9 mV, and perhaps 20–40 mV once the holder contacts, slide
    switch and 30 AWG are counted. The real driver is **relaxation** — near the bottom of the
    discharge curve an unloaded cell recovers 50–100 mV over seconds to minutes, easily enough to
    re-cross the floor and oscillate cut/relight/cut. Calibrate **under LED load**, and measure
    the recovery as well as the sag.

  **A second cell is not a fix — don't re-propose it.** Series (~7.4 V) would destroy the
  single-cell BQ25101, and series cells drain unevenly so per-cell undervoltage is invisible to
  one ADC reading. Parallel only halves the discharge rate, which buys time against an indefinite
  Lumos rather than protecting anything, and two 14500s are 28 mm across a 20 mm bore against a
  1-slot holder.
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

Only `README.md`, `CLAUDE.md` and `docs/` exist so far — no firmware yet. Planned structure:

```
smart-wand/
├── CLAUDE.md
├── docs/
│   ├── implementation-plan.md    # phased plan of record
│   └── spell-spec.md             # gesture/incantation prior art and rationale
├── SmartWand/                    # main firmware sketch
│   ├── SmartWand.ino
│   ├── config.h                  # pins, thresholds, tunables
│   ├── gestures.{h,cpp}
│   ├── effects.{h,cpp}
│   └── power.{h,cpp}
├── bringup/                      # Phase 1 throwaway test sketches
│   ├── LedTest/LedTest.ino
│   ├── ImuTest/ImuTest.ino
│   ├── TapTest/TapTest.ino       # tap detection bench harness; superseded for arming, see Spells
│   ├── TapMinimal/TapMinimal.ino # tap detection, literal ST datasheet sequence — same status
│   ├── ButtonTest/ButtonTest.ino # debounced press/release over serial — bench-verified 2026-08-16
│   ├── BatteryTest/BatteryTest.ino
│   └── MicTest/MicTest.ino
└── tools/                        # host-side capture scripts, run on the laptop
    ├── capture_traces.py         # serial → labelled CSV, Edge Impulse ingestible
    └── capture_audio.py          # raw PDM stream → per-utterance WAV
```

`tools/` is plain Python on the host, not on the wand. It exists because the C++-only decision
leaves no USB-drive filesystem to drop CSV and WAV files onto.

## Roadmap

1. **Bench bring-up** — LED colors/patterns on flying leads; stream IMU data over serial to
   collect real gesture traces; verify battery sense and WS2812B behavior at 3.7 V.
   **Source the cast button and mock up its bore placement early** — a hole through the shell,
   reachable during a natural grip without blocking gesture swings, is the one part of the
   button plan that's hard to change after **Assembly** step 3 glues things shut.
   `bringup/ButtonTest/ButtonTest.ino` (debounced press/release over serial) is written and
   bench-verified (2026-08-16) — but only against a stand-in 4-leg tactile switch on a
   breadboard wired to `D1`/`GND`, not yet against the actual AliExpress candidate part
   (still not ordered, see **Hardware**). Five press/release cycles, holds ranging ~800–1460 ms,
   zero bounce or spurious edges through the 30 ms debounce window. One bring-up gotcha worth
   knowing if this gets re-run: see **Board gotchas** for the `Adafruit_TinyUSB.h` include this
   core needs before `Serial` will link.
   **Also dump raw PDM audio to serial and record incantation samples while the board is on
   the bench** — this is nearly free now and annoying to redo once the wand is epoxied shut.
   `bringup/ImuTest/ImuTest.ino` (104 Hz accel+gyro CSV, keypress-tagged labelling) is written
   and bench-verified (2026-08-16): clean sampling (9.6 ms avg period, zero dropped samples) and
   correct label tagging confirmed. Used for two full labelled-gesture sessions so far, board
   rubber-banded to a toy wand (not the final bore) rather than a bare board on a desk — see
   **Why these five gestures separate** for the resulting axis-mapping and discriminator
   findings, and `bringup/traces/` for the raw CSVs. Both the axis mapping and the thrust/zigzag
   discriminator got revised from their original design based on this data.
2. **Firmware** — gesture recognition from accel+gyro; map gestures to spell animations
   (*Lumos*, *Expelliarmus*, …); idle power optimization.
3. **Assembly** — solder 30 AWG to castellated pads, heat shrink every joint, epoxy the LED
   into the tip as a diffuser, hot-glue the stack into the 20 mm bore. Keep the USB-C port,
   switch lever, and cast button accessible.
4. **Incantations (deferred)** — button-gated keyword spotting on the PDM mic. See below.

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

- **Gate the mic on the cast button; never listen continuously.** Always-on wake-word detection
  keeps the MCU out of low-power states and will destroy the 8–12 h runtime target. "Hold the
  button, then speak" is both the natural interaction and the cheap one — simpler to reason
  about than the original gesture-triggered gate, since the button is an unambiguous digital
  signal rather than something a gesture classifier has to first recognize.
- **Model a spell as `(gesture, optional incantation)` from the very first commit.** If the
  spell table is keyed on gesture alone, adding a word later means reworking every call
  site. Leave the field present and unused.
- **Structure the firmware as an explicit `IDLE ⇄ CASTING` state machine**, transitioning on
  the button's press/release edges (superseded 2026-08-15 from an earlier `IDLE → ARMED →
  LISTENING → CASTING` design written for tap-arming — see **Spells**). `CASTING` covers both
  "listening for the incantation" and "running the gesture-matched effect"; there's no separate
  `ARMED`/`LISTENING` state because the button already unambiguously marks the whole window —
  press starts it, release ends it. The mic path is a no-op stub within `CASTING` in Phases 1–3
  and gets filled in at Phase 4.
- **Keep flash and RAM headroom.** Don't let gesture code, animation tables, or logging
  sprawl to fill the chip. Check `compile` output for usage as a routine habit.

The 2 MB onboard QSPI flash is a good home for models and recorded samples, and is otherwise
unused — don't repurpose it casually.

## Open decisions

- **Is BLE used?** The nRF52840 supports it and the plan never mentions it. It costs power
  and complexity; worth having only if there's a use (config, OTA, wand-to-wand duels).
- **How many pixels at the tip?** The plan says one WS2812B. More would allow richer
  effects at a real power cost.

Closed 2026-08-13. The **incantation vocabulary** is the five spell words in **Spells** plus a
noise/other class — record samples for exactly those. The **LED is driven by `D0`**: corner
castellated pad, easy to solder, no peripheral conflict.
