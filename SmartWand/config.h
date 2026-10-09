// Every pin assignment and tunable in one place -- they get retuned
// constantly during calibration (see CLAUDE.md Firmware conventions).
//
// The gesture thresholds are ported from tools/gesture_lab.py, which is the
// reference: tune there against recorded traces, then copy the numbers here.
// tests/gesture_parity.cpp fails if the two disagree on any recorded cast.
#pragma once

#include <stdint.h>

#ifdef ARDUINO
#include <Arduino.h>  // D0/D1. The host parity test defines them on the command line
#endif

// --- pins -------------------------------------------------------------------
const int PIN_TIP_LED = D0;      // WS2812B data. Corner pad, see CLAUDE.md Open decisions
const int PIN_CAST_BUTTON = D1;  // to GND, INPUT_PULLUP, pressed = LOW. Provisional pad

// --- sampling ---------------------------------------------------------------
const uint16_t IMU_SAMPLE_RATE_HZ = 104;
const uint32_t IMU_SAMPLE_PERIOD_US = 1000000UL / IMU_SAMPLE_RATE_HZ;
const uint32_t BUTTON_DEBOUNCE_MS = 30;    // bench-verified, ButtonTest and ImuTest

// Casts shorter than this are an accidental tap, not a cast: no spell fires.
const uint32_t MIN_CAST_MS = 150;

// The flip count needs the whole cast's main axis before it can run, so
// (gy, gz) are buffered. A cast longer than this keeps its running sums but
// stops buffering; only the flip count -- i.e. zigzag, a ~1-1.6 s gesture --
// depends on the buffer, so a long circle loses nothing. 8 B per sample.
const uint16_t MAX_CAST_SAMPLES = IMU_SAMPLE_RATE_HZ * 10;

// --- gesture thresholds (from tools/gesture_lab.py, 2026-10-09) -------------
// Ranges in the comments are what the 2026-08-16 traces measured, two people,
// 3-4 reps each, captured without the button. Provisional until button-gated
// casts are recorded -- see CLAUDE.md "Offline analysis (2026-10-09)".
const float DEADBAND_DPS = 75.0f;        // still hold: <=44 deg cum over 8 s at 75
const float MIN_CUM_DEG = 55.0f;         // below: no gesture
const float FAST_PEAK_DPS = 300.0f;      // flicks and zigzags are fast
const float PHI_SPLIT_DEG = 45.0f;       // |phi| below: pitch (flick), above: yaw (zigzag)
const float FLICK_MIN_PLANAR = 0.80f;    // flicks 0.93-1.00, thrust <=0.68
const float ZIGZAG_MIN_PLANAR = 0.50f;   // loose on purpose, only keeps circles (<=0.27) out
const uint8_t ZIGZAG_MIN_FLIPS = 2;      // zigzag exactly 2, everything else <=1
const float CIRCLE_MIN_CUM_DEG = 180.0f; // one circle 214-296, more circles only add
const float CIRCLE_MIN_SMOOTH = 0.55f;   // circle 0.64-0.91, thrust 0.26-0.48
const float LOBE_DPS = 250.0f;           // "strong" for first-lobe sign and flips
const float FLIP_SMOOTH_ALPHA = 0.4f;    // EMA weight in the flip counter

// --- tip LED ----------------------------------------------------------------
// Caps every effect. One WS2812B at full white is ~60 mA, comparable to the
// whole MCU, and Lumos never times out -- so this sets how long a lit wand
// lasts. PROVISIONAL: pick it from measured LedTest current, not by guess.
const uint8_t LED_MAX_BRIGHTNESS = 64;

const uint32_t NOX_FADE_MS = 250;
const uint32_t EXPELLIARMUS_MS = 300;
const uint32_t AVADA_KEDAVRA_MS = 600;
const uint32_t AVADA_STROBE_PERIOD_MS = 60;
const uint32_t PATRONUM_MS = 2000;
