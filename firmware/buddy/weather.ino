// ===================== Weather page =====================
// Data comes from Open-Meteo (WMO weather codes), fetched in wxnet.ino. Tap the page to
// switch the bottom strip between the next 12 hours and the next 3 days.

bool wxShowDays = false;

static WxIcon wxIconFor(int code) {
  if (code <= 1) return WI_CLEAR;
  if (code == 2) return WI_PARTLY;
  if (code == 3) return WI_CLOUD;
  if (code == 45 || code == 48) return WI_FOG;
  if (code >= 51 && code <= 57) return WI_DRIZZLE;
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return WI_RAIN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WI_SNOW;
  if (code >= 95) return WI_THUNDER;
  return WI_CLOUD;
}

const char *wxText(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Storm with hail";
    default: return "";
  }
}

// ---- icons (s = scale, ~1 means a 40 px wide icon) ----

static void wxSun(float cx, float cy, float s) {
  float spin = millis() * 0.0004f;
  for (int i = 0; i < 8; i++) {
    float a = spin + i * PI / 4;
    aaLine(cx + cosf(a) * 13 * s, cy + sinf(a) * 13 * s, cx + cosf(a) * 18 * s, cy + sinf(a) * 18 * s,
           2.5f * s, COL_AMBER);
  }
  aaCircle(cx, cy, 9.5f * s, COL_AMBER);
}

static void wxMoon(float cx, float cy, float s) {
  aaCircle(cx, cy, 11 * s, rgb(235, 225, 190));
  aaCircle(cx + 6 * s, cy - 4 * s, 9 * s, BG);  // bite out of it -> crescent
}

static void wxCloud(float cx, float cy, float s, uint16_t col) {
  aaCircle(cx - 9 * s, cy + 3 * s, 8 * s, col);
  aaCircle(cx + 1 * s, cy - 3 * s, 11 * s, col);
  aaCircle(cx + 11 * s, cy + 3 * s, 8 * s, col);
  aaRoundRect(cx - 17 * s, cy + 1 * s, 36 * s, 10 * s, 5 * s, col);
}

static void wxDrops(float cx, float cy, float s, int n, bool snow) {
  uint32_t t = millis();
  for (int i = 0; i < n; i++) {
    float x = cx + (i - (n - 1) / 2.0f) * 9 * s;
    float ph = fmodf(t * 0.0012f + i * 0.37f, 1.0f);  // 0..1 falling loop
    float y = cy + ph * 12 * s;
    uint16_t c = blend565(snow ? COL_WHITE : COL_BLUE, BG, ph * 0.8f);
    if (snow) aaCircle(x, y, 2 * s, c);
    else aaLine(x, y, x - 2 * s, y + 5 * s, 2 * s, c);
  }
}

void drawWxIcon(float cx, float cy, float s, int code, bool day) {
  const uint16_t cloud = rgb(200, 205, 215), dark = rgb(110, 115, 130);
  switch (wxIconFor(code)) {
    case WI_CLEAR:
      if (day) wxSun(cx, cy, s); else wxMoon(cx, cy, s);
      break;
    case WI_PARTLY:
      if (day) wxSun(cx + 8 * s, cy - 8 * s, s * 0.75f); else wxMoon(cx + 8 * s, cy - 8 * s, s * 0.75f);
      wxCloud(cx - 2 * s, cy + 4 * s, s * 0.85f, cloud);
      break;
    case WI_CLOUD:
      wxCloud(cx + 6 * s, cy - 6 * s, s * 0.6f, dark);
      wxCloud(cx - 2 * s, cy + 3 * s, s * 0.9f, cloud);
      break;
    case WI_FOG:
      for (int i = 0; i < 4; i++) {
        float y = cy - 9 * s + i * 6 * s, off = (i % 2) * 5 * s;
        aaLine(cx - 16 * s + off, y, cx + 12 * s + off, y, 3 * s, dark);
      }
      break;
    case WI_DRIZZLE:
      wxDrops(cx, cy + 9 * s, s, 2, false);
      wxCloud(cx, cy - 4 * s, s * 0.9f, cloud);
      break;
    case WI_RAIN:
      wxDrops(cx, cy + 9 * s, s, 3, false);
      wxCloud(cx, cy - 4 * s, s * 0.9f, dark);
      break;
    case WI_SNOW:
      wxDrops(cx, cy + 9 * s, s, 3, true);
      wxCloud(cx, cy - 4 * s, s * 0.9f, cloud);
      break;
    case WI_THUNDER: {
      bool flash = (millis() / 120) % 25 == 0;
      wxCloud(cx, cy - 4 * s, s * 0.9f, flash ? COL_WHITE : dark);
      spr.fillTriangle(cx + 2 * s, cy + 2 * s, cx - 6 * s, cy + 13 * s, cx + 1 * s, cy + 12 * s, COL_AMBER);
      spr.fillTriangle(cx - 1 * s, cy + 10 * s, cx + 6 * s, cy + 10 * s, cx - 4 * s, cy + 23 * s, COL_AMBER);
      break;
    }
  }
}

