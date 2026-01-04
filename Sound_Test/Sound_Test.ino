#include <Arduino.h>
#include <SoftwareSerial.h>
#include "DFRobotDFPlayerMini.h"

#define PIN_DF_RX  D6  // ESP RX  (connect to DF TX)
#define PIN_DF_TX  D7  // ESP TX  (connect to DF RX via divider)

SoftwareSerial dfSerial(PIN_DF_RX, PIN_DF_TX); // RX, TX
DFRobotDFPlayerMini dfp;

void setup() {
  // No Serial, no WiFi
  dfSerial.begin(9600);
  dfp.begin(dfSerial);         // if SD absent, this will just do nothing harmlessly
  dfp.setTimeOut(500);
  dfp.volume(20);              // 0..30
  dfp.EQ(DFPLAYER_EQ_NORMAL);
}

void loop() {
  dfp.play(1);                 // play track 001.mp3 (or 0001.mp3) in / or /01/
  delay(5000);                 // let it play for 5s
  dfp.stop();
  delay(1000);                 // pause 1s and repeat
  delay(0);                    // feed WDT
}
