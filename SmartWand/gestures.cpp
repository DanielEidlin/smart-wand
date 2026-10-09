// Port of tools/gesture_lab.py features() and classify(). Keep the two in
// step: change a rule there first, then here, then run tests/gesture_parity.
#include "gestures.h"

#include <math.h>

#include "config.h"

namespace {

float gyBuf[MAX_CAST_SAMPLES];
float gzBuf[MAX_CAST_SAMPLES];

const float RAD2DEG = 57.29577951f;

float mag3(float x, float y, float z) { return sqrtf(x * x + y * y + z * z); }

}  // namespace

const char* gestureName(Gesture g) {
  switch (g) {
    case Gesture::FlickUp:   return "flick_up";
    case Gesture::FlickDown: return "flick_down";
    case Gesture::Thrust:    return "thrust";
    case Gesture::Zigzag:    return "zigzag";
    case Gesture::Circle:    return "circle";
    default:                 return "none";
  }
}

void GestureEngine::beginCast() {
  *this = GestureEngine();
}

void GestureEngine::addSample(const ImuSample& s) {
  float m = mag3(s.gx, s.gy, s.gz);

  if (n_ == 0) {
    firstMs_ = s.ms;
  } else {
    // cumulative rotation: each sample's speed times the gap to the next
    float pm = mag3(prev_.gx, prev_.gy, prev_.gz);
    if (pm > DEADBAND_DPS) cum_ += pm * (s.ms - prev_.ms) / 1000.0f;
  }
  if (m > peak_) peak_ = m;
  if (m > DEADBAND_DPS) {
    movingSum_ += m;
    movingN_++;
  }
  syy_ += s.gy * s.gy;
  szz_ += s.gz * s.gz;
  syz_ += s.gy * s.gz;

  if (!lobeDone_) {
    if (m > LOBE_DPS && (!haveLobe_ || m > lobeMag_)) {
      haveLobe_ = true;
      lobeMag_ = m;
      lobeGy_ = s.gy;
      lobeGz_ = s.gz;
    } else if (haveLobe_ && m < LOBE_DPS / 2) {
      lobeDone_ = true;
    }
  }

  if (buffered_ < MAX_CAST_SAMPLES) {
    gyBuf[buffered_] = s.gy;
    gzBuf[buffered_] = s.gz;
    buffered_++;
  }

  prev_ = s;
  n_++;
}

GestureResult GestureEngine::endCast() {
  feat_ = {};
  if (n_ == 0) return {Gesture::None, 0};

  // principal direction of the (gy, gz) rotation vectors: 2x2 eigenproblem
  float tr = syy_ + szz_;
  float det = syy_ * szz_ - syz_ * syz_;
  float disc = tr * tr / 4 - det;
  float l1 = tr / 2 + sqrtf(disc > 0 ? disc : 0);
  float phi = 0.5f * atan2f(2 * syz_, syy_ - szz_);  // [-90, 90] deg, cos >= 0
  float c = cosf(phi), sn = sinf(phi);

  // flips: sign changes of the smoothed rotation along that axis
  uint8_t flips = 0;
  int8_t last = 0;
  float f = 0;
  for (uint16_t i = 0; i < buffered_; i++) {
    float p = gyBuf[i] * c + gzBuf[i] * sn;
    f = i == 0 ? p : FLIP_SMOOTH_ALPHA * p + (1 - FLIP_SMOOTH_ALPHA) * f;
    if (fabsf(f) > LOBE_DPS) {
      int8_t sign = f > 0 ? 1 : -1;
      if (last && sign != last && flips < 255) flips++;
      last = sign;
    }
  }

  feat_.durS = (prev_.ms - firstMs_) / 1000.0f;
  feat_.peakDps = peak_;
  feat_.cumDeg = cum_;
  feat_.planar = tr > 0 ? (2 * l1 - tr) / tr : 0;
  feat_.phiDeg = phi * RAD2DEG;
  feat_.y1 = haveLobe_ ? lobeGy_ * c + lobeGz_ * sn : 0;
  feat_.flips = flips;
  feat_.smooth = movingN_ && peak_ > 0 ? (movingSum_ / movingN_) / peak_ : 0;

  const GestureFeatures& F = feat_;
  Gesture g;
  if (F.cumDeg < MIN_CUM_DEG) {
    g = Gesture::None;
  } else if (F.peakDps >= FAST_PEAK_DPS && fabsf(F.phiDeg) >= PHI_SPLIT_DEG &&
             F.planar >= ZIGZAG_MIN_PLANAR && F.flips >= ZIGZAG_MIN_FLIPS) {
    g = Gesture::Zigzag;
  } else if (F.peakDps >= FAST_PEAK_DPS && fabsf(F.phiDeg) < PHI_SPLIT_DEG &&
             F.planar >= FLICK_MIN_PLANAR) {
    g = F.y1 < 0 ? Gesture::FlickUp : Gesture::FlickDown;
  } else if (F.cumDeg >= CIRCLE_MIN_CUM_DEG && F.smooth >= CIRCLE_MIN_SMOOTH) {
    g = Gesture::Circle;
  } else {
    g = Gesture::Thrust;
  }
  return {g, g == Gesture::None ? 0.0f : 1.0f};
}
