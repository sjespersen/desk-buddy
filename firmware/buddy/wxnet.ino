// ===================== Weather fetch (Open-Meteo) =====================
// A background task on core 0 fetches the forecast over HTTPS every 15 minutes, so
// the slow TLS handshake never stalls the UI. The location is the pinned one (LOC
// serial command / POST /api/location) or else an IP lookup at ipinfo.io. The task
// fills wxPending and raises wxReady; the main loop copies it into wx and applies the
// location's UTC offset to the clock.

// Mozilla CA bundle that ships inside the core's mbedtls library
extern const uint8_t crtBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t crtBundleEnd[] asm("_binary_x509_crt_bundle_end");

static constexpr uint32_t WX_REFRESH_MS = 15UL * 60 * 1000;
static constexpr uint32_t WX_RETRY_MS = 2UL * 60 * 1000;

Location pinnedLoc = {};             // from prefs; written only by the main task
volatile bool wxRefetch = false;     // set when the location changes
std::atomic<bool> wxReady{false};
Weather wxPending;
int32_t tzOffsetApplied = INT32_MIN;

bool parseLocation(const String &s, Location &out) {
  // "lat,lon" or "lat,lon,Name"
  int c1 = s.indexOf(','), c2 = c1 < 0 ? -1 : s.indexOf(',', c1 + 1);
  if (c1 < 0) return false;
  out.lat = s.substring(0, c1).toFloat();
  out.lon = s.substring(c1 + 1, c2 < 0 ? s.length() : c2).toFloat();
  strlcpy(out.name, c2 < 0 ? "" : s.substring(c2 + 1).c_str(), sizeof(out.name));
  out.valid = fabsf(out.lat) <= 90 && fabsf(out.lon) <= 180 && (out.lat != 0 || out.lon != 0);
  return out.valid;
}

// Empty string clears the pin (back to IP lookup).
bool setLocation(const String &s) {
  Location l = {};
  if (s.length() && !parseLocation(s, l)) return false;
  pinnedLoc = l;
  if (s.length()) prefs.putString("loc", s);
  else prefs.remove("loc");
  wxRefetch = true;
  Serial.printf("[WX] location %s\n", l.valid ? s.c_str() : "from IP lookup");
  return true;
}

static bool httpsGetJson(NetworkClientSecure &client, const String &url, JsonDocument &doc) {
  HTTPClient http;
  http.setTimeout(10000);
  http.setUserAgent("desk-buddy/1.0");
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  bool ok = code == 200 && !deserializeJson(doc, http.getString());
  if (!ok) Serial.printf("[WX] GET %s failed (%d)\n", url.c_str(), code);
  http.end();
  return ok;
}

