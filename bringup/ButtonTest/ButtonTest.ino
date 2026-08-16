// Bring-up sketch: debounced press/release over serial for the cast button.
// Wiring: button between BUTTON_PIN and GND, INPUT_PULLUP (pressed = LOW).
// Provisional pin per CLAUDE.md (2026-08-15) -- not yet checked against the
// castellated-pad layout for solder accessibility.

#include <Adafruit_TinyUSB.h>  // pulls in the USB-CDC Serial object on this core

const int BUTTON_PIN = D1;
const unsigned long DEBOUNCE_MS = 30;

bool stableState = HIGH;      // debounced state: HIGH = released, LOW = pressed
bool lastRawState = HIGH;
unsigned long lastEdgeMs = 0;
unsigned long pressStartMs = 0;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println("ButtonTest ready");
}

void loop() {
  bool raw = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  if (raw != lastRawState) {
    lastRawState = raw;
    lastEdgeMs = now;
  }

  if (raw != stableState && (now - lastEdgeMs) >= DEBOUNCE_MS) {
    stableState = raw;
    if (stableState == LOW) {
      pressStartMs = now;
      Serial.print(now);
      Serial.println(" PRESS");
    } else {
      Serial.print(now);
      Serial.print(" RELEASE  held=");
      Serial.print(now - pressStartMs);
      Serial.println("ms");
    }
  }
}
