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
| Expecto Patronum | Circle(s), either direction, any count | "Expecto Patronum" | Light blue-silver shimmer, ~2 s |

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

**Expecto Patronum accepts any number of circles** (added 2026-08-22, from bench feel rather
than from data: a single slow circle is anticlimactic to perform, and the natural instinct is to
keep circling). Threshold on **cumulative** angular displacement with no upper bound, not on one
completed 360 deg revolution. This costs nothing and arguably helps: N circles pushes further
along the very axis that already separates circle from every other gesture (sustained
low-angular-velocity motion with large total displacement), so it moves *away* from the flick,
thrust and zigzag clusters rather than toward them. It is free specifically **because the cast is
button-gated** — press starts the window, release ends it, so a variable-length gesture needs no
timeout, no segmentation heuristic, and no upper bound. It also fixes an ergonomic mismatch: the
longest incantation in the set was previously paired with a fixed-length movement, which meant
racing the word to fit the gesture. Now you circle while you speak and release on the last
syllable. Changing the gesture shape instead was considered and rejected — the five shapes are
chosen for separability, any replacement must prove it doesn't collide with a flick, thrust or
zigzag, and there is no canon to appeal to (see `docs/spell-spec.md`).

### Why these five gestures separate

The shapes are an engineering choice optimised for sensor separability, not a reconstruction of
canon — **no canonical wand movements exist** for these spells (see `docs/spell-spec.md`). Each
occupies a different corner of feature space, so thresholds can reach it:

| Gesture | Primary discriminator |
| --- | --- |
| Flick up | Short, single-axis rotation, **positive** sign, no reversals |
| Flick down | Same axis, **negative** sign — plus lit/unlit context as a tiebreak |
| Thrust | **Elevated linear accel** vs. that person's own flicks — *not* near-zero rotation, provisional, see finding below |
| Zigzag | **Side-to-side yaw (~90 deg from the flicks) that reverses twice** along that axis. See the 2026-10-09 analysis below |
| Circle | **Large CUMULATIVE angular displacement + sustained (smooth) motion** rather than a burst, either rotation direction, no upper bound on revolutions |

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
  *(Superseded by the 2026-10-09 analysis below: with per-rep segmentation the 3D dot-product
  reversal count does fire on zigzag, 2 per rep — but circles score 1-2 as well, so it is not
  a discriminator on its own.)*
- **Idle/rest is a solid, person-independent floor.** Both people's idle baseline landed around
  1.1 g peak accel / <125°/s peak gyro, well clear of every real gesture (next-lowest was circle
  at 196–365°/s). Safe to use as a wake/trigger threshold regardless of who's holding the wand.

**Offline analysis (2026-10-09, `tools/gesture_lab.py`, same 2026-08-16 traces): a
roll-tolerant heuristic classifies 30/33 intact reps across both people, and the three misses
are artifacts of the stand-in segmenter (a slow circle split in two, two thrust wind-ups cut
from their stab), not of the rules.** Every feature is a running sum over one window, so it
ports to the M4F as-is. Thresholds live at the top of the script and are the candidates for
`config.h`. `--stress` and `--roll-sweep` re-run the robustness checks below on any new traces.

