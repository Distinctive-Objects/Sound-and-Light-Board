/******************************************************
 * Block-Bot Controller – Option B (Existing Wiring)
 *
 * FEATURES:
 *  - 4-button A0 ladder (debounced)
 *  - 4 LEDs (LED4 on D4, active-LOW)
 *  - 5x5 NeoPixel faces on D1
 *  - DFPlayer via SoftwareSerial on D6/D7
 *  - EEPROM config for per-button track/LED/face
 *  - WiFi AP config mode (hold any button at boot)
 *
 * CRITICAL BEHAVIOUR:
 *  - CONFIG MODE (WiFi, NO DFPlayer):
 *      Any button held at boot (A0 > ~300)
 *      AP SSID: DO-BlockBot
 *      PASS:    DistinctiveObjects
 *      Visit:   http://192.168.4.1
 *      After “Save”, EEPROM is updated and the ESP
 *      auto-restarts. Next boot goes to RUN MODE.
 *
 *  - RUN MODE (DFPlayer, NO WiFi):
 *      No button held at boot (A0 low)
 *      WiFi forced OFF
 *      DFPlayer initialised
 *      Button press -> LED + Face + DFPlayer track
 ******************************************************/

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include "DFRobotDFPlayerMini.h"
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ----------------- DEBUG TOGGLE -----------------
//#define USE_SERIAL

#ifdef USE_SERIAL
  #define DBG_BEGIN()       do{ Serial.begin(115200); delay(300); }while(0)
  #define DBG_PRINTLN(x)    Serial.println(x)
  #define DBG_PRINT(x)      Serial.print(x)
#else
  #define DBG_BEGIN()       do{}while(0)
  #define DBG_PRINTLN(x)    do{}while(0)
  #define DBG_PRINT(x)      do{}while(0)
#endif

// ----------------- PIN & HW SETUP -----------------

// NeoPixel 5x5 matrix on D1
#define MATRIX_W   5
#define MATRIX_H   5
#define NEOPIX_PIN D1
#define SERPENTINE 1
#define NP_BRIGHT  64

// 4 LEDs
#define LED1  D0
#define LED2  D2
#define LED3  D5
#define LED4  D4   // GPIO2, LED to GND (active-LOW)

// DFPlayer via SoftwareSerial
#define PIN_DF_RX D6   // ESP RX  (DF TX)
#define PIN_DF_TX D7   // ESP TX  (DF RX via divider)
SoftwareSerial dfSerial(PIN_DF_RX, PIN_DF_TX);
DFRobotDFPlayerMini dfp;
bool dfOk = false;

// A0 ladder
#define DEBOUNCE_MS   50
#define THRESH_IDLE   50

// ----------------- BUTTON CLASSIFICATION -----------------

static inline uint8_t classifyButton(int v) {
  if (v < THRESH_IDLE) return 0;
  if (v >= 968)       return 4;
  else if (v >= 842)  return 3;
  else if (v >= 656)  return 2;
  else if (v >= 300)  return 1;
  else                return 0;
}

const uint8_t TRACK_FOR_BTN[4] = {1, 2, 3, 4};
const uint8_t LED_FOR_BTN[4]   = {1, 2, 3, 4};

// ----------------- NEOPIXELS / FACES -----------------

#define NUM_LEDS (MATRIX_W*MATRIX_H)
Adafruit_NeoPixel strip(NUM_LEDS, NEOPIX_PIN, NEO_GRB + NEO_KHZ800);

static inline uint32_t RGB(uint8_t r,uint8_t g,uint8_t b){ return strip.Color(r,g,b); }

const uint32_t C_BG     = RGB(0,0,0);
const uint32_t C_NEU    = RGB(200,200,255);
const uint32_t C_SMILE  = RGB(255,220,0);
const uint32_t C_FROWN  = RGB(255,0,0);
const uint32_t C_SURP   = RGB(0,180,255);

static uint8_t B5(const char* s){
  return ((s[0]=='1')<<4)
       | ((s[1]=='1')<<3)
       | ((s[2]=='1')<<2)
       | ((s[3]=='1')<<1)
       | ((s[4]=='1')<<0);
}

struct Face {
  uint8_t  row[5];
  uint32_t fg;
};

#define FACEID_NEUTRAL 0
#define FACEID_SMILE   1
#define FACEID_FROWN   2
#define FACEID_SURP    3

