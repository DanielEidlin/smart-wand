// Host-side parity test: the firmware's GestureEngine must agree with the
// Python reference in tools/gesture_lab.py on every recorded cast, plus the
// synthetic stress variants (twisted grip, faster/slower, steeper zigzags).
//
// Build and run from the repo root (any C++14 compiler; g++ shown):
//   python tools/gesture_lab.py --dump-windows tests/windows.csv
//   g++ -std=c++14 -O2 -DD0=0 -DD1=1 -ISmartWand -o tests/gesture_parity tests/gesture_parity.cpp SmartWand/gestures.cpp
//   tests/gesture_parity tests/windows.csv
// Both outputs are build artifacts and gitignored.
//
// The -DD0/-DD1 stand in for the Arduino pin macros config.h names; the
// engine itself never touches a pin. Exit code is non-zero on any mismatch.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gestures.h"

namespace {

struct Expected {
  char name[96];
  char cls[16];
  double dur, peak, cum, planar, phi, y1, smooth;
  int flips;
};

// float firmware vs double Python: allow a little relative slack
bool close(double got, double want, double absTol) {
  return fabs(got - want) <= absTol + 1e-3 * fabs(want);
}

int failures = 0, windows = 0;

void check(const Expected& e, GestureEngine& eng) {
  GestureResult r = eng.endCast();
  const GestureFeatures& f = eng.features();
  windows++;
  bool ok = strcmp(gestureName(r.gesture), e.cls) == 0 && f.flips == e.flips &&
            close(f.durS, e.dur, 1e-3) && close(f.peakDps, e.peak, 0.01) &&
            close(f.cumDeg, e.cum, 0.05) && close(f.planar, e.planar, 1e-3) &&
            close(f.phiDeg, e.phi, 0.05) && close(f.y1, e.y1, 0.05) &&
            close(f.smooth, e.smooth, 1e-3);
  if (!ok) {
    failures++;
    printf("MISMATCH %s\n  python: %-10s dur=%.3f peak=%.1f cum=%.1f planar=%.3f phi=%.1f "
           "y1=%.1f flips=%d smooth=%.3f\n  c++:    %-10s dur=%.3f peak=%.1f cum=%.1f "
           "planar=%.3f phi=%.1f y1=%.1f flips=%d smooth=%.3f\n",
           e.name, e.cls, e.dur, e.peak, e.cum, e.planar, e.phi, e.y1, e.flips, e.smooth,
           gestureName(r.gesture), f.durS, f.peakDps, f.cumDeg, f.planar, f.phiDeg, f.y1,
           f.flips, f.smooth);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s windows.csv\n", argv[0]);
    return 2;
  }
  FILE* in = fopen(argv[1], "r");
  if (!in) {
    perror(argv[1]);
    return 2;
  }

  GestureEngine eng;
  Expected e;
  bool open = false;
  char line[512];
  while (fgets(line, sizeof line, in)) {
    if (line[0] == 'W') {
      if (open) check(e, eng);
      if (sscanf(line, "W,%95[^,],%15[^,],%lf,%lf,%lf,%lf,%lf,%lf,%d,%lf", e.name, e.cls,
                 &e.dur, &e.peak, &e.cum, &e.planar, &e.phi, &e.y1, &e.flips,
                 &e.smooth) != 10) {
        fprintf(stderr, "bad window line: %s", line);
        return 2;
      }
      eng.beginCast();
      open = true;
    } else if (line[0] == 'S') {
      double ms, v[6];
      sscanf(line, "S,%lf,%lf,%lf,%lf,%lf,%lf,%lf", &ms, &v[0], &v[1], &v[2], &v[3], &v[4],
             &v[5]);
      ImuSample s = {(uint32_t)llround(ms), (float)v[0], (float)v[1], (float)v[2],
                     (float)v[3], (float)v[4], (float)v[5]};
      eng.addSample(s);
    }
  }
  if (open) check(e, eng);
  fclose(in);

  printf("%d/%d windows match the Python reference\n", windows - failures, windows);
  return failures ? 1 : 0;
}