// Day of week (0 = Sunday) for an ISO date "YYYY-MM-DD"
static int weekday(const char *iso) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = atoi(iso), m = atoi(iso + 5), d = atoi(iso + 8);
  if (m < 1 || m > 12) return 0;
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static bool wxFetch(NetworkClientSecure &client, Location &ipLoc) {
  Location loc = pinnedLoc;
  if (!loc.valid) {
    if (!ipLoc.valid) {
      JsonDocument info;
      if (!httpsGetJson(client, "https://ipinfo.io/json", info)) return false;
      String s = String(info["loc"] | "") + "," + (info["city"] | "");
      if (!parseLocation(s, ipLoc)) return false;
    }
    loc = ipLoc;
  }

  char url[420];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f&timezone=auto"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,wind_speed_10m"
           "&hourly=temperature_2m,precipitation_probability"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&forecast_hours=%d&forecast_days=%d",
           loc.lat, loc.lon, WX_HOURS, WX_DAYS + 1);
  JsonDocument doc;
  if (!httpsGetJson(client, url, doc)) return false;
  JsonObjectConst cur = doc["current"], hourly = doc["hourly"], daily = doc["daily"];
  if (cur.isNull() || hourly.isNull() || daily.isNull()) return false;

  Weather &n = wxPending;
  n = Weather();
  strlcpy(n.city, loc.name, sizeof(n.city));
  n.utcOffset = doc["utc_offset_seconds"] | 0;
  time_t local = time(nullptr) + n.utcOffset;  // fetch time in the location's clock
  struct tm tm;
  gmtime_r(&local, &tm);
  strftime(n.at, sizeof(n.at), "%H:%M", &tm);
  n.temp = cur["temperature_2m"] | 0.0f;
  n.feels = cur["apparent_temperature"] | 0.0f;
  n.wind = cur["wind_speed_10m"] | 0.0f;
  n.hum = cur["relative_humidity_2m"] | 0;
  n.code = cur["weather_code"] | 0;
  n.day = (cur["is_day"] | 1) != 0;
  n.hi = lroundf(daily["temperature_2m_max"][0] | 0.0f);
  n.lo = lroundf(daily["temperature_2m_min"][0] | 0.0f);
  n.pop = daily["precipitation_probability_max"][0] | 0;
  const char *h0 = hourly["time"][0] | "0000-00-00T00:00";
  n.h0 = strlen(h0) >= 13 ? atoi(h0 + 11) : 0;
  JsonArrayConst ht = hourly["temperature_2m"], hp = hourly["precipitation_probability"];
  n.nHours = min((int)ht.size(), WX_HOURS);
  for (int i = 0; i < n.nHours; i++) {
    n.hTemp[i] = lroundf(ht[i] | 0.0f);
    n.hPop[i] = hp[i] | 0;
  }
  static const char *DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  JsonArrayConst dt = daily["time"];
  n.nDays = min((int)dt.size() - 1, WX_DAYS);
  for (int i = 0; i < n.nDays; i++) {
    WxDay &d = n.days[i];
    strlcpy(d.name, DAY_NAMES[weekday(dt[i + 1] | "")], sizeof(d.name));
    d.code = daily["weather_code"][i + 1] | 0;
    d.hi = lroundf(daily["temperature_2m_max"][i + 1] | 0.0f);
    d.lo = lroundf(daily["temperature_2m_min"][i + 1] | 0.0f);
  }
  n.valid = true;
  Serial.printf("[WX] %s %.1fC code %d\n", n.city, n.temp, n.code);
  return true;
}

static void wxTask(void *) {
  NetworkClientSecure client;
  client.setCACertBundle(crtBundleStart, crtBundleEnd - crtBundleStart);
  Location ipLoc = {};
  uint32_t nextAt = 0;
  for (;;) {
    bool due = (int32_t)(millis() - nextAt) >= 0;
    if (wifiUp && time(nullptr) > 1700000000 && !wxReady && (due || wxRefetch)) {
      wxRefetch = false;
      bool ok = wxFetch(client, ipLoc);
      if (ok) wxReady = true;
      nextAt = millis() + (ok ? WX_REFRESH_MS : WX_RETRY_MS);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void wxBegin() {
  String s = prefs.getString("loc", "");
  if (s.length()) parseLocation(s, pinnedLoc);
  // Last known UTC offset until the first forecast arrives (else the TZ_INFO default)
  if (prefs.isKey("tzoff")) applyUtcOffset(prefs.getInt("tzoff", 0));
  xTaskCreatePinnedToCore(wxTask, "weather", 12288, nullptr, 1, nullptr, 0);
}

void applyUtcOffset(int32_t off) {
  if (off == tzOffsetApplied) return;
  tzOffsetApplied = off;
  // POSIX TZ offsets are west-positive, hence the flipped sign
  char tz[24];
  int a = abs(off);
  snprintf(tz, sizeof(tz), "LOC%c%d:%02d", off >= 0 ? '-' : '+', a / 3600, a % 3600 / 60);
  setenv("TZ", tz, 1);
  tzset();
}

// Main loop: take over a finished fetch
void wxLoop() {
  if (!wxReady) return;
  wx = wxPending;
  wx.receivedAt = millis();
  wxReady = false;
  applyUtcOffset(wx.utcOffset);
  if (prefs.getInt("tzoff", INT32_MIN) != wx.utcOffset) prefs.putInt("tzoff", wx.utcOffset);
}
