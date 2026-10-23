#include <functional>
/*
  Desk Buddy — XIAO ESP32-S3 Plus + 1.47" touch display (JD9853A + AXS5106L)

  Pages (cycle on their own; swipe left/right or press the side buttons):
    FACE     animated buddy that reacts to touch, tilt and shake, and sleeps at night
    WEATHER  Open-Meteo forecast
    CLOCK    time, date and a weather summary
*/

#include "buddy.h"
#include <Seeed_GFX.h>
#include "board/boards/XIAO_LCD_Board.h"
#include "driver/tft/Driver_JD9853A.h"
#include "panel/Panel_TFT.h"
#include "touch/Touch_AXS5106L.h"
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <atomic>
#include <time.h>

Seeed_GFX display;
Seeed_Sprite spr;
Touch_AXS5106L touch(-1, D7, Wire, 172, 320);
WebServer server(80);
DNSServer dns;
Preferences prefs;

Page page = PAGE_FACE;
Weather wx;
float fps = 0;  // smoothed frame rate, reported in /api/state
uint8_t rotation = 1;
bool setupMode = false;
bool wifiUp = false;
bool perfLog = false;
uint32_t pageShownAt = 0;

// Backlight
uint8_t blTarget = 255;
float blLevel = 255;
uint32_t lastInteraction = 0;

void setPage(int p) {
  page = (Page)((p + PAGE_COUNT) % PAGE_COUNT);
  pageShownAt = millis();
}

void noteInteraction() { lastInteraction = millis(); }

void setup() {
  usbBegin();
  delay(300);
  Serial.println("\n=== Desk Buddy ===");

  pinMode(BTN_A_PIN, INPUT_PULLUP);
  pinMode(BTN_B_PIN, INPUT_PULLUP);

  if (!display.begin<Board_XIAO_1inch47_Touch_Display<LCD_RST_PIN, LCD_BL_PIN>,
                     Config_Seeed_1inch47_Touch_JD9853A>()) {
    Serial.printf("[LCD] %s\n", display.lastResult().message);
  }
  if (!display.attachTouch(touch, display.panel().driver().bus())) {
    Serial.printf("[TOUCH] %s\n", display.lastResult().message);
  }

  prefs.begin("buddy", false);
  rotation = prefs.getUChar("rot", 1) == 3 ? 3 : 1;
  display.setRotation(rotation);
  display.fillScreen(BG);

  ledcAttach(LCD_BL_PIN, 5000, 8);
  ledcWrite(LCD_BL_PIN, 255);

  spr.setColorDepth(16);
  if (!spr.createSprite(display, W, H)) {
    Serial.println("[GFX] sprite alloc failed");
  }

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);
  imuInit();

  faceInit();
  netBegin();
  wxBegin();
  lastInteraction = millis();
  Serial.printf("[BOOT] display %dx%d, rot=%d, free heap %u, psram %u\n",
                display.width(), display.height(), rotation,
                ESP.getFreeHeap(), ESP.getFreePsram());
}

