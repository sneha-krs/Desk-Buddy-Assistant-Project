// ==================================================
// DESK BUDDY  -  COMBINED PROGRAM
//   1) DeskBuddy ESP32 (eyes, clock, world clock)
//   2) LDR light sensor -> relay (auto light) + auto OLED brightness
//   3) Stopwatch with 10-minute triple-beep alert
//   (weather / forecast removed)
//
// PAGES (single tap goes to the next one)
//   eyes  ->  clock  ->  STOPWATCH  ->  eyes ...
//   (pages only change when you tap - no automatic switching)
//
// STOPWATCH BUTTON (ONE button between GPIO17 / D17 and GND)
//   Short press          : START / STOP
//   Hold for 1 second    : RESET to 00:00:00
//   Works on any page, the stopwatch keeps counting in the background
//   and ticks once every second (on every page) while it is running.
//
// TOUCH CONTROLS
//   Single tap  : next page
//   Double tap  : toggle display brightness (auto / dim)
//   Long press  : change mood (on eyes) / world clock (on clock)
//   Hold touch 3 s at power-up : WiFi config portal
// ==================================================

// watch complete making video from here , please subscribe and support us
// you can buy components and this kit from www.esclabs.in 

// ==================================================
// EDISON SCIENCE CORNER  - ESCLABS
// ==================================================

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "time.h"
#include <math.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

// ==================================================
// 1. ASSETS & CONFIG
// ==================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SDA_PIN 21
#define SCL_PIN 22
#define TOUCH_PIN 4
#define BUZZER_PIN 18
#define LDR_PIN 34
#define RELAY_PIN 16          // active-LOW relay module (from ldrfinalcode.ino)
#define PAGE_STOPWATCH 2
#define BTN_SW_PIN 17         // stopwatch button (to GND): tap = START/STOP, hold = RESET

// LDR wiring: 1 = ADC value goes UP when it gets darker (matches your relay sketch)
//             0 = ADC value goes DOWN when it gets darker
#define LDR_HIGH_MEANS_DARK 1

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


