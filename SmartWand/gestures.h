// GestureEngine: the seam between "a cast's IMU samples" and "which gesture
// was it". This is the heuristic rung (CLAUDE.md: thresholds -> DTW -> ML);
// a later rung replaces the implementation behind the same three calls.
//
// Deliberately free of Arduino headers so tests/gesture_parity.cpp can
// compile it on the laptop and check it against tools/gesture_lab.py.
#pragma once

#include <stdint.h>

enum class Gesture : uint8_t { None, FlickUp, FlickDown, Thrust, Zigzag, Circle };

const char* gestureName(Gesture g);

struct ImuSample {
  uint32_t ms;
  float ax, ay, az;  // g
  float gx, gy, gz;  // deg/s
};

// Per-cast features, exposed for logging and the parity test. Definitions
// match tools/gesture_lab.py's docstring.
struct GestureFeatures {
  float durS;
  float peakDps;
  float cumDeg;
  float planar;
  float phiDeg;
  float y1;
  uint8_t flips;
  float smooth;
};

struct GestureResult {
  Gesture gesture;
  // The heuristic rung has no calibrated confidence: 1 when a rule matched,
  // 0 for None. The field exists for the DTW/ML rungs to fill properly.
  float confidence;
};

class GestureEngine {
 public:
  void beginCast();
  void addSample(const ImuSample& s);
  GestureResult endCast();
  const GestureFeatures& features() const { return feat_; }

 private:
  uint32_t n_ = 0;
  uint32_t firstMs_ = 0;
  ImuSample prev_ = {};

  float peak_ = 0, cum_ = 0;
  float movingSum_ = 0;
  uint32_t movingN_ = 0;
  float syy_ = 0, szz_ = 0, syz_ = 0;

  // first strong lobe: the strongest sample of the first run above LOBE_DPS
  bool lobeDone_ = false, haveLobe_ = false;
  float lobeMag_ = 0, lobeGy_ = 0, lobeGz_ = 0;

  // (gy, gz) per sample, for the flip count -- see MAX_CAST_SAMPLES
  uint16_t buffered_ = 0;

  GestureFeatures feat_ = {};
};