void loop() {
  static uint32_t lastFrame = millis();
  uint32_t now = millis();
  float dt = (now - lastFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastFrame = now;

  netLoop();
  uint32_t tNet = millis();
  serialLoop();
  imuUpdate(dt);
  uint32_t tImu = millis();
  inputUpdate();
  uint32_t tInput = millis();


  wxLoop();

  // Show the next page now and then, unless someone is playing with the buddy
  if (!setupMode && now - pageShownAt > PAGE_SHOW_MS[page] && now - lastInteraction > CYCLE_PAUSE_MS) {
    setPage(page + 1);
  }

  updateBacklight(dt);

  // Screen is off while the Mac is dark; skip drawing until it lights up
  if (macDark() && blLevel < 1) {
    delay(50);
    return;
  }

  if (setupMode) {
    drawSetupScreen();
  } else {
    switch (page) {
      case PAGE_FACE: faceUpdate(dt); faceDraw(); break;
      case PAGE_WEATHER: drawWeather(); break;
      case PAGE_CLOCK: drawClock(); break;
      default: break;
    }
    drawPageDots();
  }
  uint32_t tDraw = millis();
  pushFrame();
  if (perfLog) perfSample(now, tNet, tImu, tInput, tDraw, millis());

  static uint32_t prevEnd = 0;
  uint32_t end = millis();
  if (end > prevEnd) fps += (1000.0f / (end - prevEnd) - fps) * 0.05f;
  prevEnd = end;

  // ~40 fps cap
  uint32_t spent = millis() - now;
  if (spent < 25) delay(25 - spent);
}

void updateBacklight(float dt) {
  uint32_t now = millis();
  // Dim at night while the buddy sleeps; the info pages stay readable
  bool sleepy = !setupMode && baseExpr() == EX_ASLEEP && now - lastInteraction > 15000;
  blTarget = sleepy ? (page == PAGE_FACE ? 18 : 50) : 255;
  if (macDark()) blTarget = 0;  // the Mac sleeps or its display is off, so does the screen
  blLevel += (blTarget - blLevel) * fminf(1.0f, dt * 3.0f);
  ledcWrite(LCD_BL_PIN, (uint8_t)blLevel);
}

// Send the whole sprite in one address window (pushSprite opens a window per row).
void pushFrame() {
  const bool swap = display.getSwapBytes();
  display.setSwapBytes(false);
  display.startWrite();
  display.setAddrWindow(0, 0, W, H);
  display.pushPixels(spr.frameBuffer(0), W * H);
  display.endWrite();
  display.setSwapBytes(swap);
}

// Time individual drawing primitives (BENCH serial command)
void runBench() {
  auto t = [](const char *name, std::function<void()> f) {
    uint32_t t0 = micros();
    for (int i = 0; i < 20; i++) f();
    Serial.printf("[BENCH] %-22s %7.1f us\n", name, (micros() - t0) / 20.0f);
  };
  t("fillSprite", [] { spr.fillSprite(BG); });
  t("fillRect 60x60", [] { spr.fillRect(10, 10, 60, 60, COL_WHITE); });
  t("fillSmoothRoundRect", [] { spr.fillSmoothRoundRect(100, 40, 56, 70, 18, COL_WHITE, BG); });
  t("fillSmoothCircle r4", [] { spr.fillSmoothCircle(50, 50, 4, COL_WHITE, BG); });
  t("drawWideLine 10px", [] { spr.drawWideLine(20, 20, 30, 22, 4, COL_WHITE, BG); });
  t("fillEllipse 16x7", [] { spr.fillEllipse(60, 60, 16, 7, COL_WHITE); });
  t("fillTriangle", [] { spr.fillTriangle(10, 10, 60, 10, 10, 40, COL_WHITE); });
  t("drawString font2", [] { spr.drawString("12:34", 6, 3, 2); });
  t("drawString font4", [] { spr.drawString("42%", 60, 60, 4); });
  t("faceDraw", [] { faceDraw(); });
  t("drawClock", [] { drawClock(); });
  t("drawWeather", [] { drawWeather(); });
}

// Frame timing over serial (toggle with the PERF command)

void perfSample(uint32_t t0, uint32_t tNet, uint32_t tImu, uint32_t tInput, uint32_t tDraw, uint32_t tEnd) {
  static uint32_t frames = 0, windowStart = 0, sum[5] = {0}, peak[5] = {0};
  uint32_t parts[5] = {tNet - t0, tImu - tNet, tInput - tImu, tDraw - tInput, tEnd - tDraw};
  for (int i = 0; i < 5; i++) {
    sum[i] += parts[i];
    if (parts[i] > peak[i]) peak[i] = parts[i];
  }
  frames++;
  if (tEnd - windowStart >= 2000) {
    const char *names[5] = {"net", "imu", "input", "draw", "push"};
    Serial.printf("[PERF] %.1f fps |", frames * 1000.0f / (tEnd - windowStart));
    for (int i = 0; i < 5; i++) Serial.printf(" %s %.1f/%lu", names[i], (float)sum[i] / frames, (unsigned long)peak[i]);
    Serial.println(" ms (avg/max)");
    frames = 0;
    windowStart = tEnd;
    memset(sum, 0, sizeof(sum));
    memset(peak, 0, sizeof(peak));
  }
}

// Serial commands (e.g. to provision WiFi over USB):
//   WIFI <ssid>\t<password>   save credentials and reboot
//   ROT <1|3>                 flip the landscape orientation
//   RESETWIFI                 forget credentials
//   LOC <lat,lon[,Name]>      pin the weather location (LOC alone: IP lookup)
//   WAKE                      wake the Mac this buddy is plugged into
//   PERF / BENCH / STATUS     debugging
void serialLoop() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (line.length() < 200) line += c;
      continue;
    }
    if (line.startsWith("WIFI ")) {
      String rest = line.substring(5);
      int tab = rest.indexOf('\t');
      String ssid = tab >= 0 ? rest.substring(0, tab) : rest;
      String pass = tab >= 0 ? rest.substring(tab + 1) : "";
      prefs.putString("ssid", ssid);
      prefs.putString("pass", pass);
      Serial.printf("[WIFI] saved '%s', rebooting\n", ssid.c_str());
      delay(200);
      ESP.restart();
    } else if (line.startsWith("ROT ")) {
      rotation = line.substring(4).toInt() == 3 ? 3 : 1;  // landscape only
      prefs.putUChar("rot", rotation);
      display.setRotation(rotation);
      Serial.printf("[ROT] %d\n", rotation);
    } else if (line == "RESETWIFI") {
      prefs.remove("ssid");
      prefs.remove("pass");
      Serial.println("[WIFI] cleared, rebooting");
      delay(200);
      ESP.restart();
    } else if (line == "LOC" || line.startsWith("LOC ")) {
      if (!setLocation(line.length() > 4 ? line.substring(4) : "")) Serial.println("[WX] usage: LOC lat,lon[,Name]");
    } else if (line == "WAKE") {
      Serial.printf("[WAKE] %s\n", wakeMac());
    } else if (line == "BENCH") {
      runBench();
    } else if (line == "PERF") {
      perfLog = !perfLog;
      Serial.printf("[PERF] logging %s\n", perfLog ? "on" : "off");
    } else if (line == "STATUS") {
      Serial.printf("[STATUS] wifi=%d ip=%s page=%d heap=%u\n",
                    wifiUp, WiFi.localIP().toString().c_str(), page, ESP.getFreeHeap());
    }
    line = "";
  }
}
