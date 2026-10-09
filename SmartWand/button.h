// Debounced cast button. Same logic as bringup/ButtonTest, which was
// bench-verified against the real momentary switch on 2026-10-09.
#pragma once

#include <Arduino.h>

#include "config.h"

enum class ButtonEdge : uint8_t { None, Pressed, Released };

class DebouncedButton {
 public:
  explicit DebouncedButton(int pin) : pin_(pin) {}

  void begin() { pinMode(pin_, INPUT_PULLUP); }

  ButtonEdge update(uint32_t nowMs) {
    bool raw = digitalRead(pin_);
    if (raw != lastRaw_) {
      lastRaw_ = raw;
      lastEdgeMs_ = nowMs;
    }
    if (raw == stable_ || nowMs - lastEdgeMs_ < BUTTON_DEBOUNCE_MS) {
      return ButtonEdge::None;
    }
    stable_ = raw;
    return stable_ == LOW ? ButtonEdge::Pressed : ButtonEdge::Released;
  }

 private:
  int pin_;
  bool stable_ = HIGH;  // HIGH = released (pull-up), LOW = pressed
  bool lastRaw_ = HIGH;
  uint32_t lastEdgeMs_ = 0;
};