// --- EMOTION PARTICLES (16x16) ---
const unsigned char bmp_heart[] PROGMEM = { 0x00, 0x00, 0x0c, 0x60, 0x1e, 0xf0, 0x3f, 0xf8, 0x7f, 0xfc, 0x7f, 0xfc, 0x7f, 0xfc, 0x3f, 0xf8, 0x1f, 0xf0, 0x0f, 0xe0, 0x07, 0xc0, 0x03, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const unsigned char bmp_zzz[] PROGMEM = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x0c, 0x00, 0x18, 0x00, 0x30, 0x00, 0x7e, 0x00, 0x00, 0x3c, 0x00, 0x0c, 0x00, 0x18, 0x00, 0x30, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00 };
const unsigned char bmp_anger[] PROGMEM = { 0x00, 0x00, 0x11, 0x10, 0x2a, 0x90, 0x44, 0x40, 0x80, 0x20, 0x80, 0x20, 0x44, 0x40, 0x2a, 0x90, 0x11, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// --- GLOBALS ---
int currentPage = 0;
bool highBrightness = true;
int tapCounter = 0;
unsigned long lastTapTime = 0;
bool lastPinState = false;
unsigned long pressStartTime = 0;
bool isLongPressHandled = false;
const unsigned long LONG_PRESS_TIME = 800;
const unsigned long DOUBLE_TAP_DELAY = 300;

// MOODS
#define MOOD_NORMAL 0
#define MOOD_HAPPY 1
#define MOOD_SURPRISED 2
#define MOOD_SLEEPY 3
#define MOOD_ANGRY 4
#define MOOD_SAD 5
#define MOOD_EXCITED 6
#define MOOD_LOVE 7
#define MOOD_SUSPICIOUS 8
int currentMood = MOOD_NORMAL;

String wifiSsid; // Loaded from Preferences
String wifiPass; // Loaded from Preferences
const char* ntpServer = "pool.ntp.org";
String tzString;   // Loaded from Preferences (e.g. "IST-5:30")

// ==================================================
// 2. ULTRA PRO PHYSICS ENGINE
// ==================================================

struct Eye {
  float x, y;  // Current position (Top-Left)
  float w, h;  // Current size
  float targetX, targetY, targetW, targetH;

  // Pupil Physics (Secondary Motion)
  float pupilX, pupilY;
  float targetPupilX, targetPupilY;

  // Physics constants
  float velX, velY, velW, velH;
  float pVelX, pVelY;
  float k = 0.12;   // Eye Spring
  float d = 0.60;   // Eye Damping (Heavier feel)
  float pk = 0.08;  // Pupil Spring (Softer/Laggier)
  float pd = 0.50;  // Pupil Damping

  bool blinking;
  unsigned long lastBlink;
  unsigned long nextBlinkTime;

  void init(float _x, float _y, float _w, float _h) {
    x = targetX = _x;
    y = targetY = _y;
    w = targetW = _w;
    h = targetH = _h;
    pupilX = targetPupilX = 0;
    pupilY = targetPupilY = 0;
    nextBlinkTime = millis() + random(1000, 4000);
  }

  void update() {
    // 1. Main Eye Physics
    float ax = (targetX - x) * k;
    float ay = (targetY - y) * k;
    float aw = (targetW - w) * k;
    float ah = (targetH - h) * k;

    velX = (velX + ax) * d;
    velY = (velY + ay) * d;
    velW = (velW + aw) * d;
    velH = (velH + ah) * d;

    x += velX;
    y += velY;
    w += velW;
    h += velH;

    // 2. Pupil Physics (Dragging behind)
    float pax = (targetPupilX - pupilX) * pk;
    float pay = (targetPupilY - pupilY) * pk;
    pVelX = (pVelX + pax) * pd;
    pVelY = (pVelY + pay) * pd;
    pupilX += pVelX;
    pupilY += pVelY;
  }
};

Eye leftEye, rightEye;
unsigned long lastSaccade = 0;
unsigned long saccadeInterval = 3000;
float breathVal = 0;

// ==================================================
// 2b. CONFIG PORTAL (WiFi + timezone via local web)
// ==================================================
#define CONFIG_AP_SSID   "DeskBuddy-Setup"
#define CONFIG_AP_PASS   "12345678"
#define CONFIG_HOLD_MS   3000

Preferences prefs;
WebServer configServer(80);
bool inConfigMode = false;

void loadConfig() {
  prefs.begin("deskbuddy", true);
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  tzString = prefs.getString("tz", "");
  prefs.end();
  if (wifiSsid.isEmpty()) {
    wifiSsid = "edison science corner";
    wifiPass = "eeeeeeee";
  }
  if (tzString.isEmpty()) tzString = "GST-4";  // UAE time (UTC+4); change in the setup portal
}

void saveConfig(const String& s, const String& p, const String& tz) {
  prefs.begin("deskbuddy", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  prefs.putString("tz", tz);
  prefs.end();
}

void handleConfigRoot() {
  prefs.begin("deskbuddy", true);
  String sSsid = prefs.getString("ssid", "");
  String sTz   = prefs.getString("tz", "GST-4");
  prefs.end();

  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskBuddy Config</title>
<style>
body{font-family:sans-serif;max-width:420px;margin:30px auto;padding:24px;background:#0c1929;color:#e8f4fc;}
h1{color:#5ba3f5;margin-bottom:8px;}
input{width:100%;padding:10px;margin:6px 0;border:1px solid #2d4a6f;border-radius:6px;box-sizing:border-box;background:#1a2d47;color:#e8f4fc;}
input:focus{outline:none;border-color:#5ba3f5;}
button{width:100%;padding:12px;background:#3498db;color:#fff;border:none;border-radius:6px;font-size:16px;cursor:pointer;margin-top:16px;}
button:hover{background:#2980b9;}
label{display:block;margin-top:14px;color:#8ab4e8;font-size:14px;}
.section{margin-top:20px;padding-top:16px;border-top:1px solid #1e3a5f;}
.section-title{color:#5ba3f5;font-size:13px;margin-bottom:8px;}
</style></head><body>
<h1>DeskBuddy Setup</h1>
<form action="/save" method="POST">
<label>WiFi SSID</label><input name="ssid" placeholder="Your WiFi name" value=")rawliteral";
  html += sSsid;
  html += R"rawliteral(">
<label>WiFi Password</label><input name="pass" type="password" placeholder="WiFi password">
<div class="section"><div class="section-title">Time</div>
<label>Timezone</label><input name="tz" placeholder="e.g. GST-4, IST-5:30, EST5EDT" value=")rawliteral";
  html += sTz;
  html += R"rawliteral(">
</div>
<button type="submit">Save &amp; Reboot</button>
</form></body></html>)rawliteral";
  configServer.send(200, "text/html", html);
}

void handleConfigSave() {
  if (!configServer.hasArg("ssid") || configServer.arg("ssid").length() == 0) {
    configServer.send(400, "text/plain", "SSID required");
    return;
  }
  String s  = configServer.arg("ssid");
  String p  = configServer.arg("pass");
  String tz = configServer.arg("tz");
  if (tz.isEmpty()) tz = "GST-4";
  saveConfig(s, p, tz);
  configServer.send(200, "text/html",
    "<html><body style='font-family:sans-serif;background:#0c1929;color:#e8f4fc;padding:40px;'>"
    "<h2 style='color:#5ba3f5'>Saved!</h2><p>Rebooting in 2 seconds...</p></body></html>");
  delay(2000);
  ESP.restart();
}

void startConfigPortal() {
  inConfigMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASS);
  configServer.on("/", handleConfigRoot);
  configServer.on("/save", HTTP_POST, handleConfigSave);
  configServer.begin();
  display.clearDisplay();
  display.setFont(NULL);
  display.setCursor(0, 0);
  display.print("Config mode\n\nConnect to:\n");
  display.print(CONFIG_AP_SSID);
  display.print("\n\nThen open:\n192.168.4.1");
  display.display();
}

// ==================================================
// 3. LOGIC & NETWORK
// ==================================================
// --- BUZZER (non-blocking) ---
unsigned long buzzerOffTime = 0;
bool buzzerOn = false;
// Most small buzzers are loudest around 2500-4000 Hz, so the beeps use that range.
const int PAGE_BEEP_FREQ = 2700;  // page-change beep pitch (Hz)
const int PAGE_BEEP_MS   = 100;   // page-change beep length (ms)
const int START_BEEP_FREQ = 3200; // stopwatch START beep (higher)
const int STOP_BEEP_FREQ  = 2300; // stopwatch STOP beep  (lower)
const int SW_BEEP_MS      = 120;  // start / stop beep length (ms)

void buzzerInit() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZER_PIN, 2000, 8);
#else
  ledcSetup(0, 2000, 8);
  ledcAttachPin(BUZZER_PIN, 0);
#endif
}

// beep(freq, ms)
//   freq = pitch in Hz   (higher number = higher, squeakier sound)
//   ms   = length in milliseconds (1000 ms = 1 second)
// Sound list used in this program:
//   Startup            2000 Hz, 100 ms
//   Page change        2700 Hz, 100 ms  (on tap)
//   Double tap         2000 Hz,  80 ms
//   Long press         1000 Hz, 150 ms
//   Button tap         3200 Hz (start) or 2300 Hz (stop), 120 ms
//   Button hold 1 s     900 Hz,  80 ms  (reset)
//   Stopwatch tick     4000 Hz,  10 ms  (every second while running)
//   10-minute alert    2500 Hz, 150 ms, repeated 3 times (150 ms gap)
void beep(int freq, int ms) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(BUZZER_PIN, freq);
#else
  ledcWriteTone(0, freq);
#endif
  buzzerOn = true;
  buzzerOffTime = millis() + ms;
}

void buzzerUpdate() {
  if (buzzerOn && millis() > buzzerOffTime) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWriteTone(BUZZER_PIN, 0);
#else
    ledcWriteTone(0, 0);
#endif
    buzzerOn = false;
  }
}

// --- LDR: AUTO BRIGHTNESS + RELAY (one shared reading on GPIO34) ---
unsigned long lastLdrRead = 0;
float ldrSmooth = 2000;
const int LDR_THRESHOLD  = 2000;  // same threshold as ldrfinalcode.ino
const int LDR_HYSTERESIS = 150;   // stops the relay chattering around the threshold
bool relayOn = false;

void setRelay(bool on) {
  relayOn = on;
  digitalWrite(RELAY_PIN, on ? LOW : HIGH);  // active-LOW module
}

unsigned long lastLdrPrint = 0;
int lastContrast = -1;

void updateLdr() {
  unsigned long now = millis();
  if (now - lastLdrRead < 100) return;  // check 10 times per second
  lastLdrRead = now;

  // average 4 quick samples to remove ADC noise, but no slow filtering
  long sum = 0;
  for (int i = 0; i < 4; i++) sum += analogRead(LDR_PIN);
  int ldrRaw = sum / 4;

  // slow filter is used ONLY for the OLED brightness (so the screen doesn't flicker)
  ldrSmooth = ldrSmooth * 0.95 + ldrRaw * 0.05;

  // ---- Relay: uses the fast raw value (with hysteresis) ----
#if LDR_HIGH_MEANS_DARK
  bool dark = relayOn ? (ldrRaw > LDR_THRESHOLD - LDR_HYSTERESIS)
                      : (ldrRaw > LDR_THRESHOLD + LDR_HYSTERESIS);
#else
  bool dark = relayOn ? (ldrRaw < LDR_THRESHOLD + LDR_HYSTERESIS)
                      : (ldrRaw < LDR_THRESHOLD - LDR_HYSTERESIS);
#endif
  if (dark != relayOn) setRelay(dark);

  if (now - lastLdrPrint > 500) {
    lastLdrPrint = now;
    Serial.print("LDR: ");
    Serial.print(ldrRaw);
    Serial.println(relayOn ? "  Dark - Relay ON" : "  Bright - Relay OFF");
  }

  // ---- OLED auto brightness (skipped when user chose manual dim) ----
  if (!highBrightness) return;
#if LDR_HIGH_MEANS_DARK
  int contrast = map((int)ldrSmooth, 0, 4095, 255, 10);  // dark room -> dim screen
#else
  int contrast = map((int)ldrSmooth, 0, 4095, 10, 255);
#endif
  contrast = constrain(contrast, 10, 255);
  if (contrast != lastContrast) {
    lastContrast = contrast;
    display.setContrast(contrast);
  }
}

// --- STOPWATCH (non-blocking, keeps running on any page) ---
const unsigned long SW_ALERT_INTERVAL = 600000UL;  // 10 minutes=10*60*1000
unsigned long swStartTime = 0;
unsigned long swElapsed = 0;
unsigned long swLastAlert = 0;
bool swRunning = false;
int swBeepsLeft = 0;
unsigned long swBeepNext = 0;
unsigned long swLastTickSec = 0;   // last whole second that ticked
const int TICK_FREQ = 4000;        // tick pitch (Hz)
const int TICK_MS = 10;            // tick length (ms)

void startStopwatch() {
  if (!swRunning) {
    swStartTime = millis() - swElapsed;
    swRunning = true;
  }
}
void pauseStopwatch() {
  if (swRunning) {
    swElapsed = millis() - swStartTime;
    swRunning = false;
  }
}
void resetStopwatch() {
  swElapsed = 0;
  swLastAlert = 0;
  swRunning = false;
  swBeepsLeft = 0;
  swLastTickSec = 0;
}
void toggleStopwatch() {
  if (swRunning) pauseStopwatch();
  else startStopwatch();
}

void stopwatchUpdate() {
  if (swRunning) {
    swElapsed = millis() - swStartTime;
    unsigned long alertCount = swElapsed / SW_ALERT_INTERVAL;
    if (alertCount > swLastAlert) {  // 10-minute mark reached
      swLastAlert = alertCount;
      swBeepsLeft = 3;
      swBeepNext = millis();
    }
    // tick once per second on ANY page (skipped if another sound is playing)
    unsigned long sec = swElapsed / 1000;
    if (sec != swLastTickSec) {
      swLastTickSec = sec;
      if (!buzzerOn && swBeepsLeft == 0) beep(TICK_FREQ, TICK_MS);
    }
  }
  // three short beeps, 150 ms on / 150 ms off, without blocking
  if (swBeepsLeft > 0 && millis() >= swBeepNext) {
    beep(2500, 150);
    swBeepsLeft--;
    swBeepNext = millis() + 300;
  }
}

// --- STOPWATCH BUTTON on D17 (debounced, active LOW) ---
//   short press (released before 1 s) -> START / STOP
//   hold for 1 s                      -> RESET (fires while still held)
const unsigned long BTN_DEBOUNCE = 40;
const unsigned long BTN_HOLD_RESET = 1000;
bool btnStable = HIGH;
bool btnLastRead = HIGH;
unsigned long btnLastChange = 0;
unsigned long btnPressStart = 0;
bool btnHoldHandled = false;

void handleStopwatchButton() {
  bool r = digitalRead(BTN_SW_PIN);
  unsigned long now = millis();
  if (r != btnLastRead) {
    btnLastRead = r;
    btnLastChange = now;
  }

  // debounced edge
  if ((now - btnLastChange > BTN_DEBOUNCE) && (r != btnStable)) {
    btnStable = r;
    if (btnStable == LOW) {               // just pressed
      btnPressStart = now;
      btnHoldHandled = false;
    } else if (!btnHoldHandled) {         // released before 1 s -> START/STOP
      toggleStopwatch();
      if (swRunning) swLastTickSec = swElapsed / 1000;  // first tick at next full second
      beep(swRunning ? START_BEEP_FREQ : STOP_BEEP_FREQ, SW_BEEP_MS);
      Serial.println(swRunning ? "D17 tap: started" : "D17 tap: stopped");
    }
  }

  // still held for 1 s -> RESET
  if (btnStable == LOW && !btnHoldHandled && now - btnPressStart >= BTN_HOLD_RESET) {
    btnHoldHandled = true;
    resetStopwatch();
    beep(900, 80);
    Serial.println("D17 hold: stopwatch reset to 00:00:00");
  }
}

void handleTouch() {
  bool currentPinState = digitalRead(TOUCH_PIN);
  unsigned long now = millis();
  if (currentPinState && !lastPinState) {
    pressStartTime = now;
    isLongPressHandled = false;
  } else if (currentPinState && lastPinState) {
    if ((now - pressStartTime > LONG_PRESS_TIME) && !isLongPressHandled) {
      if (currentPage == 0) {
        currentMood++;
        if (currentMood > MOOD_SUSPICIOUS) currentMood = 0;
        lastSaccade = 0;  // Trigger move
      } else if (currentPage == 1) currentPage = 3;
      isLongPressHandled = true;
      beep(1000, 150);
    }
  } else if (!currentPinState && lastPinState) {
    if ((now - pressStartTime < LONG_PRESS_TIME) && !isLongPressHandled) {
      tapCounter++;
      lastTapTime = now;
    }
  }
  lastPinState = currentPinState;
  if (tapCounter > 0) {
    if (now - lastTapTime > DOUBLE_TAP_DELAY) {
      if (tapCounter == 2) {
        beep(2000, 80);
        highBrightness = !highBrightness;
        lastContrast = -1;
        display.setContrast(highBrightness ? 255 : 1);
        display.display();
      } else if (tapCounter == 1) {
        beep(PAGE_BEEP_FREQ, PAGE_BEEP_MS);     // page-change beep
        if (currentPage == 3) currentPage = 1;  // world clock -> clock
        else {
          currentPage++;                        // eyes -> clock -> stopwatch -> eyes
          if (currentPage > PAGE_STOPWATCH) currentPage = 0;
        }
      }
      tapCounter = 0;
    }
  }
}

// ==================================================
// 4. DRAWING & ANIMATION
// ==================================================

void drawEyelidMask(float x, float y, float w, float h, int mood, bool isLeft) {
  int ix = (int)x;
  int iy = (int)y;
  int iw = (int)w;
  int ih = (int)h;
  display.setTextColor(SH110X_BLACK);

  // ANGRY: Sharp slanted cut
  if (mood == MOOD_ANGRY) {
    if (isLeft)
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy + i, ix + iw, iy - 6 + i, SH110X_BLACK);
    else
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy - 6 + i, ix + iw, iy + i, SH110X_BLACK);
  }
  // SAD: Inverse slanted cut
  else if (mood == MOOD_SAD) {
    if (isLeft)
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy - 6 + i, ix + iw, iy + i, SH110X_BLACK);
    else
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy + i, ix + iw, iy - 6 + i, SH110X_BLACK);
  }
  // HAPPY/LOVE: Cheek push up
  else if (mood == MOOD_HAPPY || mood == MOOD_LOVE || mood == MOOD_EXCITED) {
    display.fillRect(ix, iy + ih - 12, iw, 14, SH110X_BLACK);
    display.fillCircle(ix + iw / 2, iy + ih + 6, iw / 1.3, SH110X_BLACK);  // Round cut
  }
  // SLEEPY: Heavy lids
  else if (mood == MOOD_SLEEPY) {
    display.fillRect(ix, iy, iw, ih / 2 + 2, SH110X_BLACK);
  }
  // SUSPICIOUS: One eye squint, one open
  else if (mood == MOOD_SUSPICIOUS) {
    if (isLeft) display.fillRect(ix, iy, iw, ih / 2 - 2, SH110X_BLACK);
    else display.fillRect(ix, iy + ih - 8, iw, 8, SH110X_BLACK);
  }
}

void drawUltraProEye(Eye& e, bool isLeft) {
  int ix = (int)e.x;
  int iy = (int)e.y;
  int iw = (int)e.w;
  int ih = (int)e.h;

  // 1. Draw Sclera (White base)
  int r = 8;
  if (iw < 20) r = 3;
  display.fillRoundRect(ix, iy, iw, ih, r, SH110X_WHITE);

  // 2. Draw Pupil (Black inner eye) - Constrained to be inside
  // Calculate center of eye
  int cx = ix + iw / 2;
  int cy = iy + ih / 2;

  // Pupil size is proportional to eye size
  int pw = iw / 2.2;
  int ph = ih / 2.2;

  // Apply pupil offset (Gaze)
  int px = cx + (int)e.pupilX - (pw / 2);
  int py = cy + (int)e.pupilY - (ph / 2);

  // Clamp pupil to ensure it doesn't leave the eye completely
  if (px < ix) px = ix;
  if (px + pw > ix + iw) px = ix + iw - pw;
  if (py < iy) py = iy;
  if (py + ph > iy + ih) py = iy + ih - ph;

  display.fillRoundRect(px, py, pw, ph, r / 2, SH110X_BLACK);

  // 3. Draw Specular Highlight (The "Glint" of life)
  // A tiny white dot in the top-right of the pupil
  if (iw > 15 && ih > 15) {
    display.fillCircle(px + pw - 4, py + 4, 2, SH110X_WHITE);
  }

  // 4. Apply Eyelid Masks (Expressions)
  drawEyelidMask(e.x, e.y, e.w, e.h, currentMood, isLeft);
}

void updatePhysicsAndMood() {
  unsigned long now = millis();
  breathVal = sin(now / 800.0) * 1.5;  // Breathing effect

  // --- BLINK LOGIC ---
  if (now > leftEye.nextBlinkTime) {
    leftEye.blinking = true;
    leftEye.lastBlink = now;
    rightEye.blinking = true;
    leftEye.nextBlinkTime = now + random(2000, 6000);
  }
  if (leftEye.blinking) {
    leftEye.targetH = 2;
    rightEye.targetH = 2;  // Close
    if (now - leftEye.lastBlink > 120) {
      leftEye.blinking = false;
      rightEye.blinking = false;
    }
  }

  // --- SACCADE (Gaze) LOGIC ---
  if (!leftEye.blinking && now - lastSaccade > saccadeInterval) {
    lastSaccade = now;
    saccadeInterval = random(500, 3000);

    // Pick direction
    int dir = random(0, 10);
    float lx = 0, ly = 0;

    if (dir < 4) {
      lx = 0;
      ly = 0;
    }  // Center
    else if (dir == 4) {
      lx = -6;
      ly = -4;
    }  // TL
    else if (dir == 5) {
      lx = 6;
      ly = -4;
    }  // TR
    else if (dir == 6) {
      lx = -6;
      ly = 4;
    }  // BL
    else if (dir == 7) {
      lx = 6;
      ly = 4;
    }  // BR
    else if (dir == 8) {
      lx = 8;
      ly = 0;
    }  // R
    else if (dir == 9) {
      lx = -8;
      ly = 0;
    }  // L

    // Move pupil target relative to center
    leftEye.targetPupilX = lx;
    leftEye.targetPupilY = ly;
    rightEye.targetPupilX = lx;
    rightEye.targetPupilY = ly;

    // Move eye container slightly (Head follow)
    leftEye.targetX = 18 + (lx * 0.3);
    leftEye.targetY = 14 + (ly * 0.3);
    rightEye.targetX = 74 + (lx * 0.3);
    rightEye.targetY = 14 + (ly * 0.3);
  }

  // --- MOOD SHAPES (Overrides targets) ---
  if (!leftEye.blinking) {
    float baseW = 36;
    float baseH = 36;

    // Breathing effect applied to height
    baseH += breathVal;

    switch (currentMood) {
      case MOOD_NORMAL:
        leftEye.targetW = baseW;
        leftEye.targetH = baseH;
        rightEye.targetW = baseW;
        rightEye.targetH = baseH;
        break;
      case MOOD_HAPPY:
      case MOOD_LOVE:
        leftEye.targetW = 40;
        leftEye.targetH = 32;
        rightEye.targetW = 40;
        rightEye.targetH = 32;
        break;
      case MOOD_SURPRISED:
        leftEye.targetW = 30;
        leftEye.targetH = 45;
        rightEye.targetW = 30;
        rightEye.targetH = 45;
        // Jitter pupil
        leftEye.targetPupilX += random(-1, 2);
        break;
      case MOOD_SLEEPY:
        leftEye.targetW = 38;
        leftEye.targetH = 30;
        rightEye.targetW = 38;
        rightEye.targetH = 30;
        break;
      case MOOD_ANGRY:
        leftEye.targetW = 34;
        leftEye.targetH = 32;
        rightEye.targetW = 34;
        rightEye.targetH = 32;
        break;
      case MOOD_SAD:
        leftEye.targetW = 34;
        leftEye.targetH = 40;
        rightEye.targetW = 34;
        rightEye.targetH = 40;
        break;
      case MOOD_SUSPICIOUS:
        leftEye.targetW = 36;
        leftEye.targetH = 20;  // Left Squint
        rightEye.targetW = 36;
        rightEye.targetH = 42;  // Right Wide
        break;
    }
  }

  leftEye.update();
  rightEye.update();
}

void drawEmoPage() {
  updatePhysicsAndMood();

  // Draw Floating Particles based on Mood
  if (currentMood == MOOD_LOVE) {
    display.drawBitmap(56, 0, bmp_heart, 16, 16, SH110X_WHITE);
  } else if (currentMood == MOOD_SLEEPY) {
    display.drawBitmap(110, 0, bmp_zzz, 16, 16, SH110X_WHITE);
  } else if (currentMood == MOOD_ANGRY) {
    display.drawBitmap(56, 0, bmp_anger, 16, 16, SH110X_WHITE);
  }

  drawUltraProEye(leftEye, true);
  drawUltraProEye(rightEye, false);
}

// --- STANDARD PAGES ---
void drawClock() {
  struct tm t;
  if (!getLocalTime(&t)) {
    display.setFont(NULL);
    display.setCursor(30, 30);
    display.print("Syncing...");
    return;
  }
  String ampm = (t.tm_hour >= 12) ? "PM" : "AM";
  int h12 = t.tm_hour % 12;
  if (h12 == 0) h12 = 12;
  display.setTextColor(SH110X_WHITE);
  display.setFont(NULL);
  display.setTextSize(1);
  display.setCursor(114, 0);
  display.print(ampm);
  display.setFont(&FreeSansBold18pt7b);
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", h12, t.tm_min);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 42);
  display.print(timeStr);
  display.setFont(&FreeSans9pt7b);
  char dateStr[20];
  strftime(dateStr, 20, "%a, %b %d", &t);
  display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 62);
  display.print(dateStr);
}
void drawWorldClock() {
  time_t now;
  time(&now);
  time_t indiaEpoch = now + (5 * 3600) + (30 * 60);
  time_t sydneyEpoch = now + (11 * 3600);
  struct tm* indiatm = gmtime(&indiaEpoch);
  int i_h = indiatm->tm_hour;
  int i_m = indiatm->tm_min;
  struct tm* sydneytm = gmtime(&sydneyEpoch);
  int s_h = sydneytm->tm_hour;
  int s_m = sydneytm->tm_min;
  display.fillRect(0, 0, 128, 16, SH110X_WHITE);
  display.setFont(NULL);
  display.setTextColor(SH110X_BLACK);
  display.setCursor(32, 4);
  display.print("WORLD CLOCK");
  display.setTextColor(SH110X_WHITE);
  display.drawLine(64, 18, 64, 54, SH110X_WHITE);
  display.setFont(NULL);
  display.setCursor(16, 22);
  display.print("INDIA");
  display.setFont(&FreeSansBold9pt7b);
  char iStr[10];
  sprintf(iStr, "%02d:%02d", i_h, i_m);
  display.setCursor(5, 46);
  display.print(iStr);
  display.setFont(NULL);
  display.setCursor(78, 22);
  display.print("SYDNEY");
  display.setFont(&FreeSansBold9pt7b);
  char sStr[10];
  sprintf(sStr, "%02d:%02d", s_h, s_m);
  display.setCursor(69, 46);
  display.print(sStr);
  display.setFont(NULL);
  display.setCursor(35, 56);
  display.print("Tap to Exit");
}

