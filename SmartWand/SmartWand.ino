// SmartWand main firmware: an explicit IDLE <-> CASTING state machine on the
// cast button's press/release edges (CLAUDE.md "Designing for voice").
//
//   IDLE     -- effects keep running; nothing is sampled.
//   CASTING  -- entered on press. IMU samples at 104 Hz feed the gesture
//               engine. Phase 4 starts the mic here too (stub below).
//   release  -> classify the cast, look up the spell, play its effect, IDLE.
//
// Not yet here, by design of this skeleton: IMU FIFO + wake-on-motion and
// MCU sleep in IDLE (idle-power work), the low-voltage floor (power.h), and
// the keyword spotter. Serial logging works when a host is connected and is
// skipped silently on battery -- there is no `while (!Serial)`.

#include <Adafruit_TinyUSB.h>  // pulls in the USB-CDC Serial object on this core
#include "LSM6DS3.h"
#include "Wire.h"

#include "button.h"
#include "config.h"
#include "effects.h"
#include "gestures.h"
#include "power.h"
#include "spells.h"

enum class WandState : uint8_t { Idle, Casting };

LSM6DS3 imu(I2C_MODE, 0x6A);
DebouncedButton button(PIN_CAST_BUTTON);
GestureEngine engine;
Effects effects;

WandState state = WandState::Idle;
uint32_t castStartMs = 0;
uint32_t lastSampleUs = 0;
uint16_t castNumber = 0;

void fail(const char* why) {
  Serial.print("# FATAL: ");
  Serial.println(why);
  pinMode(LED_RED, OUTPUT);
  for (;;) {  // onboard LED is active-LOW; blink red forever
    digitalWrite(LED_RED, (millis() / 250) % 2 ? LOW : HIGH);
  }
}

void startCast(uint32_t nowMs) {
  state = WandState::Casting;
  castStartMs = nowMs;
  lastSampleUs = micros();
  engine.beginCast();
  digitalWrite(LED_BLUE, LOW);  // debug: onboard blue while held
  // Phase 4: power the PDM mic (PIN_PDM_PWR) and start capture here.
}

void sampleImu() {
  uint32_t now = micros();
  if (now - lastSampleUs < IMU_SAMPLE_PERIOD_US) return;
  lastSampleUs += IMU_SAMPLE_PERIOD_US;

  ImuSample s = {millis(),
                 imu.readFloatAccelX(), imu.readFloatAccelY(), imu.readFloatAccelZ(),
                 imu.readFloatGyroX(),  imu.readFloatGyroY(),  imu.readFloatGyroZ()};
  engine.addSample(s);
}

void logCast(uint32_t heldMs, GestureResult r, const Spell* spell) {
  if (!Serial) return;
  const GestureFeatures& f = engine.features();
  Serial.print("# cast ");
  Serial.print(castNumber);
  Serial.print(": held ");
  Serial.print(heldMs);
  Serial.print(" ms -> ");
  Serial.print(gestureName(r.gesture));
  Serial.print(spell ? " (" : "");
  Serial.print(spell ? spell->name : "");
  Serial.print(spell ? ")" : "");
  Serial.print("  peak=");
  Serial.print(f.peakDps, 0);
  Serial.print(" cum=");
  Serial.print(f.cumDeg, 0);
  Serial.print(" planar=");
  Serial.print(f.planar, 2);
  Serial.print(" phi=");
  Serial.print(f.phiDeg, 1);
  Serial.print(" flips=");
  Serial.print(f.flips);
  Serial.print(" smooth=");
  Serial.print(f.smooth, 2);
  Serial.print(" y1=");
  Serial.println(f.y1, 0);
}

void endCast(uint32_t nowMs) {
  state = WandState::Idle;
  digitalWrite(LED_BLUE, HIGH);
  uint32_t heldMs = nowMs - castStartMs;
  if (heldMs < MIN_CAST_MS) return;  // accidental tap
  castNumber++;

  GestureResult r = engine.endCast();
  const char* heard = nullptr;  // Phase 4: stop the mic, run the keyword spotter
  const Spell* spell = findSpell(r.gesture, heard);
  logCast(heldMs, r, spell);

  if (!spell) return;
  if (spell->effect == Effect::Lumos && !lumosAllowed()) return;
  effects.play(spell->effect, nowMs);
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, HIGH);
  button.begin();
  effects.begin();
  powerBegin();

  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
  delay(100);  // IMU power rail settle; boot only, before any effect runs
  imu.settings.gyroSampleRate = IMU_SAMPLE_RATE_HZ;
  imu.settings.accelSampleRate = IMU_SAMPLE_RATE_HZ;
  if (imu.begin() != 0) fail("IMU init");
}

void loop() {
  uint32_t now = millis();
  ButtonEdge edge = button.update(now);

  if (state == WandState::Idle && edge == ButtonEdge::Pressed) {
    startCast(now);
  } else if (state == WandState::Casting) {
    if (edge == ButtonEdge::Released) {
      endCast(now);
    } else {
      sampleImu();
    }
  }

  effects.update(millis());
}
