// Bring-up sketch: read the battery voltage over serial.
//
// Two board quirks this works around (see CLAUDE.md "Board gotchas"):
//   - VBAT_ENABLE is driven HIGH by the core's startup code, which
//     *disables* battery-voltage sensing on this board. Must be driven
//     LOW here before PIN_VBAT gives a real reading.
//   - The battery sits behind a 1 M ohm / 510 k ohm voltage divider (two
//     resistors in series that scale the battery's ~3.0-4.2V range down
//     into the ADC's 0-3V measurable range), so the raw ADC count has to
//     be scaled back up in software to recover the real voltage.
//
// CALIBRATION_SCALE below is the nominal divider math, UNCALIBRATED
// against a multimeter (none on hand as of 2026-08-22). Treat the printed
// voltage as a rough estimate only -- fine for confirming the circuit
// works and that the number moves the right way, not for tuning the
// firmware's low-voltage cutoff yet. Revisit once a multimeter is available.

#include <Adafruit_TinyUSB.h>  // pulls in the USB-CDC Serial object on this core

const unsigned long PRINT_PERIOD_MS = 1000;

// vbat = adcCount * (3.0V ref / 4096 counts) * ((1000k+510k) / 510k divider ratio)
const float CALIBRATION_SCALE = (3.0 / 4096.0) * (1510.0 / 510.0);

unsigned long lastPrintMs = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);  // enable battery sensing (inverted vs. the pin name)

  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);

  Serial.println("# BatteryTest ready");
  Serial.println("# voltage is UNCALIBRATED -- nominal divider math only, see header comment");
  Serial.println("millis,adcRaw,voltage");
}

void loop() {
  unsigned long now = millis();
  if (now - lastPrintMs < PRINT_PERIOD_MS) {
    return;
  }
  lastPrintMs = now;

  int adcRaw = analogRead(PIN_VBAT);
  float voltage = adcRaw * CALIBRATION_SCALE;

  Serial.print(now);
  Serial.print(',');
  Serial.print(adcRaw);
  Serial.print(',');
  Serial.println(voltage, 3);
}
