// SelfTest -- post-mishap hardware check for the XIAO nRF52840 Sense.
//
// Written 2026-08-23 after the board was flexed while removing a rubber band.
// The failure that worries you after bending a board is not a dead chip, it is
// a CRACKED SOLDER JOINT: the part still powers up and still answers, but one
// axis floats, or a reading freezes, or it works until it is flexed again. So
// "the IMU responded" is NOT a pass here. Every check below demands that the
// data be alive (changing sample to sample) and physically sane (gravity is
// 1 g, a resting gyro reads ~0), because those are what a cracked joint breaks
// while leaving the ID register perfectly readable.
//
// Run:
//   arduino-cli compile --fqbn Seeeduino:nrf52:xiaonRF52840Sense bringup/SelfTest
//   arduino-cli upload -p COM5 --fqbn Seeeduino:nrf52:xiaonRF52840Sense bringup/SelfTest
//   python tools/read_serial.py COM5        (arduino-cli monitor needs a TTY)

#include <Adafruit_TinyUSB.h>   // required or `Serial` will not link on this core
#include <Wire.h>
#include "LSM6DS3.h"
#include <PDM.h>

LSM6DS3 imu(I2C_MODE, 0x6A);

int passCount = 0, failCount = 0, warnCount = 0;

void report(const char *name, bool ok, const char *detail) {
  Serial.print(ok ? "PASS  " : "FAIL  ");
  Serial.print(name);
  Serial.print("  ");
  Serial.println(detail);
  if (ok) passCount++; else failCount++;
}

void warn(const char *name, const char *detail) {
  Serial.print("WARN  ");
  Serial.print(name);
  Serial.print("  ");
  Serial.println(detail);
  warnCount++;
}

// ---------------------------------------------------------------- IMU

static bool imuWhoAmI() {
  // Do NOT probe the bus with raw Wire1 calls. An earlier version of this
  // sketch did (beginTransmission/endTransmission to 0x6A) and hung the board
  // dead in setup(): the nRF52 TWIM peripheral blocks with no timeout, so a
  // bus that does not answer exactly as expected never returns, and the sketch
  // produces no output at all -- which reads exactly like dead hardware. Use
  // the path ImuTest has had bench-verified since 2026-08-16 instead: drive
  // the power pin, then let the library do its own Wire1 handling.
  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
  delay(100);  // let the IMU power rail settle before talking to it

  char buf[64];
  if (imu.begin() != 0) {
    report("IMU begin", false, "imu.begin() failed -- no response on Wire1");
    return false;
  }
  report("IMU begin", true, "library initialised the IMU on Wire1");

  uint8_t who = 0;
  imu.readRegister(&who, LSM6DS3_ACC_GYRO_WHO_AM_I_REG);
  snprintf(buf, sizeof buf, "WHO_AM_I = 0x%02X (expect 0x6A)", who);
  bool ok = (who == 0x6A);
  report("IMU identity", ok, buf);
  return ok;
}

