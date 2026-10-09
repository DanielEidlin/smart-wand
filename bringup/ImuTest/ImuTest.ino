// Bring-up sketch: stream accel+gyro as CSV over serial at a fixed ODR
// (data rate -- how many samples per second the sensor produces), with
// keypress labelling so a host script can tag which gesture a run of
// samples belongs to, and the cast button marking where each cast starts
// and ends.
//
// Also the vehicle for establishing the board-axis -> wand-axis mapping:
// CLAUDE.md's gesture table is written in wand-relative terms (e.g. flick
// up = positive rotation about the wrist axis), but the IMU reports in
// board-relative X/Y/Z. Do known single-axis motions (rotate about each
// board axis in turn, thrust along each) with the board held the way it
// will sit in the bore, label each with a single keystroke, and read off
// which board axis/sign corresponds to which wand motion from the CSV.
//
// Wiring/pins: IMU is onboard, on Wire1 (handled automatically by the
// library's Wire1 #define on this target -- see CLAUDE.md). Power-enable
// pin must be driven HIGH before the IMU responds. Cast button between
// BUTTON_PIN and GND, INPUT_PULLUP (pressed = LOW), debounced exactly as in
// ButtonTest. Works with no button wired: the pin just reads released and
// every sample carries cast=0.
//
// Serial protocol:
//   - Every line is one sample: millis,label,ax,ay,az,gx,gy,gz,cast
//     accel in g, gyro in deg/s. cast is 0 while the button is released and
//     the press number (1, 2, 3...) while it is held, so one cast is every
//     row sharing a non-zero cast value -- no segmentation needed on the
//     host. The 2026-08-16 traces predate this column.
//   - "# PRESS <n>" / "# RELEASE <n> held=<ms>" comment lines bracket each
//     cast, for a human watching the stream.
//   - Send any single printable character over serial to change the
//     current label (echoed back as a comment line). Send '0' to clear
//     the label back to idle. Label persists across samples until changed.
//   - Send '#' to reset the cast counter to 0 (a host script does this at
//     the start of each file so cast numbers count from 1 per file).

#include <Adafruit_TinyUSB.h>  // pulls in the USB-CDC Serial object on this core
#include "LSM6DS3.h"
#include "Wire.h"

const uint16_t SAMPLE_RATE_HZ = 104;
const unsigned long SAMPLE_PERIOD_US = 1000000UL / SAMPLE_RATE_HZ;

const int BUTTON_PIN = D1;              // provisional, see CLAUDE.md Board gotchas
const unsigned long DEBOUNCE_MS = 30;   // same as ButtonTest, bench-verified

LSM6DS3 myIMU(I2C_MODE, 0x6A);

char currentLabel = '0';  // '0' = idle/unlabeled
unsigned long lastSampleUs = 0;

bool buttonStable = HIGH;    // debounced: HIGH = released, LOW = pressed
bool buttonLastRaw = HIGH;
unsigned long buttonLastEdgeMs = 0;
unsigned long pressStartMs = 0;
uint16_t castCount = 0;      // presses so far; the current cast while held

void pollButton() {
  bool raw = digitalRead(BUTTON_PIN);
  unsigned long now = millis();
  if (raw != buttonLastRaw) {
    buttonLastRaw = raw;
    buttonLastEdgeMs = now;
  }
  if (raw == buttonStable || now - buttonLastEdgeMs < DEBOUNCE_MS) {
    return;
  }
  buttonStable = raw;
  if (buttonStable == LOW) {
    castCount++;
    pressStartMs = now;
    digitalWrite(LED_BLUE, LOW);   // onboard LED is active-LOW
    Serial.print("# PRESS ");
    Serial.println(castCount);
  } else {
    digitalWrite(LED_BLUE, HIGH);
    Serial.print("# RELEASE ");
    Serial.print(castCount);
    Serial.print(" held=");
    Serial.println(now - pressStartMs);
  }
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, HIGH);

  Serial.begin(115200);
  while (!Serial) {}

  pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
  digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
  delay(100);  // let the IMU power rail settle before talking to it

  myIMU.settings.gyroSampleRate = SAMPLE_RATE_HZ <= 104 ? 104 : SAMPLE_RATE_HZ;
  myIMU.settings.accelSampleRate = SAMPLE_RATE_HZ <= 104 ? 104 : SAMPLE_RATE_HZ;
  // Ranges left at library defaults (accel 16g, gyro 2000dps) until bench
  // data shows they're too coarse for the gesture set -- see CLAUDE.md.

  if (myIMU.begin() != 0) {
    Serial.println("# IMU init failed");
    while (1) {}
  }

  Serial.println("# ImuTest ready");
  Serial.print("# accelRange=");
  Serial.print(myIMU.settings.accelRange);
  Serial.print("g gyroRange=");
  Serial.print(myIMU.settings.gyroRange);
  Serial.print("dps rate=");
  Serial.print(SAMPLE_RATE_HZ);
  Serial.println("Hz");
  Serial.println("# send any char to set label, '0' to clear, '#' to reset cast count");
  Serial.println("# hold the button on D1 to cast; format: millis,label,ax,ay,az,gx,gy,gz,cast");
  Serial.println("millis,label,ax,ay,az,gx,gy,gz,cast");

  lastSampleUs = micros();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '#') {
      castCount = 0;
      Serial.println("# cast count reset");
    } else if (c != '\r' && c != '\n') {
      currentLabel = c;
      Serial.print("# label=");
      Serial.println(currentLabel);
    }
  }

  pollButton();

  unsigned long now = micros();
  if (now - lastSampleUs < SAMPLE_PERIOD_US) {
    return;
  }
  lastSampleUs += SAMPLE_PERIOD_US;

  Serial.print(millis());
  Serial.print(',');
  Serial.print(currentLabel);
  Serial.print(',');
  Serial.print(myIMU.readFloatAccelX(), 4);
  Serial.print(',');
  Serial.print(myIMU.readFloatAccelY(), 4);
  Serial.print(',');
  Serial.print(myIMU.readFloatAccelZ(), 4);
  Serial.print(',');
  Serial.print(myIMU.readFloatGyroX(), 4);
  Serial.print(',');
  Serial.print(myIMU.readFloatGyroY(), 4);
  Serial.print(',');
  Serial.print(myIMU.readFloatGyroZ(), 4);
  Serial.print(',');
  Serial.println(buttonStable == LOW ? castCount : 0);
}
