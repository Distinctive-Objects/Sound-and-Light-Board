#include <Arduino.h>

#define THRESH_IDLE 10         // anything below this is "no press"
#define DEBOUNCE_MS 60         // debounce time

uint8_t lastButton = 0;
uint32_t lastChange = 0;

int identifyButton(int v) {
  // >>> Update these once you take fresh readings <<<
  if (v > 890) return 4;
  if (v > 820) return 3;
  if (v > 750) return 2;
  if (v > 200) return 1;
  return 0;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("A0 Ladder Test – prints ONLY when buttons are pressed"));
  Serial.println(F("-----------------------------------------------------"));
}

void loop() {
  int v = analogRead(A0);
  uint32_t t = millis();

  // Idle state — don’t print noise
  if (v < THRESH_IDLE) {
    lastButton = 0;
    return;
  }

  // Identify which button this raw value corresponds to
  uint8_t b = identifyButton(v);

  // Debounce
  if (b != lastButton && (t - lastChange) > DEBOUNCE_MS) {
    lastChange = t;
    lastButton = b;

    if (b > 0) {
      Serial.print("Button ");
      Serial.print(b);
      Serial.print(" pressed   |   ADC value = ");
      Serial.println(v);
    }
  }

  delay(10);
}
