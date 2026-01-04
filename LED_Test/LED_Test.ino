#include <Arduino.h>

#define LED1  D0   // GPIO16 (no PWM/interrupts, fine for on/off)
#define LED2  D2   // GPIO4
#define LED3  D5   // GPIO14
#define LED4  1    // GPIO1 (TX0) used as GPIO output

inline void setLed(uint8_t pin, bool on) {
  if (pin == D4) {                 // (not used here) built-in would be active LOW
    digitalWrite(pin, on ? LOW : HIGH);
  } else {
    digitalWrite(pin, on ? HIGH : LOW);
  }
}

void setup() {
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LED4, OUTPUT);
  setLed(LED1, false);
  setLed(LED2, false);
  setLed(LED3, false);
  setLed(LED4, false);
}

void loop() {
  const uint8_t seq[4] = { LED1, LED2, LED3, LED4 };
  for (uint8_t i=0; i<4; i++) {
    // one-hot
    for (uint8_t j=0; j<4; j++) setLed(seq[j], j==i);
    delay(150);
    delay(0);
  }
  // all on, then all off
  for (uint8_t j=0; j<4; j++) setLed(seq[j], true);
  delay(200);
  for (uint8_t j=0; j<4; j++) setLed(seq[j], false);
  delay(200);
  delay(0);
}
