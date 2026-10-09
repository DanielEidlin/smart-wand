// Tip LED effects, all millis()-driven -- update() is called every loop and
// never blocks, so sampling keeps running while an effect plays.
//
// Two layers: a persistent base (lit by Lumos, extinguished by Nox) and a
// transient effect on top. When a transient finishes, the LED returns to the
// base, so Expelliarmus cast while Lumos burns flashes and then relights.
#pragma once

#include <Adafruit_NeoPixel.h>

enum class Effect : uint8_t { None, Lumos, Nox, Expelliarmus, AvadaKedavra, ExpectoPatronum };

class Effects {
 public:
  Effects();
  void begin();
  void play(Effect e, uint32_t nowMs);
  void update(uint32_t nowMs);
  bool lit() const { return lit_; }

 private:
  struct Rgb {
    uint8_t r, g, b;
  };
  Rgb base() const;
  void show(Rgb c);

  Adafruit_NeoPixel pixel_;
  Effect active_ = Effect::None;
  uint32_t startMs_ = 0;
  bool lit_ = false;
  Rgb fadeFrom_ = {0, 0, 0};
  Rgb shown_ = {0, 0, 0};
};