const Face faces[] = {
  // Neutral
  { { B5("00000"),
      B5("01010"),
      B5("00000"),
      B5("01110"),
      B5("00000") }, C_NEU },

  // Smile
  { { B5("00000"),
      B5("01010"),
      B5("00000"),
      B5("01010"),
      B5("00100") }, C_SMILE },

  // Frown
  { { B5("00000"),
      B5("01010"),
      B5("00100"),
      B5("01010"),
      B5("00000") }, C_FROWN },

  // Surprise
  { { B5("00000"),
      B5("01010"),
      B5("00000"),
      B5("00100"),
      B5("00000") }, C_SURP }
};

static inline uint16_t idxXY(uint8_t x,uint8_t y){
  if (SERPENTINE) {
    if (y & 1) return y*MATRIX_W + (MATRIX_W-1 - x);
    else       return y*MATRIX_W + x;
  } else {
    return y*MATRIX_W + x;
  }
}

void drawFaceById(uint8_t faceId){
  if (faceId > 3) faceId = FACEID_NEUTRAL;
  const Face &f = faces[faceId];

  for (uint8_t y=0; y<5; y++) {
    for (uint8_t x=0; x<5; x++) {
      bool on = (f.row[y] >> (4-x)) & 1;
      strip.setPixelColor(idxXY(x,y), on ? f.fg : C_BG);
    }
  }
  strip.show();
}

// ----------------- EEPROM CONFIG -----------------

struct ButtonConfig {
  uint8_t track;  // 1–30
  uint8_t led;    // 1–4
  uint8_t face;   // 0–3
};

struct BotConfig {
  uint32_t magic;
  ButtonConfig btn[4];
};

BotConfig gConfig;

// Same magic as last persistent test, so your settings are kept
#define CONFIG_MAGIC 0x424F5450UL
#define EEPROM_SIZE  64

void setDefaultConfig(){
  gConfig.magic = CONFIG_MAGIC;

  for (uint8_t i=0; i<4; i++){
    gConfig.btn[i].track = TRACK_FOR_BTN[i];
    gConfig.btn[i].led   = LED_FOR_BTN[i];

    switch(i){
      case 0: gConfig.btn[i].face = FACEID_SMILE;   break;
      case 1: gConfig.btn[i].face = FACEID_SURP;    break;
      case 2: gConfig.btn[i].face = FACEID_NEUTRAL; break;
      case 3: gConfig.btn[i].face = FACEID_FROWN;   break;
    }
  }
}

void saveConfig(){
  EEPROM.put(0, gConfig);
  EEPROM.commit();
}

bool loadConfig(){
  EEPROM.get(0, gConfig);
  if (gConfig.magic != CONFIG_MAGIC){
    setDefaultConfig();
    saveConfig();
    return false;
  }

  bool ok = true;
  for (uint8_t i=0; i<4; i++){
    if (gConfig.btn[i].track < 1 || gConfig.btn[i].track > 30) ok = false;
    if (gConfig.btn[i].led   < 1 || gConfig.btn[i].led   > 4 ) ok = false;
    if (gConfig.btn[i].face  > 3)                              ok = false;
  }
  if (!ok){
    setDefaultConfig();
    saveConfig();
  }
  return ok;
}

// ----------------- DEBOUNCE -----------------

uint8_t  lastReading    = 0;
uint8_t  stableReading  = 0;
uint32_t lastChangeMs   = 0;

// ----------------- LED HELPERS -----------------

inline void setLed(uint8_t pin, bool on){
  if (pin == LED4) {
    // D4 often active-LOW. Treat LED4 as active-LOW.
    digitalWrite(pin, on ? LOW : HIGH);
  } else {
    digitalWrite(pin, on ? HIGH : LOW);
  }
}

void showOnlyLedIndex(uint8_t i){
  setLed(LED1, i==1);
  setLed(LED2, i==2);
  setLed(LED3, i==3);
  setLed(LED4, i==4);
}

// ----------------- ACTION ON BUTTON PRESS (RUN MODE) -----------------

