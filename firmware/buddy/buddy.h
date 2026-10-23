#pragma once
// Shared types and constants for the desk buddy. All .ino tabs are compiled as
// one translation unit, so globals live in buddy.ino and are visible everywhere.

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <atomic>

// Native USB (usbwake.ino): built with CDCOnBoot=cdc so the sketch can add a
// keyboard and the remote-wakeup flag before starting USB; Serial is its CDC port.
extern USBCDC USBSerial;
#undef Serial
#define Serial USBSerial
extern "C" bool tud_remote_wakeup(void);

// ---------- Hardware (XIAO ESP32-S3 Plus + 1.47" Touch Display) ----------
static constexpr int8_t LCD_RST_PIN = 13;   // D17
static constexpr int8_t LCD_BL_PIN = 12;    // D18
static constexpr uint8_t BTN_A_PIN = 11;    // D19
static constexpr uint8_t BTN_B_PIN = 42;    // D15
static constexpr uint8_t I2C_SDA_PIN = 5;   // D4
static constexpr uint8_t I2C_SCL_PIN = 6;   // D5

static constexpr int W = 320;  // landscape
static constexpr int H = 172;

static constexpr const char *HOSTNAME = "buddy";
static constexpr const char *SETUP_AP = "Buddy-Setup";
static constexpr const char *TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";  // Europe/Berlin
// Bedtime: sleepy from SLEEPY_HOUR, asleep (and dimmed) from SLEEP_HOUR until WAKE_HOUR
static constexpr int SLEEPY_HOUR = 22, SLEEP_HOUR = 23, WAKE_HOUR = 7;

// ---------- Colours (RGB565) ----------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static constexpr uint16_t BG = rgb(0, 0, 0);
static constexpr uint16_t COL_CYAN = rgb(64, 224, 255);
static constexpr uint16_t COL_PINK = rgb(255, 105, 180);
static constexpr uint16_t COL_RED = rgb(255, 70, 60);
static constexpr uint16_t COL_ORANGE = rgb(255, 160, 40);
static constexpr uint16_t COL_AMBER = rgb(255, 200, 60);
static constexpr uint16_t COL_GREEN = rgb(80, 230, 120);
static constexpr uint16_t COL_BLUE = rgb(90, 140, 255);
static constexpr uint16_t COL_PURPLE = rgb(180, 120, 255);
static constexpr uint16_t COL_WHITE = rgb(255, 255, 255);
static constexpr uint16_t COL_GREY = rgb(120, 120, 130);
static constexpr uint16_t COL_DIM = rgb(45, 45, 55);
static constexpr uint16_t COL_PANEL = rgb(22, 24, 32);

// ---------- State ----------
enum Page : uint8_t { PAGE_FACE, PAGE_WEATHER, PAGE_CLOCK, PAGE_COUNT };

// Pages advance on their own; any touch/button/tilt pauses that for a minute.
static constexpr uint32_t PAGE_SHOW_MS[PAGE_COUNT] = {30000, 15000, 15000};
static constexpr uint32_t CYCLE_PAUSE_MS = 60000;

enum Expr : uint8_t {
  EX_NEUTRAL, EX_HAPPY, EX_LOVE, EX_SURPRISED, EX_ANGRY, EX_SAD,
  EX_SLEEPY, EX_ASLEEP, EX_DIZZY
};

// Weather the buddy fetches itself from Open-Meteo every 15 min (wxnet.ino)
static constexpr int WX_HOURS = 12;
static constexpr int WX_DAYS = 3;
enum WxIcon : uint8_t { WI_CLEAR, WI_PARTLY, WI_CLOUD, WI_FOG, WI_DRIZZLE, WI_RAIN, WI_SNOW, WI_THUNDER };
struct WxDay { int code, hi, lo; char name[4]; };
struct Weather {
  bool valid = false;
  uint32_t receivedAt = 0;   // millis()
  char city[32] = "";
  char at[6] = "";           // HH:MM of the forecast fetch
  int32_t utcOffset = 0;     // seconds, for the location's local time
  float temp = 0, feels = 0, wind = 0;
  int hum = 0, code = 0, hi = 0, lo = 0, pop = 0;
  bool day = true;
  int h0 = 0;                // hour of the first hourly slot
  int nHours = 0;
  int8_t hTemp[WX_HOURS] = {};
  uint8_t hPop[WX_HOURS] = {};
  int nDays = 0;
  WxDay days[WX_DAYS] = {};
};

struct Location { bool valid; float lat, lon; char name[32]; };

struct EyeParams {
  float lx, ly;    // look direction, -1..1
  float w, h;      // eye size in px
  float open;      // 0..1 lid openness
  float slant;     // >0 angry (inner lowered), <0 sad (outer lowered)
  float happy;     // 0..1 crescent "^" eyes
  float mouth;     // smile bend in px (+ smile, - frown)
};

enum ParticleKind : uint8_t { P_NONE, P_HEART, P_ZZZ, P_SWEAT, P_STAR };

struct Particle {
  ParticleKind kind;
  float x, y, vx, vy;
  float life, maxLife;
  float size;
};

struct Button {
  uint8_t pin;
  int idleLevel;
  bool pressed;
  uint32_t changedAt;
  bool longFired;
};


extern bool perfLog;   // PERF serial command: frame timing + touch logs
extern int32_t tzOffsetApplied;  // wxnet.ino, INT32_MIN until a location offset is known
extern Location pinnedLoc;

// usbwake.ino
bool macAsleep();
bool macDark();
bool macDisplayOff();
void setMacDisplay(bool on);
const char *usbStateName();
const char *wakeMac();

// Shared weather drawing helpers (weather.ino), used by the clock page too
const char *wxText(int code);
int textDeg(const char *txt, int x, int y, int font, uint16_t col);
void drawWxIcon(float cx, float cy, float s, int code, bool day);