void drawStopwatch() {
  unsigned long totalSeconds = swElapsed / 1000;
  int hours = (totalSeconds / 3600) % 100;
  int minutes = (totalSeconds / 60) % 60;
  int seconds = totalSeconds % 60;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours, minutes, seconds);

  display.setFont(NULL);
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 16, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setCursor(37, 4);
  display.print("STOPWATCH");

  display.setTextColor(SH110X_WHITE);
  display.setTextSize(2);
  display.setCursor(16, 26);
  display.print(buf);

  display.setTextSize(1);
  const char* st = swRunning ? "RUNNING" : (swElapsed == 0 ? "READY" : "STOPPED");
  display.setCursor((128 - strlen(st) * 6) / 2, 54);
  display.print(st);
}

// ==================================================
// 5. BOOT & MAIN
// ==================================================

void playBootAnimation() {
  display.setTextColor(SH110X_WHITE);
  int cx = 64;
  int cy = 32;
  // Expanding Circle Wipe
  for (int r = 0; r < 80; r += 4) {
    display.clearDisplay();
    display.fillCircle(cx, cy, r, SH110X_WHITE);
    display.display();
    delay(10);
  }
  // Inverse Wipe
  for (int r = 0; r < 80; r += 4) {
    display.clearDisplay();
    display.fillCircle(cx, cy, 80, SH110X_WHITE);
    display.fillCircle(cx, cy, r, SH110X_BLACK);
    display.display();
    delay(10);
  }

  // Text Display - "PR-TECH"
  display.setFont(&FreeSansBold9pt7b);
  String bootText = "ESCLabs";

  // Calculate width to center it perfectly
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(bootText, 0, 0, &x1, &y1, &w, &h);

  display.clearDisplay();
  display.setCursor((SCREEN_WIDTH - w) / 2, 36);
  display.print(bootText);
  display.display();
  delay(2000);
}