void triggerForButton(uint8_t b){
  if (b < 1 || b > 4) return;
  uint8_t idx = b - 1;

  uint8_t track = gConfig.btn[idx].track;
  uint8_t ledIx = gConfig.btn[idx].led;
  uint8_t face  = gConfig.btn[idx].face;

  if (track < 1 || track > 30) track = TRACK_FOR_BTN[idx];
  if (ledIx < 1 || ledIx > 4)  ledIx = LED_FOR_BTN[idx];

  if (face > 3) {
    switch(b){
      case 1: face = FACEID_SMILE;   break;
      case 2: face = FACEID_SURP;    break;
      case 3: face = FACEID_NEUTRAL; break;
      case 4: face = FACEID_FROWN;   break;
      default: face = FACEID_NEUTRAL;break;
    }
  }

  showOnlyLedIndex(ledIx);
  drawFaceById(face);

  if (dfOk) {
    dfp.play(track);
  }
}

// ----------------- WIFI CONFIG (CONFIG MODE ONLY) -----------------

ESP8266WebServer server(80);
const char* AP_SSID = "DO-BlockBot";
const char* AP_PASS = "DistinctiveObjects";

bool isConfigMode = false;
bool shouldReboot = false;
uint32_t rebootAt = 0;

void handleRoot() {
  String html;
  html.reserve(2500);

  const char* faceNames[4] = {"Neutral","Smile","Frown","Surprise"};

  html += F(
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>BlockBot Config</title>"
    "<style>"
    "body{font-family:system-ui,Arial,sans-serif;background:#111;color:#eee;padding:10px;}"
    "h1{font-size:1.4rem;text-align:center;margin-bottom:0.5em;}"
    "h2{font-size:1.1rem;margin:0 0 6px 0;}"
    ".card{background:#222;border-radius:10px;padding:10px;margin:10px 0;}"
    "label{display:block;margin:6px 0 2px;font-size:0.9rem;}"
    "input,select{width:100%;padding:6px 8px;font-size:1rem;border-radius:6px;border:none;margin-bottom:4px;}"
    "button{width:100%;padding:10px;font-size:1rem;border:none;border-radius:8px;background:#00b37a;color:#fff;margin-top:10px;}"
    ".small{font-size:0.8rem;color:#aaa;text-align:center;margin-top:8px;}"
    "</style></head><body>"
    "<h1>BlockBot Configuration</h1>"
    "<form action='/save' method='GET'>"
  );

  for (uint8_t i=0; i<4; i++) {
    uint8_t btnNum = i+1;
    html += F("<div class='card'><h2>Button ");
    html += btnNum;
    html += F("</h2>");

    html += F("<label>Track number (1-30)</label>"
              "<input type='number' min='1' max='30' name='b");
    html += btnNum;
    html += F("_track' value='");
    html += gConfig.btn[i].track;
    html += F("'>");

    html += F("<label>LED (1-4)</label><select name='b");
    html += btnNum;
    html += F("_led'>");
    for (uint8_t l=1; l<=4; l++) {
      html += F("<option value='");
      html += l;
      html += "'";
      if (gConfig.btn[i].led == l) html += F(" selected");
      html += F(">LED ");
      html += l;
      html += F("</option>");
    }
    html += F("</select>");

    html += F("<label>Face</label><select name='b");
    html += btnNum;
    html += F("_face'>");

    for (uint8_t f=0; f<4; f++) {
      html += F("<option value='");
      html += f;
      html += "'";
      if (gConfig.btn[i].face == f) html += F(" selected");
      html += F(">");
      html += faceNames[f];
      html += F("</option>");
    }
    html += F("</select>");

    html += F("</div>");
  }

  html += F(
    "<button type='submit'>Save & Reboot</button>"
    "<div class='small'>After saving, the controller will reboot into run mode."
    " To enter config mode again, power on while holding any button.</div>"
    "</form></body></html>"
  );

  server.send(200, "text/html", html);
}

