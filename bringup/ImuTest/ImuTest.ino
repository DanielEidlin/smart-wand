// Bring-up sketch: stream accel+gyro as CSV over serial at a fixed ODR
// (data rate -- how many samples per second the sensor produces), with
// keypress labelling so a host script can tag which gesture a run of
// samples belongs to.
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
// pin must be driven HIGH before the IMU responds.
//
// Serial protocol:
//   - Every line is one sample: millis,label,ax,ay,az,gx,gy,gz
//     accel in g, gyro in deg/s.
//   - Send any single printable character over serial to change the
//     current label (echoed back as a comment line). Send '0' to clear
//     the label back to idle. Label persists across samples until changed.

#include <Adafruit_TinyUSB.h>  // pulls in the USB-CDC Serial object on this core
#include "LSM6DS3.h"
#include "Wire.h"

const uint16_t SAMPLE_RATE_HZ = 104;
const unsigned long SAMPLE_PERIOD_US = 1000000UL / SAMPLE_RATE_HZ;

LSM6DS3 myIMU(I2C_MODE, 0x6A);

char currentLabel = '0';  // '0' = idle/unlabeled
unsigned long lastSampleUs = 0;

void setup() {
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
  Serial.println("# send any char to set label, '0' to clear; format: millis,label,ax,ay,az,gx,gy,gz");
  Serial.println("millis,label,ax,ay,az,gx,gy,gz");

  lastSampleUs = micros();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c != '\r' && c != '\n') {
      currentLabel = c;
      Serial.print("# label=");
      Serial.println(currentLabel);
    }
  }

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
  Serial.println(myIMU.readFloatGyroZ(), 4);
}
