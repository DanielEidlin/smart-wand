# Spell spec — prior art and rationale

Background for the decisions recorded in `CLAUDE.md`. The spell table, the gesture
discriminators, and the double-tap arming decision live there, because they are referenced
constantly while writing gesture and effect code. This file holds the research behind them,
which is read once.

**Its purpose is to stop these questions being re-opened.** Both conclusions below cost real
research time; neither needs repeating.

## There is no canonical wand movement for these spells

The source material does not specify wand motions in any usable detail. The books give exactly
one description — "swish and flick", for *Wingardium Leviosa* — and nothing for *Lumos*, *Nox*,
*Expelliarmus*, *Avada Kedavra*, or *Expecto Patronum*. The films, games, and theme-park
attractions each invent their own motions and contradict one another. There is no authority to
defer to.

So **the gestures are an engineering choice, chosen for sensor separability.** This is not a
compromise or a shortcut; it is what the one professionally-tuned implementation did too.
Universal's park wands use a triangle, a swirl, and similar abstract shapes, and their staff
coach visitors to work at wrist scale — "like writing with a pen" — rather than swinging the arm.

Consequences already reflected in `CLAUDE.md`:

- The five shapes are picked so each dominates a different feature: rotation sign, accelerometer-
  vs-gyroscope dominance, direction-reversal count, and total angular displacement.
- *Avada Kedavra* is a **zigzag**, not the point-and-stab the films depict, specifically so it
  cannot collide with the *Expelliarmus* thrust. Two "point and stab" gestures would be the
  obvious design and would be unseparable in practice.
- All five are **wrist-scale**, following Universal's field-tested advice, which puts most of the
  signal in the gyroscope.

## Neither commercial motion wand does free-running recognition

This is the finding that drove the arming decision. Both shipped products make the user
explicitly declare that a cast is starting:

| Product | How a cast is declared |
| --- | --- |
| Kano Coding Wand | A button held down for the duration of the motion |
| Universal park wands | Standing at a fixed medallion, aiming at an IR camera |

Neither watches continuously for spell-shaped motion. That is a strong signal: continuous
recognition means every gesture competes against ordinary handling — putting the wand down,
gesturing while talking, walking with it — and the false-positive rate is what kills it.

Given that, the only question was *which* declaration mechanism. **Double-tap won** because it is
the only option that adds nothing physical:

- No button, so no extra part, no pin, and **no hole in the 20 mm bore** — which is already tight
  enough that pin headers don't fit.
- The LSM6DS3TR-C detects taps in hardware, so it costs no CPU and works while the MCU sleeps.
- It doubles as the **Phase 4 microphone gate**. Voice needs a trigger anyway (always-on keyword
  spotting would destroy the runtime target), and "raise the wand, double-tap, speak, cast" is one
  interaction rather than two mechanisms bolted together.

The mechanism is confirmed feasible against the installed library — see the tap bullet under
**Board gotchas** in `CLAUDE.md` for the register-level details and traps.

**The open risk is tap reliability through the finished wand.** Tap thresholds tuned on a bare
board may not survive being epoxied into a 20 mm wooden bore, where the enclosure damps the
impulse. `bringup/TapTest/` exists to settle this, and is scheduled early in Phase 1 precisely
because a negative result reopens the cast-trigger decision.
