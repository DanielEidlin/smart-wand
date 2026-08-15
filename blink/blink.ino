#include "Wire.h"

void setup() {
  Serial.begin(9600);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
}

void blinkLed(int ledPin) {
    digitalWrite(ledPin, LOW);
    delay(175);
    digitalWrite(ledPin, HIGH);
}

void loop() {
  blinkLed(LED_RED);
  blinkLed(LED_GREEN);
  blinkLed(LED_BLUE);
}