// Sample the IMU repeatedly and check the data is alive and physically sane.
// A cracked joint typically shows up here, not in WHO_AM_I.
static void imuLiveData() {
  char buf[128];   // imu.begin() already succeeded in imuWhoAmI()

  // Throw away the first samples. Straight after imu.begin() the gyro emits a
  // large startup transient -- measured 2026-08-23, a single outlier stretched
  // the reported gz range at rest to 457 dps while the mean stayed near 5 dps,
  // which looks alarming and means nothing. The steady-state stream a second
  // later sits inside +-3.4 dps.
  for (int i = 0; i < 20; i++) {
    (void)imu.readFloatGyroX(); (void)imu.readFloatAccelX();
    delay(10);
  }

  const int N = 64;
  float ax[N], ay[N], az[N], gx[N], gy[N], gz[N];
  for (int i = 0; i < N; i++) {
    ax[i] = imu.readFloatAccelX(); ay[i] = imu.readFloatAccelY(); az[i] = imu.readFloatAccelZ();
    gx[i] = imu.readFloatGyroX();  gy[i] = imu.readFloatGyroY();  gz[i] = imu.readFloatGyroZ();
    delay(10);
  }

  // 1. gravity check -- at rest the accel vector magnitude must be ~1 g,
  //    whatever orientation the board is in. This is the single best test that
  //    all three accel axes are connected and scaled: a dead axis reading 0
  //    drags the magnitude well off 1.0.
  float sum = 0;
  for (int i = 0; i < N; i++) sum += sqrtf(ax[i]*ax[i] + ay[i]*ay[i] + az[i]*az[i]);
  float gMag = sum / N;
  snprintf(buf, sizeof buf, "|accel| = %.3f g at rest (expect 0.90-1.10)", gMag);
  report("Accel gravity magnitude", gMag > 0.90f && gMag < 1.10f, buf);

  // 2. liveness -- a real MEMS sensor is never perfectly still. Zero spread
  //    across 64 samples means a frozen register read, i.e. the bus works but
  //    the sensor is not converting.
  float spread[6] = {0,0,0,0,0,0};
  float *chan[6] = {ax, ay, az, gx, gy, gz};
  const char *names[6] = {"ax","ay","az","gx","gy","gz"};
  for (int c = 0; c < 6; c++) {
    float lo = chan[c][0], hi = chan[c][0];
    for (int i = 1; i < N; i++) { if (chan[c][i] < lo) lo = chan[c][i]; if (chan[c][i] > hi) hi = chan[c][i]; }
    spread[c] = hi - lo;
  }
  bool alive = true;
  String dead = "";
  for (int c = 0; c < 6; c++) if (spread[c] <= 0.0f) { alive = false; dead += String(names[c]) + " "; }
  if (alive) {
    snprintf(buf, sizeof buf, "all 6 axes show sample-to-sample noise");
    report("IMU liveness", true, buf);
  } else {
    snprintf(buf, sizeof buf, "FROZEN axes: %s -- reads are constant, sensor not converting", dead.c_str());
    report("IMU liveness", false, buf);
  }

  // 3. resting gyro -- should sit near zero. A large offset on one axis is a
  //    classic damaged/stressed-die symptom.
  float gAvg[3] = {0,0,0};
  for (int i = 0; i < N; i++) { gAvg[0] += gx[i]; gAvg[1] += gy[i]; gAvg[2] += gz[i]; }
  for (int c = 0; c < 3; c++) gAvg[c] /= N;
  float worst = 0;
  for (int c = 0; c < 3; c++) if (fabsf(gAvg[c]) > worst) worst = fabsf(gAvg[c]);
  snprintf(buf, sizeof buf, "resting bias gx=%.1f gy=%.1f gz=%.1f dps (worst %.1f, expect <15)",
           gAvg[0], gAvg[1], gAvg[2], worst);
  report("Gyro resting bias", worst < 15.0f, buf);

  Serial.print("      per-axis spread at rest: ");
  for (int c = 0; c < 6; c++) { Serial.print(names[c]); Serial.print("="); Serial.print(spread[c], 3); Serial.print(" "); }
  Serial.println();
}

// ---------------------------------------------------------------- PDM mic

volatile int pdmBytes = 0;
int16_t pdmBuf[512];
volatile int pdmCount = 0;

void onPDMdata() {
  int n = PDM.available();
  if (n > (int)sizeof(pdmBuf)) n = sizeof(pdmBuf);
  PDM.read(pdmBuf, n);
  pdmBytes += n;
  pdmCount = n / 2;
}

static void micTest() {
  char buf[128];
  PDM.onReceive(onPDMdata);
  if (!PDM.begin(1, 16000)) {
    report("Mic PDM.begin", false, "PDM.begin() returned false");
    return;
  }
  PDM.setGain(28);          // must come AFTER begin(); begin() sets its own default
  delay(300);
  pdmBytes = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 700) { delay(1); }

  snprintf(buf, sizeof buf, "%d bytes in 700 ms (expect ~22400 at 16 kHz mono)", pdmBytes);
  report("Mic data flowing", pdmBytes > 8000, buf);

  // Sanity on the samples themselves: real mic output is noisy and centred
  // near zero. All-zero means a dead or unclocked mic; railed means a broken
  // bias or a cracked joint pulling the data line.
  long sum = 0; int32_t lo = 32767, hi = -32768;
  int n = pdmCount ? pdmCount : 0;
  for (int i = 0; i < n; i++) { sum += pdmBuf[i]; if (pdmBuf[i] < lo) lo = pdmBuf[i]; if (pdmBuf[i] > hi) hi = pdmBuf[i]; }
  if (n > 0) {
    double mean = (double)sum / n;
    double var = 0;
    for (int i = 0; i < n; i++) { double d = pdmBuf[i] - mean; var += d * d; }
    double rms = sqrt(var / n);
    snprintf(buf, sizeof buf, "rms=%.1f mean=%.1f range=[%ld..%ld]", rms, mean, (long)lo, (long)hi);
    report("Mic samples sane", rms > 1.0 && hi < 32000 && lo > -32000, buf);
  } else {
    report("Mic samples sane", false, "no samples captured");
  }
  PDM.end();
}

// ---------------------------------------------------------------- battery

