/**************************************************************
 * Integrated: A0 Ladder (4 buttons) + DFPlayer + 4 LEDs + NeoPixel
 * - Each button press plays track (1..4), advances 1-of-4 LED,
 *   and colors the NeoPixel ring (B1=Red, B2=Green, B3=Blue, B4=Yellow)
 *
 * Default: NO Serial I/O (set USE_SERIAL=1 to enable debug).
 **************************************************************/

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include "DFRobotDFPlayerMini.h"

// ---------- CONFIG ----------
#define USE_SERIAL    0            // set to 1 if you want Serial debug
#define SERIAL_BAUD   115200

// Buttons on A0 (resistor ladder)
#define BTN_DEBOUNCE_MS  60        // debouncing interval

// LEDs (4 total)
#define LED1   D0      // GPIO16
#define LED2   D2      // GPIO4
#define LED3   D5      // GPIO14
#define LED4   1       // GPIO1 (TX0) used as GPIO output

// NeoPixel ring
#define NEOPIX_PIN   D1            // GPIO5
#define NEOPIX_COUNT 12            // set to your actual ring length
#define NEOPIX_BRIGHTNESS 80       // 0..255

// DFPlayer (SoftwareSerial)
#define PIN_DF_RX  D6              // ESP RX (connect DF TX)
#define PIN_DF_TX  D7              // ESP TX (to DF RX via divider)
SoftwareSerial dfSerial(PIN_DF_RX, PIN_DF_TX);
DFRobotDFPlayerMini dfp;

// ---------- STATE ----------
Adafruit_NeoPixel strip(NEOPIX_COUNT, NEOPIX_PIN, NEO_GRB + NEO_KHZ800);

// Your measured A0 values were ~738, 758, 841, 915
// We'll give each a comfortable range:
inline int identifyButton(int v) {
  if (v > 890 && v <= 1023) return 4; // ~915
  if (v > 820 && v <= 890)  return 3; // ~841
  if (v > 750 && v <= 820)  return 2; // ~758
  if (v > 720 && v <= 750)  return 1; // ~738
  return 0;                           // none
}

static uint8_t currentLedIndex = 0;  // which LED is ON (0..3)
static uint8_t lastButton = 0;
static uint32_t lastChangeMs = 0;

// ---------- UTILS ----------
inline void setLedPin(uint8_t pin, bool on) {
  if (pin == D4) {
    // not used here, but reminder: D4 (built-in) is active-LOW
    digitalWrite(pin, on ? LOW : HIGH);
  } else if (pin == LED1) {
    // LED1 on GPIO16 (normal HIGH=ON as wired)
    digitalWrite(pin, on ? HIGH : LOW);
  } else {
    digitalWrite(pin, on ? HIGH : LOW);
  }
}

void showOneHotLED(uint8_t idx) {
  // Turn exactly one LED on based on idx (0..3)
  setLedPin(LED1, idx == 0);
  setLedPin(LED2, idx == 1);
  setLedPin(LED3, idx == 2);
  setLedPin(LED4, idx == 3);
}

void fillRing(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

void colorForButton(uint8_t b) {
  switch (b) {
    case 1: fillRing(255, 0, 0);   break; // Red
    case 2: fillRing(0, 255, 0);   break; // Green
    case 3: fillRing(0, 0, 255);   break; // Blue
    case 4: fillRing(255, 200, 0); break; // Yellow/Amber
    default: fillRing(0, 0, 0);    break; // Off
  }
}

inline void playTrack(uint8_t n) {
  if (n < 1) n = 1;
  if (n > 3000) n = 3000;
  dfp.play(n);
}

inline void SAFE_LOG(const __FlashStringHelper* s) {
#if USE_SERIAL
  if (Serial && Serial.availableForWrite() > 16) Serial.println(s);
#endif
}

inline void SAFE_LOG_VAL(const char* label, int v) {
#if USE_SERIAL
  if (Serial && Serial.availableForWrite() > 16) {
    Serial.print(label); Serial.println(v);
  }
#endif
}

// ---------- SETUP ----------
void setup() {
#if USE_SERIAL
  Serial.begin(SERIAL_BAUD);
  delay(300);
  Serial.println();
  Serial.println(F("Integrated: Buttons(A0)+DFPlayer+LEDs+NeoPixel"));
#endif

  // LEDs
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LED4, OUTPUT);
  showOneHotLED(currentLedIndex);   // light first LED

  // NeoPixel
  strip.begin();
  strip.setBrightness(NEOPIX_BRIGHTNESS);
  strip.show(); // all off
  colorForButton(0);

  // DFPlayer
  dfSerial.begin(9600);
  if (dfp.begin(dfSerial)) {
    dfp.setTimeOut(500);
    dfp.volume(20);               // 0..30
    dfp.EQ(DFPLAYER_EQ_NORMAL);
    SAFE_LOG(F("DFPlayer ready"));
  } else {
    SAFE_LOG(F("DFPlayer init failed (check wiring/SD)"));
  }
}

// ---------- LOOP ----------
void loop() {
  // Read and debounce the analog ladder
  int v = analogRead(A0);
  int b = identifyButton(v);
#if USE_SERIAL
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 200) {  // throttle prints
    SAFE_LOG_VAL("A0=", v);
    lastPrint = millis();
  }
#endif

  uint32_t t = millis();
  if (b != lastButton && (t - lastChangeMs) > BTN_DEBOUNCE_MS) {
    lastChangeMs = t;
    lastButton = b;

    if (b != 0) {
      // 1) Play matching track
      playTrack(b);

      // 2) Advance the one-hot LED
      currentLedIndex = (currentLedIndex + 1) & 0x03; // wrap 0..3
      showOneHotLED(currentLedIndex);

      // 3) Set ring color for this button
      colorForButton(b);

#if USE_SERIAL
      SAFE_LOG_VAL("Button=", b);
      SAFE_LOG_VAL("LED idx=", currentLedIndex);
#endif
    }
  }

  // keep WDT happy
  delay(0);
}