void handleSave() {
  for (uint8_t i=0; i<4; i++) {
    uint8_t btnNum = i + 1;

    String key = "b" + String(btnNum) + "_track";
    if (server.hasArg(key)) {
      int t = server.arg(key).toInt();
      if (t < 1) t = 1;
      if (t > 30) t = 30;
      gConfig.btn[i].track = (uint8_t)t;
    }

    key = "b" + String(btnNum) + "_led";
    if (server.hasArg(key)) {
      int l = server.arg(key).toInt();
      if (l < 1) l = 1;
      if (l > 4) l = 4;
      gConfig.btn[i].led = (uint8_t)l;
    }

    key = "b" + String(btnNum) + "_face";
    if (server.hasArg(key)) {
      int f = server.arg(key).toInt();
      if (f < 0) f = 0;
      if (f > 3) f = 3;
      gConfig.btn[i].face = (uint8_t)f;
    }
  }

  saveConfig();

  String html;
  html += F(
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Saved</title>"
    "<style>body{background:#111;color:#eee;font-family:system-ui,Arial,sans-serif;"
    "display:flex;align-items:center;justify-content:center;height:100vh;text-align:center;}"
    "a{color:#00b37a;text-decoration:none;font-weight:bold;}</style>"
    "</head><body>"
    "<div><h2>Settings Saved</h2>"
    "<p>The controller will reboot into run mode in a couple of seconds.</p>"
    "<p>If you see this for long, power-cycle the board.</p>"
    "</div></body></html>"
  );

  server.send(200, "text/html", html);

  // schedule a reboot shortly after sending the response
  shouldReboot = true;
  rebootAt = millis() + 2000;
}

// ----------------- SETUP -----------------

void setup(){
  DBG_BEGIN();

  // Never persist WiFi mode across boots
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);

  pinMode(LED1,OUTPUT);
  pinMode(LED2,OUTPUT);
  pinMode(LED3,OUTPUT);
  pinMode(LED4,OUTPUT);
  showOnlyLedIndex(1);

  strip.begin();
  strip.setBrightness(NP_BRIGHT);
  strip.show();
  drawFaceById(FACEID_NEUTRAL);

  // Decide mode by sampling A0 at boot
  delay(100);
  uint32_t sum = 0;
  const uint8_t N = 8;
  for (uint8_t i=0; i<N; i++){
    sum += analogRead(A0);
    delay(5);
  }
  uint16_t avg = sum / N;

  EEPROM.begin(EEPROM_SIZE);
  loadConfig();

  if (avg > 300) {
    isConfigMode = true;
  } else {
    isConfigMode = false;
  }

  if (isConfigMode) {
    // CONFIG MODE: WiFi AP + Web, NO DFPlayer
    WiFi.mode(WIFI_AP);
    IPAddress local_IP(192,168,4,1);
    IPAddress gateway(192,168,4,1);
    IPAddress subnet(255,255,255,0);
    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP(AP_SSID, AP_PASS);

    showOnlyLedIndex(1);

    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.begin();

  } else {
    // RUN MODE: WiFi stays OFF, DFPlayer active
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();  // be extra sure radio is idle
    delay(10);

    dfSerial.begin(9600);
    delay(1000);  // give DFPlayer power-up time

    // Try a few times to init DFPlayer
    for (int i=0; i<3 && !dfOk; i++) {
      if (dfp.begin(dfSerial)) {
        dfOk = true;
      } else {
        delay(300);
      }
    }
    if (dfOk) {
      dfp.volume(25);
    }

    lastReading   = 0;
    stableReading = 0;
    lastChangeMs  = millis();
  }
}

// ----------------- LOOP -----------------

void loop(){
  if (isConfigMode) {
    server.handleClient();

    // handle scheduled reboot after save
    if (shouldReboot && (int32_t)(millis() - rebootAt) >= 0) {
      ESP.restart();
    }

    delay(0);
    return;
  }

  // RUN MODE: buttons -> LEDs + faces + sound (if dfOk)
  uint32_t now = millis();
  int v = analogRead(A0);
  uint8_t reading = classifyButton(v);

#ifdef USE_SERIAL
  static uint32_t lastPrint = 0;
  if (now - lastPrint > 300) {
    DBG_PRINT("A0=");
    DBG_PRINT(v);
    DBG_PRINT("  rawBtn=");
    DBG_PRINT(reading);
    DBG_PRINT("  stable=");
    DBG_PRINTLN(stableReading);
    lastPrint = now;
  }
#endif

  if (reading != lastReading) {
    lastReading  = reading;
    lastChangeMs = now;
  }

  if ((now - lastChangeMs) >= DEBOUNCE_MS && reading != stableReading) {
    uint8_t prevStable = stableReading;
    stableReading = reading;

    if (prevStable == 0 && stableReading > 0){
      triggerForButton(stableReading);
    }
  }

  delay(0);
}
