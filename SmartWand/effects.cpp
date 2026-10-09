#include "effects.h"

#include <math.h>

#include "config.h"

namespace {

// Full-scale colours; show() applies LED_MAX_BRIGHTNESS. WS2812B colour on
// a sagging 3.7 V supply is still unverified (CLAUDE.md Electrical
// constraints), so expect to retune these on the real tip.
const uint8_t WARM_WHITE[3] = {255, 170, 90};
const uint8_t RED[3] = {255, 20, 0};
const uint8_t GREEN[3] = {20, 255, 30};
const uint8_t BLUE_SILVER[3] = {150, 190, 255};

const uint32_t LUMOS_FADE_IN_MS = 150;
const uint32_t EXPELLIARMUS_WHITE_MS = 60;
const uint32_t PATRONUM_FADE_IN_MS = 200;
const uint32_t PATRONUM_FADE_OUT_MS = 400;

uint8_t lerp8(uint8_t a, uint8_t b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  return (uint8_t)(a + (b - a) * t + 0.5f);
}

}  // namespace

Effects::Effects() : pixel_(1, PIN_TIP_LED, NEO_GRB + NEO_KHZ800) {}

void Effects::begin() {
  pixel_.begin();
  pixel_.setPixelColor(0, 0, 0, 0);
  pixel_.show();
}

Effects::Rgb Effects::base() const {
  return lit_ ? Rgb{WARM_WHITE[0], WARM_WHITE[1], WARM_WHITE[2]} : Rgb{0, 0, 0};
}

void Effects::play(Effect e, uint32_t nowMs) {
  fadeFrom_ = shown_;
  active_ = e;
  startMs_ = nowMs;
  if (e == Effect::Lumos) lit_ = true;
  if (e == Effect::Nox) lit_ = false;
}

void Effects::update(uint32_t nowMs) {
  uint32_t t = nowMs - startMs_;
  Rgb b = base();

  switch (active_) {
    case Effect::None:
      show(b);
      return;

    case Effect::Lumos: {  // quick fade up, then steady forever (no timeout)
      float k = (float)t / LUMOS_FADE_IN_MS;
      show({lerp8(fadeFrom_.r, b.r, k), lerp8(fadeFrom_.g, b.g, k), lerp8(fadeFrom_.b, b.b, k)});
      if (t >= LUMOS_FADE_IN_MS) active_ = Effect::None;
      return;
    }

    case Effect::Nox: {  // fast fade, ends FULLY off: 0,0,0 actually written
      float k = (float)t / NOX_FADE_MS;
      show({lerp8(fadeFrom_.r, 0, k), lerp8(fadeFrom_.g, 0, k), lerp8(fadeFrom_.b, 0, k)});
      if (t >= NOX_FADE_MS) active_ = Effect::None;
      return;
    }

    case Effect::Expelliarmus: {  // white crack, then red decaying to base
      if (t < EXPELLIARMUS_WHITE_MS) {
        show({255, 255, 255});
      } else {
        float k = (float)(t - EXPELLIARMUS_WHITE_MS) / (EXPELLIARMUS_MS - EXPELLIARMUS_WHITE_MS);
        show({lerp8(RED[0], b.r, k), lerp8(RED[1], b.g, k), lerp8(RED[2], b.b, k)});
      }
      if (t >= EXPELLIARMUS_MS) active_ = Effect::None;
      return;
    }

    case Effect::AvadaKedavra: {  // harsh on/off strobe
      bool on = (t % AVADA_STROBE_PERIOD_MS) < AVADA_STROBE_PERIOD_MS / 2;
      show(on ? Rgb{GREEN[0], GREEN[1], GREEN[2]} : Rgb{0, 0, 0});
      if (t >= AVADA_KEDAVRA_MS) active_ = Effect::None;
      return;
    }

    case Effect::ExpectoPatronum: {  // shimmer: two beating sines on blue-silver
      float s = t / 1000.0f;
      float level = 0.6f + 0.25f * sinf(2 * PI * 3.0f * s) + 0.15f * sinf(2 * PI * 7.3f * s);
      Rgb c = {(uint8_t)(BLUE_SILVER[0] * level), (uint8_t)(BLUE_SILVER[1] * level),
               (uint8_t)(BLUE_SILVER[2] * level)};
      if (t < PATRONUM_FADE_IN_MS) {
        float k = (float)t / PATRONUM_FADE_IN_MS;
        c = {lerp8(fadeFrom_.r, c.r, k), lerp8(fadeFrom_.g, c.g, k), lerp8(fadeFrom_.b, c.b, k)};
      } else if (t > PATRONUM_MS - PATRONUM_FADE_OUT_MS) {
        float k = (float)(t - (PATRONUM_MS - PATRONUM_FADE_OUT_MS)) / PATRONUM_FADE_OUT_MS;
        c = {lerp8(c.r, b.r, k), lerp8(c.g, b.g, k), lerp8(c.b, b.b, k)};
      }
      show(c);
      if (t >= PATRONUM_MS) active_ = Effect::None;
      return;
    }
  }
}

void Effects::show(Rgb c) {
  if (c.r == shown_.r && c.g == shown_.g && c.b == shown_.b) return;  // skip redundant writes
  shown_ = c;
  // Scale here rather than with setBrightness(), which is lossy and global.
  pixel_.setPixelColor(0, (uint16_t)c.r * LED_MAX_BRIGHTNESS / 255,
                       (uint16_t)c.g * LED_MAX_BRIGHTNESS / 255,
                       (uint16_t)c.b * LED_MAX_BRIGHTNESS / 255);
  pixel_.show();
}
