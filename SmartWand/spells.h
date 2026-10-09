// The spell table. A spell is (gesture, optional incantation) from the first
// commit -- the incantation field exists now and goes unused until Phase 4,
// and stays per-spell optional: nullptr means gesture-only. Dropping Nox's
// at-risk incantation, for example, is a one-word change here.
#pragma once

#include "effects.h"
#include "gestures.h"

struct Spell {
  const char* name;
  Gesture gesture;
  const char* incantation;  // keyword-spotter label, or nullptr. Unused before Phase 4
  Effect effect;
};

const Spell SPELLS[] = {
    {"Lumos", Gesture::FlickUp, "lumos", Effect::Lumos},
    {"Nox", Gesture::FlickDown, "nox", Effect::Nox},
    {"Expelliarmus", Gesture::Thrust, "expelliarmus", Effect::Expelliarmus},
    {"Avada Kedavra", Gesture::Zigzag, "avada_kedavra", Effect::AvadaKedavra},
    {"Expecto Patronum", Gesture::Circle, "expecto_patronum", Effect::ExpectoPatronum},
};

// Phases 1-3 match on gesture alone. `heard` is the keyword spotter's label
// for this cast (nullptr = nothing recognised); Phase 4 decides how it combines
// with the gesture -- the signature is here so call sites don't change then.
inline const Spell* findSpell(Gesture g, const char* heard) {
  (void)heard;
  for (const Spell& s : SPELLS) {
    if (s.gesture == g) return &s;
  }
  return nullptr;
}