// Text followed by a small drawn degree ring (the fonts have no degree sign).
// Uses TL datum; returns the x where the ring ends.
int textDeg(const char *txt, int x, int y, int font, uint16_t col) {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(col);
  spr.drawString(txt, x, y, font);
  int tw = spr.textWidth(txt, font);
  float r = font >= 6 ? 5.5f : (font == 4 ? 3.5f : 2.5f);
  float th = font >= 6 ? 2.2f : 1.4f;
  aaArc(x + tw + r + 2, y + r + 1, r, r - th, 0, 360, col, false);
  return x + tw + 2 * r + 3;
}

static void drawHours() {
  const int x0 = 14, x1 = W - 14, yTop = 118, yBot = 140, yBar = 152;
  int n = wx.nHours;
  if (n < 2) return;
  int tmin = 127, tmax = -128;
  for (int i = 0; i < n; i++) {
    tmin = min(tmin, (int)wx.hTemp[i]);
    tmax = max(tmax, (int)wx.hTemp[i]);
  }
  float span = max(3, tmax - tmin);
  float step = (float)(x1 - x0) / (n - 1);
  auto py = [&](int i) { return yBot - (wx.hTemp[i] - tmin) / span * (yBot - yTop); };

  for (int i = 0; i < n; i++) {
    float x = x0 + i * step;
    int bh = wx.hPop[i] * 14 / 100;  // rain chance bar
    if (bh > 0) aaRoundRect(x - 4, yBar - bh, 8, bh, 2, rgb(40, 70, 150));
  }
  for (int i = 0; i + 1 < n; i++) aaLine(x0 + i * step, py(i), x0 + (i + 1) * step, py(i + 1), 2, COL_AMBER);
  char buf[8];
  for (int i = 0; i < n; i++) {
    float x = x0 + i * step;
    aaCircle(x, py(i), 2.5f, COL_AMBER);
    if (i % 3 == 0) {
      snprintf(buf, sizeof(buf), "%d", wx.hTemp[i]);
      spr.setTextDatum(BC_DATUM);
      spr.setTextColor(COL_WHITE);
      spr.drawString(buf, x, py(i) - 4, 1);
      snprintf(buf, sizeof(buf), "%02d", (wx.h0 + i) % 24);
      spr.setTextDatum(TC_DATUM);
      spr.setTextColor(COL_GREY);
      spr.drawString(buf, x, yBar + 2, 1);
    }
  }
}

static void drawDays() {
  char buf[16];
  for (int i = 0; i < wx.nDays; i++) {
    const WxDay &d = wx.days[i];
    int cx = W / 6 + i * W / 3;
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(COL_GREY);
    spr.drawString(d.name, cx - 30, 124, 2);
    drawWxIcon(cx - 30, 150, 0.45f, d.code, true);
    snprintf(buf, sizeof(buf), "%d", d.hi);
    textDeg(buf, cx - 4, 118, 2, COL_WHITE);
    snprintf(buf, sizeof(buf), "%d", d.lo);
    textDeg(buf, cx - 4, 138, 2, COL_GREY);
  }
}

void drawWeather() {
  spr.fillSprite(BG);
  if (!wx.valid) {
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(COL_GREY);
    spr.drawString(wifiUp ? "Fetching the weather..." : "Connecting to WiFi...", W / 2, H / 2 - 10, 2);
    spr.setTextColor(COL_DIM);
    spr.drawString("from open-meteo.com", W / 2, H / 2 + 12, 2);
    return;
  }
  char buf[32];

  // Header: city left, fetch time right (amber when the data is getting old)
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(COL_GREY);
  spr.drawString(wx.city, 8, 4, 2);
  bool stale = millis() - wx.receivedAt > 45UL * 60 * 1000;
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(stale ? COL_AMBER : COL_DIM);
  spr.drawString(wx.at, W - 8, 4, 2);

  // Now: icon, big temperature, condition
  drawWxIcon(48, 58, 1.35f, wx.code, wx.day);
  snprintf(buf, sizeof(buf), "%d", (int)lroundf(wx.temp));
  textDeg(buf, 96, 28, 6, COL_WHITE);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(COL_CYAN);
  spr.drawString(wxText(wx.code), 98, 80, 2);

  // Details column
  const int dx = 214;
  spr.setTextColor(COL_GREY);
  spr.setTextDatum(TL_DATUM);
  snprintf(buf, sizeof(buf), "H %d", wx.hi);
  int x = textDeg(buf, dx, 26, 2, COL_WHITE);
  snprintf(buf, sizeof(buf), "L %d", wx.lo);
  textDeg(buf, x + 6, 26, 2, COL_GREY);
  snprintf(buf, sizeof(buf), "feels %d", (int)lroundf(wx.feels));
  textDeg(buf, dx, 44, 2, COL_GREY);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(COL_GREY);
  snprintf(buf, sizeof(buf), "rain %d%%", wx.pop);
  spr.drawString(buf, dx, 62, 2);
  snprintf(buf, sizeof(buf), "wind %d km/h", (int)lroundf(wx.wind));
  spr.drawString(buf, dx, 80, 2);

  spr.drawFastHLine(8, 104, W - 16, COL_DIM);
  if (wxShowDays) drawDays();
  else drawHours();
}

void weatherTap(int x, int y) {
  (void)x; (void)y;
  wxShowDays = !wxShowDays;
}