void setup() {
  Serial.begin(115200);
  digitalWrite(RELAY_PIN, HIGH);  // relay OFF before pin becomes output (active LOW)
  pinMode(RELAY_PIN, OUTPUT);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);  // fast I2C: screen redraw is ~4x quicker, so loop() stays responsive
  pinMode(TOUCH_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(BTN_SW_PIN, INPUT_PULLUP);
  ldrSmooth = analogRead(LDR_PIN);
  buzzerInit();
  display.begin(0x3C, true);
  display.setTextColor(SH110X_WHITE);

  // Hold touch for 3 sec at boot to force config mode
  bool forceConfig = false;
  for (unsigned long t = millis(); millis() - t < CONFIG_HOLD_MS; ) {
    if (digitalRead(TOUCH_PIN)) { forceConfig = true; break; }
    delay(80);
  }

  loadConfig();
  beep(2000, 100);  // startup beep
  delay(120);
  buzzerUpdate();

  if (forceConfig) {
    startConfigPortal();
    return;
  }

  // Init Eyes Center
  leftEye.init(18, 14, 36, 36);
  rightEye.init(74, 14, 36, 36);

  playBootAnimation();

  display.clearDisplay();
  display.setFont(NULL);
  display.setCursor(40, 30);
  display.print("connecting");
  display.display();
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart < 15000)) {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED) {
    startConfigPortal();
    return;
  }
  configTime(0, 0, ntpServer);
  setenv("TZ", tzString.c_str(), 1);
  tzset();
}

void loop() {
  if (inConfigMode) {
    configServer.handleClient();
    return;
  }
  handleTouch();
  handleStopwatchButton();
  buzzerUpdate();
  updateLdr();
  stopwatchUpdate();

  display.clearDisplay();
  switch (currentPage) {
    case 0: drawEmoPage(); break;
    case 1: drawClock(); break;
    case 3: drawWorldClock(); break;
    case PAGE_STOPWATCH: drawStopwatch(); break;
  }
  display.display();
}
