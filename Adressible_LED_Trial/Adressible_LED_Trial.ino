#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define PIN_NEOPIX  D1
#define NUM_PIXELS  25   // set to your strip length

Adafruit_NeoPixel strip(NUM_PIXELS, PIN_NEOPIX, NEO_GRB + NEO_KHZ800);
uint16_t hue = 0;

uint32_t colorWheel(uint16_t h) {
  uint8_t reg = (h >> 8) % 6, rem = h & 0xFF, q = 255 - rem, t = rem;
  uint8_t r=0,g=0,b=0;
  switch (reg) {
    case 0: r=255; g=t;   b=0;   break; // R->Y
    case 1: r=q;   g=255; b=0;   break; // Y->G
    case 2: r=0;   g=255; b=t;   break; // G->C
    case 3: r=0;   g=q;   b=255; break; // C->B
    case 4: r=t;   g=0;   b=255; break; // B->M
    case 5: r=255; g=0;   b=q;   break; // M->R
  }
  return strip.Color(r,g,b);
}

void setup() {
  strip.begin();
  strip.show(); // all off
}

void loop() {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    uint16_t h = hue + (65535UL * i) / strip.numPixels();
    strip.setPixelColor(i, colorWheel(h));
  }
  strip.show();
  hue += 25;          // speed of the rainbow
  delay(20);
  delay(0);
}