static void batteryTest() {
  char buf[96];
  // initVariant() drives VBAT_ENABLE HIGH, which DISABLES reading. Drive LOW.
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(20);
  long acc = 0;
  for (int i = 0; i < 16; i++) { acc += analogRead(PIN_VBAT); delay(2); }
  float raw = acc / 16.0f;
  float v = raw * (3.0f / 4096.0f) * (1510.0f / 510.0f);
  snprintf(buf, sizeof buf, "raw=%.0f -> %.2f V (USB-powered, no cell: any plausible reading is fine)", raw, v);
  // With no battery attached this reads whatever the divider floats to, so
  // this is an ADC-alive check, not a voltage check. Uncalibrated by design.
  report("Battery ADC responds", raw > 10 && raw < 4090, buf);
  digitalWrite(VBAT_ENABLE, HIGH);
}

// ---------------------------------------------------------------- LEDs

static void ledTest() {
  // Onboard RGB is ACTIVE-LOW: writing LOW lights it.
  const int pins[3] = {LED_RED, LED_GREEN, LED_BLUE};
  const char *cols[3] = {"RED", "GREEN", "BLUE"};
  for (int i = 0; i < 3; i++) { pinMode(pins[i], OUTPUT); digitalWrite(pins[i], HIGH); }
  Serial.println("      watch the onboard LED now:");
  for (int i = 0; i < 3; i++) {
    Serial.print("        "); Serial.println(cols[i]);
    digitalWrite(pins[i], LOW);
    delay(700);
    digitalWrite(pins[i], HIGH);
    delay(200);
  }
  Serial.println("      (LED check is VISUAL -- the firmware cannot verify it)");
}

// ---------------------------------------------------------------- free pins

static void pinTest() {
  // A bent board can short a castellated pad. Each free pin is pulled up and
  // read: a pin stuck LOW with nothing attached suggests a solder bridge or a
  // damaged pad. D0 drives the tip LED and D1 is the cast button, so anything
  // actually wired up will read LOW legitimately -- hence WARN, not FAIL.
  const int pins[4] = {D0, D1, D2, D3};
  const char *names[4] = {"D0", "D1", "D2", "D3"};
  char buf[96];
  String low = "";
  for (int i = 0; i < 4; i++) {
    pinMode(pins[i], INPUT_PULLUP);
    delay(5);
    if (digitalRead(pins[i]) == LOW) { low += String(names[i]) + " "; }
  }
  if (low.length() == 0) {
    report("Free pins D0-D3 float high", true, "all four read HIGH with pull-ups");
  } else {
    snprintf(buf, sizeof buf, "reading LOW: %s -- expected if something is wired there, else a short",
             low.c_str());
    warn("Free pins D0-D3", buf);
  }
}

// ----------------------------------------------------------------

void runAllTests() {
  passCount = failCount = warnCount = 0;

  Serial.println();
  Serial.println("=== XIAO nRF52840 Sense self-test ===");
  Serial.println("Put the board flat and STILL on the desk. Starting in 2 s.");
  delay(2000);
  Serial.println();

  Serial.println("-- IMU --");
  if (imuWhoAmI()) imuLiveData();

  Serial.println("-- Microphone --");
  micTest();

  Serial.println("-- Battery sense --");
  batteryTest();

  Serial.println("-- Free pins --");
  pinTest();

  Serial.println("-- Onboard RGB LED --");
  ledTest();

  Serial.println();
  Serial.print("=== ");
  Serial.print(passCount); Serial.print(" passed, ");
  Serial.print(failCount); Serial.print(" failed, ");
  Serial.print(warnCount); Serial.println(" warnings ===");
  if (failCount == 0) {
    Serial.println("Board looks healthy. Move it around and watch the live stream below;");
    Serial.println("all six numbers should respond, and return near rest when you stop.");
  } else {
    Serial.println("Something failed above -- see the detail on that line.");
  }
  Serial.println();
  Serial.println("Send 'r' to re-run the tests.");
  Serial.println("ax,ay,az,gx,gy,gz");
}

void setup() {
  Serial.begin(115200);
  // Block until the host actually opens the port. Without this the tests run
  // during USB enumeration and their output is lost -- which matters here
  // because nobody may be at the bench to press reset and catch it. `r` in
  // loop() re-runs them, so a late connection is recoverable either way.
  while (!Serial) delay(10);
  delay(300);
  runAllTests();
}

void loop() {
  if (Serial.available() && Serial.read() == 'r') {
    runAllTests();
  }
  // Live stream so the motion response of every axis can be eyeballed. A
  // cracked joint that survives a static test often shows up as an axis that
  // stops responding, or glitches, when the board is moved or flexed.
  Serial.print(imu.readFloatAccelX(), 3); Serial.print(',');
  Serial.print(imu.readFloatAccelY(), 3); Serial.print(',');
  Serial.print(imu.readFloatAccelZ(), 3); Serial.print(',');
  Serial.print(imu.readFloatGyroX(), 1);  Serial.print(',');
  Serial.print(imu.readFloatGyroY(), 1);  Serial.print(',');
  Serial.println(imu.readFloatGyroZ(), 1);
  delay(100);
}