- **The zigzag is a side-to-side yaw that reverses twice.** Treat `(gy, gz)` (the rotation
  components across the wand: pitch is tilting up/down, yaw turning side to side) as a vector
  per sample. Over the window, the principal direction of those vectors is **phi**: flicks
  -8..+19 deg (pitch), zigzag +/-86-88 deg (yaw). Project the smoothed rotation onto that axis
  and count sign changes between strong lobes: **every zigzag flips exactly 2 times** (right,
  left, right); everything else at most 1 (a flick's return stroke). Rule: |phi| >= 45 and
  flips >= 2 and planarity >= 0.5. This is the vector-reversal idea from the 2026-08-16 bullet
  above, measured along the cast's own main axis instead of raw board axes.
- **Why not planarity alone (tried first, rejected):** the eigenvalue spread of the `(gy, gz)`
  covariance (flicks 0.93-1.00, zigzag 0.87-0.94) separates the recorded Zs, but only because
  both people drew wide, flat ones: each stroke's direction measured 1-18 deg off pure yaw. A
  taller Z puts pitch into the diagonal, and with a 0.80 planarity cutoff the classifier failed
  from just +15 deg of extra diagonal tilt (synthetic, `--stress`). The flip rule holds to
  +45 deg of extra tilt. Planarity stays in the zigzag rule only at 0.5, to keep circles
  (<= 0.27) out.
- **Circle vs thrust is decided by smoothness, not peak speed.** Smoothness = mean speed while
  moving / peak speed: circles 0.64-0.91 (sustained), thrusts 0.26-0.48 (a burst). The first
  version used peak < 400 deg/s, which sat in a 55 deg/s gap for Yuval (circle 365, thrust 420)
  and failed on circles performed just 1.25x faster. Smoothness is a ratio, so performing faster
  or slower doesn't move it; synthetic circles at 3x speed still classify. Circle also needs
  cum >= 180 deg (one circle 214-296; more circles only add).
- **Decision order:** cum < 55 deg -> none; fast (peak >= 300) and the zigzag rule -> zigzag;
  fast, |phi| < 45 and planarity >= 0.8 -> flick; cum >= 180 and smooth >= 0.55 -> circle; else
  thrust. Flick sign comes from the **first strong lobe** projected on phi. Integrated `gy` nets
  to ~0, because every flick's return stroke cancels it.
- **The cumulative-rotation integral needs a 75 deg/s deadband.** At 30, an 8 s still hold
  integrates to 171 deg (Daniel), which is circle territory. At 75 it reads <= 44, while
  Daniel's very slow circles (peak only 170-196 deg/s) stay above 60 deg/s for 99% of their
  samples and lose almost nothing. This matters because circle has no upper bound on hold time.
- **Roll tolerance: accuracy holds flat from -35 to +25 deg of wand twist** (twist about the
  wand's own length, board x; `--roll-sweep` simulates it). It's asymmetric because Yuval
  already holds the wand ~15 deg twisted relative to Daniel (his flicks sit at phi +12..+19).
  If the final grip proves looser than that, de-roll using the accelerometer's gravity vector at
  button-press time. The button itself may fix most of it, since the thumb lands on it at a
  fixed roll.
- **Known soft spots:** planarity at the thrust/flick boundary (0.68 vs 0.80 cutoff, nearest
  flick 0.93); and the 55 deg "no gesture" floor, which Yuval's smallest thrust (cum 61) clears
  by little, and fails when performed slower (`--stress`).
- **Caveat that outweighs the result: ~3-4 reps per gesture per person, captured WITHOUT the
  button**, and the stress tests are synthetic edits of those same reps. Pressing a button while
  gesturing may change the motion itself. The next IMU capture should be button-gated, one
  window per press, which also replaces the segmenter.

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
| Cast button | Momentary metal panel-mount switch — **in hand and bench-verified (2026-10-09)** | Held during a cast; gates the mic in Phase 4. AliExpress listing (2026-08-15): "8mm Buttons Metal Power On Off Push Button Mini Switch Momentary Self-reset ... Waterproof", ~₪4/$1.15, 4.8★/29 reviews/500+ sold. Confirmed momentary (not latching — verify this on any alternate listing, the two look identical in photos). Dimensions from listing photo: Φ7.8 mm thread, Φ11.8 mm shoulder, Φ7.9 mm cap, 5.8 mm thread length, 15.5 mm total incl. legs. Whether a mounting nut came with it is not yet recorded. Bench check via `ImuTest` on `D1`/`GND` (2026-10-09): 5/5 presses, holds 84-3717 ms, zero bounce or spurious edges, release registered every time (blue LED tracked it), so this unit is truly momentary. Legs are flat solder lugs, so bench wiring without solder is by mini alligator clips onto the lugs, not Dupont jumpers. |
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
  **Corollary: a sketch that never includes `Adafruit_TinyUSB.h` has no USB serial port at all**,
  so the board vanishes from `board list` and `arduino-cli upload` has nothing to reset it
  through. The upload then fails, and only the *last* line of its output looks normal ("New
  upload port"), so always check for "Device programmed". Recover with a RESET double-tap
  (the bootloader appears on its own COM port, e.g. `COM4`) and upload to that port. Hit
  2026-10-09 with a serial-free pin-mirror diagnostic.
- **A single large `Serial.write()` stalls permanently on this core's USB-CDC.** Dumping a 96 KB
  buffer in one call delivered exactly 30,208 bytes at 14.7 KB/s and then nothing, forever, on
  every attempt (measured 2026-08-22 with `bringup/MicTest`). Deterministic, not a race. The fix
  is to write in small chunks (512 B), `Serial.flush()` and `yield()` between them, and **add the
  return value of `Serial.write()` to your offset rather than assuming it accepted everything** —
  it returns a count, and ignoring it silently drops data. Chunked, the same 96 KB transfers in
  0.19 s at 498 KB/s, i.e. 34x faster *and* complete. The dangerous part is the symptom: a
  truncated audio capture still sounds fine and still scores 40+ dB SNR, so nothing downstream
  flags it. Also make the host read defensively — `pyserial`'s `timeout` is a deadline for the
  whole `read()` call, not an idle timeout, so `ser.read(96000)` returns a partial buffer without
  raising. Read in chunks until the stream actually goes quiet (see `read_exact()` in
  `tools/capture_audio.py`).
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

The `SmartWand/` skeleton exists as of 2026-10-09: gesture engine verified on the host against
the Python reference, state machine verified on the board. Structure:

```
smart-wand/
├── CLAUDE.md
├── docs/
│   ├── implementation-plan.md    # phased plan of record
│   └── spell-spec.md             # gesture/incantation prior art and rationale
├── SmartWand/                    # main firmware sketch
│   ├── SmartWand.ino             # IDLE ⇄ CASTING state machine on the button
│   ├── config.h                  # pins, thresholds, tunables
│   ├── gestures.{h,cpp}          # GestureEngine, port of tools/gesture_lab.py;
│   │                             #   Arduino-free so it also compiles on the host
│   ├── spells.h                  # (gesture, optional incantation) -> effect
│   ├── effects.{h,cpp}           # millis()-driven tip LED, base + transient layers
│   ├── button.h                  # debounced cast button
│   └── power.h                   # STUB: low-voltage floor not built yet
├── tests/
│   └── gesture_parity.cpp        # C++ engine vs Python reference, host-compiled
├── bringup/                      # Phase 1 throwaway test sketches
│   ├── LedTest/LedTest.ino
│   ├── ImuTest/ImuTest.ino
│   ├── TapTest/TapTest.ino       # tap detection bench harness; superseded for arming, see Spells
│   ├── TapMinimal/TapMinimal.ino # tap detection, literal ST datasheet sequence — same status
│   ├── ButtonTest/ButtonTest.ino # debounced press/release over serial — bench-verified 2026-08-16
│   ├── BatteryTest/BatteryTest.ino
│   ├── MicTest/MicTest.ino
│   ├── SelfTest/SelfTest.ino     # hardware health check — IMU/mic/battery/LED/pins
│   ├── traces/                   # labelled IMU CSVs, per date_speaker
│   └── traces_audio/             # labelled incantation WAVs, per date_speaker_tag
│                                 #   each dir also holds takes.csv (label, tag,
│                                 #   duration, peak, in-band SNR per take)
└── tools/                        # host-side capture scripts, run on the laptop
    ├── capture_traces.py         # guided button-gated IMU capture → one CSV per gesture
    ├── capture_audio.py          # raw PDM stream → per-utterance WAV
    ├── gesture_lab.py            # offline reference classifier over bringup/traces/
    └── read_serial.py            # print serial lines; substitute for `arduino-cli
                                  #   monitor`, which needs a TTY (see Board gotchas)
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
   `bringup/MicTest/MicTest.ino` (16 kHz mono PDM capture: idle live-level meter, `'r'`/`'s'`
   keypress-triggered fixed 2 s recording window, binary PCM dump over serial) is written and
   bench-verified (2026-08-22): compiles clean (44,528 B flash / 5%, 71,748 B RAM / 30% — the
   64 KB capture buffer is most of that), live `LEVEL,...` output visibly reactive to bench noise
   — `MIC_GAIN` is **28** (−6 dB) as of 2026-08-23; it was 40 (the hardware 0 dB point) before
   that, and the reason it moved is **mount rigidity**, see the headroom section below.
   **The PDM gain is digital**, applied after decimation, so it scales signal and noise together
   and cannot improve SNR. **Don't sweep the gain looking for a better value** — it is a pure
   multiply, so a single `calibrate` run predicts every other gain arithmetically
   (`level × 10^(dB/20)`, 0.5 dB per register step). Because it is a pure multiply, a set
   recorded across a gain change is also **exactly reconcilable** rather than ruined — but only
   if the gain in force is written down per take, which is why `MicTest` now emits it in the
   `CAPTURE` header and `capture_audio.py` records it in `takes.csv`.

   **Measure audio levels BAND-LIMITED to 300-3400 Hz. Raw sample amplitude on this mic measures
   something else entirely** (established 2026-08-22 from six diagnostic takes at arm's length,
   gain 40, in `scratchpad`; the analysis is reproducible from any capture):

   | | broadband | 300-3400 Hz |
   | --- | --- | --- |
   | noise floor RMS | 375.1 | **4.9** |
   | noise floor peak | 1430 | 460 |

   **100% of the noise energy sits below 100 Hz**, and it is not a DC offset (mean is only
   −18..−81 counts). It is real infrasonic rumble — hand tremor, air movement, body motion, the
   mic's own 1/f noise — and nothing in the chain was high-passing it. Speech lives at
   300-3400 Hz, which is also all an MFCC front-end ever sees, so that rumble is invisible to the
   Phase 4 model and must be invisible to the tooling's thresholds too.

   **In-band SNR at true arm's length is 24-38 dB — incantations are comfortably viable at
   casting distance.** Per take: `expelliarmus` 30.3 dB normal / 37.7 dB projected, `nox`
   25.8 dB normal / 24.0 dB quiet. Note that even Nox — the one-syllable word flagged above as
   the at-risk incantation — has ample level; its risk is acoustic confusability with ordinary
   speech, **not** signal strength, so don't try to solve it with gain or distance.

   Everything measured before this finding was measuring rumble: three `calibrate` runs reported
   9.2-15.6 dB "SNR" for audio actually carrying 24-38 dB, and the QC thresholds would have
   rejected a good quiet `nox` (raw peak 895, in-band SNR 31 dB) as silence. Two corollaries that
   cost hours each and should not be re-derived: **the noise floor is not the room** (fan on/door
   open vs fan off/door shut moved the gain-40 floor from p50 678 to p50 638 — nothing), and
   **speech level is not a stable number** (~2 dB run-to-run variance from delivery alone, which
   is larger than most effects worth chasing — never conclude anything from a single short run).

   `MicTest.ino` now high-passes at 300 Hz before computing the level meter and emits
   `LEVEL,<ms>,<peak>,<rms>` — both of the filtered signal. **Recordings stay raw**; the Phase 4
   training pipeline owns its own preprocessing. `tools/capture_audio.py` judges every take on
   band-limited energy (loudest 50 ms frame, counts RMS, as dB over `NOISE_BAND_RMS = 5.0`;
   pass is >20 dB for a word, <22 dB for a `silence` take) and treats the `rms` field as optional
   so it still runs against un-reflashed firmware, with a warning.

   Two measurement traps already hit and fixed in `calibrate`, worth knowing before writing any
   similar bench tool: a **settle window** is mandatory (the keypress that starts a phase thumps
   the board), and a **speech-presence gate** is mandatory (the statistic is a p90 over 200 ms
   windows, so without gating it silently measures how much of the window you spent *talking*
   rather than how *loudly* — that artifact alone drifted three runs 4.5 dB apart).

   **Motion sets the headroom budget — and HOW THE BOARD IS MOUNTED sets how much motion
   reaches the mic.** This is the single most surprising thing found in the audio bring-up, and
   it invalidated a gain choice that had looked settled. The table below was measured
   2026-08-22 with the board taped **loosely** to the wand. On 2026-08-23 the board was
   remounted **rigidly** (bedded on putty under two rubber bands, because the tape was crackling
   into the mic and the board was shifting), and the identical zigzag at the identical gain then
   measured **29776-30138 raw — 91-92% of the int16 ceiling, 0.7 dB of margin**, against the
   14225-17826 below. A rigid mount transmits wand vibration into the board far better than a
   loose one; roughly **+5 dB** here.

   **`MIC_GAIN` was therefore lowered 40 → 28 (−6 dB)**, restoring ~6.7 dB of margin. It cost
   nothing measurable: the same cast scored 52.6-53.4 dB in-band SNR at gain 28 against
   53.7-56.4 dB at gain 40, i.e. unchanged within delivery variation, exactly as a pure digital
   multiply must be. The in-band noise floor simply halved, 8.8 → 4.4 counts, still ~24 dB above
   the int16 quantisation floor.

   **The rigid number is the real one, and the finished wand will be more rigid still** — the
   board gets hot-glued and epoxied into the bore. Whether that pushes transmitted vibration up
   further (stiffer coupling) or down (potting adds mass and polymer damping) is genuinely
   uncertain, so **re-measure headroom on the assembled wand rather than predicting it**. If a
   cast clips there, lower `MIC_GAIN` again; never raise it.

   Original loose-mount measurements (2026-08-22, `MIC_GAIN=40`, board handheld at arm's
   length, word spoken while performing the gesture):

   | condition | raw peak | LF (<300 Hz) RMS | in-band noise | word SNR | headroom |
   | --- | --- | --- | --- | --- | --- |
   | standing still | 1181-2034 | 114-166 | 1.7-2.2 | 39-43 dB | 24-29 dB |
   | forward thrust | 5063-8691 | ~1200 | 2.4-6.6 | 47-48 dB | 12-16 dB |
   | **zigzag (Avada Kedavra)** | **14225-17826** | ~4400 | 12-24 | 54-55 dB | **5-7 dB** |

   Swinging the wand multiplies sub-300 Hz energy by ~30x and the raw peak by ~9x. The zigzag is
   3.6x worse than the thrust — consistent with it having the highest peak gyro of any gesture
   (1188 vs 679 deg/s, see **Why these five gestures separate**), and it is *sustained*, so violent
   motion overlaps the whole utterance instead of trailing one jab. Clipping destroys the
   *speech*, not just the rumble, because saturation hits the summed waveform before any filter
   can separate them — which is the asymmetry that justifies giving up headroom cheaply. So:
   **never raise MIC_GAIN; if a future cast ever clips, lower it.** This is also why the
   clipping check in `capture_audio.py` is deliberately broadband while every level check is
   band-limited.

   **In-band SNR is unharmed by motion** — it measured *higher* while casting (54 dB vs 43 dB
   still), because people naturally project when performing the gesture. Motion raises the
   in-band noise floor from ~1.6 to 12-24 counts, real but ~35 dB below the word. Recording
   training data while actually casting costs nothing in signal quality, and avoids training on
   audio cleaner than deployment will ever be.

   **Correction to an earlier finding: room quiet DOES matter, in-band.** The fan-on/fan-off
   comparison that concluded "the floor is not the room" was measuring the broadband floor, which
   is rumble, and was blind to in-band noise by construction. Measured properly, the in-band floor
   went from 4.9 counts RMS (ordinary quiet room) to **1.6** with noise sources deliberately off —
   ~10 dB. Both statements are true at their own frequencies: **sub-100 Hz rumble is
   room-independent and is the mic and your hand; 300-3400 Hz noise is the room and responds to
   quieting it.** `NOISE_BAND_RMS = 5.0` in `capture_audio.py` is therefore a room-dependent
   reference, not a constant of the hardware — the QC margin (~11 dB) absorbs the variation.

   Don't "fix" a low reading by recording closer: the wand really is at arm's length in use, and
   training on cleaner audio than deployment sees is the mismatch this whole bring-up is trying
   to avoid.
   A triggered capture produced exactly the expected
   32,000 samples / 64,000 bytes with correct framing. `tools/capture_audio.py` (new — the first
   script actually in `tools/`; the IMU CSVs so far were captured without one) drove that capture
   end-to-end and wrote a valid mono/16-bit/16 kHz WAV from it. Not yet used for a real labelled
   incantation session (the five spell words + noise class per **Closed 2026-08-13**) — this run
   was a plumbing check with ambient bench noise, not real recordings. The PDM library used is
   the one bundled with the Seeeduino core itself (`libraries/PDM`, gated on
   `ARDUINO_NRF52_ADAFRUIT`, which `platform.txt` defines for every board in this core, XIAO Sense
   included, despite the library's own header comment claiming Adafruit-board-only) — not a
   separately managed library, so it won't show up in `arduino-cli lib list`.
   **`ImuTest` is now button-gated (2026-10-09, bench-verified with the real button: cast column
   numbered 1-5 cleanly, 9.61 ms sample period, max 10 ms, no dropped samples):** each row
   gains a `cast` column, 0 while released and the press number while the button on `D1` is
   held, so one cast = one window with no segmentation heuristic. `tools/capture_traces.py`
   drives a guided session (hold, perform, release; N casts per gesture, resumable) and prints
   what `gesture_lab.py` classifies each cast as. Its `idle` gesture means pressing the button
   *without* gesturing, which the classifier must reject. Run with:
   `python tools/capture_traces.py COM5 --speaker NAME` (default 10 casts each).
   This is the capture the 2026-10-09 classifier is waiting on: real button-gated casts, so
   the thresholds stop resting on ~3-4 unbuttoned reps per person.
   `bringup/ImuTest/ImuTest.ino` (104 Hz accel+gyro CSV, keypress-tagged labelling) is written
   and bench-verified (2026-08-16): clean sampling (9.6 ms avg period, zero dropped samples) and
   correct label tagging confirmed. Used for two full labelled-gesture sessions so far, board
   rubber-banded to a toy wand (not the final bore) rather than a bare board on a desk — see
   **Why these five gestures separate** for the resulting axis-mapping and discriminator
   findings, and `bringup/traces/` for the raw CSVs. Both the axis mapping and the thrust/zigzag
   discriminator got revised from their original design based on this data.
2. **Firmware** — gesture recognition from accel+gyro; map gestures to spell animations
   (*Lumos*, *Expelliarmus*, …); idle power optimization.
   **Skeleton in place and running on the board (2026-10-09).** Bench run with the real button:
   ~60 casts across several runs, all classified without a hang. Clean run: 4 casts for 4
   intended presses, the quick tap filtered by `MIN_CAST_MS`, all `none` at 3-18 deg/s (resting
   gyro noise plus finger pressure, far under the 75 deg/s deadband). Casts with deliberate
   motion reached the engine at 80-127 deg/s, still `none`, correctly, against flicks at 400+.
   **Jumper/alligator wiring to the button is too unstable for a gesture capture**: a lead that
   drops contact mid-swing ends the cast early, splitting one gesture into fragments. Capture
   needs the printed holder (or soldered leads), not loose clips. `GestureEngine` ports `gesture_lab.py` exactly: the C++
   and Python agree on all 350 windows (real reps plus the stress and roll variants), checked by
   `tests/gesture_parity.cpp` on the laptop. **Change a rule in Python first, then C++, then rerun
   parity**: build and run commands are in that file's header. Effects, the spell table and
   the state machine compile (7% flash, 6% RAM, mostly the 10 s cast buffer). Still to do:
   real gestures on a mounted board, wire and tune the LED, the low-voltage floor (`power.h` is a stub, so
   **don't leave Lumos burning on battery**), IMU FIFO + wake-on-motion, and MCU sleep in IDLE.
3. **Assembly** — solder 30 AWG to castellated pads, heat shrink every joint, epoxy the LED
   into the tip as a diffuser, hot-glue the stack into the 20 mm bore. Keep the USB-C port,
   switch lever, and cast button accessible.

   **Do not let epoxy or hot glue touch the PDM mic's acoustic port.** The MEMS mic breathes
   through a small hole in its package; occluding it deafens the mic, and it is unrecoverable
   once cured. Mask the mic before potting.

   **Plan an acoustic port hole in the shell, at the same time as the button hole** (both are
   pre-epoxy decisions, see step 1). Sealing a mic inside an enclosure and porting it through a
   small hole is the standard approach — every phone does it — and the rule that makes it work
   is to **minimise the cavity between the mic package and the hole**. A large trapped volume
   plus a small hole is a Helmholtz resonator and will colour the speech band; the mic sitting
   nearly flush against the shell with a short 1-2 mm hole stays flat well past 4 kHz. Mounting
   the board *outside* the wand was considered as a fallback if this proves bad — it would match
   the training data exactly, since that was all recorded in open air — but it costs the whole
   look of the thing and exposes the board to the swings, so try the port first. **This is
   testable before anything is glued:** put the board in the bore with a port hole, record a few
   takes, and compare against open air.
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

## Incantation recording plan (Phase 1, IN PROGRESS — resume here)

Audio bring-up is **complete and proven**. **This is real training data, not a bench test.**

**State (2026-08-23): Daniel's set is DONE — 260 takes**, in
`bringup/traces_audio/2026-08-23_daniel_<tag>/`. 40 takes each of the five spell words and of
`silence`, 20 of `other`, across five conditions (`normal`, `gesture`, `quiet`, `fast`,
`still`). Nothing clipped in 260 takes. **Next: repeat every batch with `--speaker alise`.**

Two directories under `traces_audio/` are deliberately **not** training data and are named
with a leading underscore so they don't collide with the session tool's directory scheme:
`_superseded_2026-08-22_pilot/` and `_compare_gesture_silence_loosemount/`. Each has a README
explaining why. Don't fold them back in without reading those.

**How a take works now.** The operator starts AND stops each take — Enter, perform, Enter —
mirroring the cast button's press/release. There is no fixed recording window, deliberately;
see `MAX_CAPTURE_MS` in `MicTest.ino`. A normal take runs ~1.1-1.6 s with a ~500 ms lead-in
before the word, which is realistic (the button is pressed before you speak) and worth keeping.

**Batches.** Record by *condition*, one batch at a time, rather than mixing variation within a
run — delivery stays consistent inside a batch, and a bad batch is re-recorded rather than
hunted for. `--tag` names the condition: it becomes part of the directory name AND a column in
`takes.csv`, which also records duration, raw peak and in-band SNR per take. The condition is
not recoverable from the audio afterwards, so it has to be written at capture time.

The six batches actually recorded, in order. `S` is the five spell words; the port is `COM5`
on this Windows machine (`arduino-cli board list` to confirm — Windows keys the COM number to
the device, so it survives replugging into a different physical port).

```bash
S="lumos nox expelliarmus avada_kedavra expecto_patronum"

python tools/capture_audio.py COM5 session --speaker NAME --tag normal  --words $S --reps 20
python tools/capture_audio.py COM5 session --speaker NAME --tag gesture --words $S --reps 10
python tools/capture_audio.py COM5 session --speaker NAME --tag quiet   --words $S --reps 5
python tools/capture_audio.py COM5 session --speaker NAME --tag fast    --words $S --reps 5
python tools/capture_audio.py COM5 session --speaker NAME --tag still   --words silence other --reps 20
python tools/capture_audio.py COM5 session --speaker NAME --tag gesture --words silence --reps 20
```

**There is no `loud`/`dramatic` batch, deliberately.** The original plan had one, but the
`gesture` batch is performed at full theatrical delivery — casting with the wand *is*
theatrical, and nobody says "Lumos" loudly and flatly — so it already occupies that end of the
range and a separate batch would duplicate it. Measured spread bears this out: `quiet` lands
~20 dB in-band, `normal` ~25 dB, `gesture` 39-53 dB. The clipping rationale for a `loud` batch
is also gone: standing still peaks at 9% of full scale, so voice alone cannot approach the
ceiling from here — **motion** is the clipping risk, and `gesture` covers it.

Every run is independently resumable (progress is counted from files on disk, never from a
counter), so `q` out any time and rerun the same line. `n` skips to the next word, `r` redoes
the take you just recorded. A run walks the word list in order — all reps of one word, then the
next — so a single word per invocation is also fine and easier on the voice.

**Pilot every new speaker with 5 takes before they commit to 260.** It caught a 16 dB room-noise
problem on Daniel's first attempt. Worth it.

**Things that will otherwise surprise whoever resumes this:**

- **The `gesture` batches are not optional colour.** Deployment always involves motion, and the
  `gesture`+`silence` batch (casting with no words at all) covers the single most common real
  input: incantations are *per-spell optional*, so the model must confidently produce nothing
  for a gesture-only cast. Nothing else in the set teaches that.
- **The `gesture`+`silence` batch will trip "not silent, redo" on every take. Ignore it.** That
  threshold is calibrated on standing-still room tone, and motion noise legitimately exceeds it.
  Only ever act on `CLIPPED`. Same applies to low-level flags on the `quiet` batch: the audio is
  *supposed* to be quiet.
- **But a quiet take can still be genuinely bad, and the SNR number alone won't tell you.**
  Long words spoken softly decay across syllables until most of the word sits under the floor —
  a quiet `expelliarmus` measured 12.8 dB with only 100-140 ms of a ~1 s utterance actually
  above room tone. That is not quiet speech, it is a mostly-missing word, and it actively
  conflicts with the `silence` class it comes to resemble. The diagnostic that catches it is
  **voiced duration**, not level. Cue for the retake: *quiet but crisp* — keep the last syllable
  as strong as the first.
- **`MIC_GAIN` may change mid-set, and that is recoverable — but only because it is recorded.**
  Daniel's `normal` batch is at gain 40 and everything after at 28. Since the gain is a pure
  multiply, the two differ by an exactly invertible scalar. This only works because the gain is
  captured per take in `takes.csv`; before 2026-08-23 it wasn't, and a change would have been
  unrecoverable. Don't change it casually, but don't panic if it must move.
- **Second speaker is Alise, not Yuval** (2026-08-23). Deliberately a female voice: formants sit
  ~15-20% higher from a shorter vocal tract, and since MFCCs encode the spectral envelope that
  shift moves the model's inputs directly. A second male voice would add far less. Both
  fundamentals (male ~85-155 Hz, female ~165-255 Hz) sit *below* the 300 Hz band-pass either
  way, so it is the formants and harmonic spacing that differ in band, not the pitch itself.
  Adding Yuval as a third is still worthwhile if he's willing.
- **Repo size: resolved.** Audio is tracked with **git-lfs** (`*.wav` in `.gitattributes`).
  Daniel's set alone is 18 MB, so two speakers land near 36 MB — above the 25-30 MB originally
  estimated.
- **This whole set is provisional with respect to the FINAL acoustics.** Every take was recorded
  with a bare board in open air. In the finished wand the mic is sealed inside a 20 mm bore,
  which changes the speech channel itself — high-frequency rolloff plus cavity resonance — not
  just the noise. Most of the set's value transfers regardless (vocabulary, delivery range,
  speaker variation, the near-miss boundary in `other`), but **expect to fine-tune on a smaller
  set recorded through the assembled wand** rather than shipping a model trained purely on this.

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
