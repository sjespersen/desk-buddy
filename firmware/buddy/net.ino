// ===================== WiFi, setup portal, HTTP API =====================

bool servicesUp = false;
bool everConnected = false;
uint32_t wifiStartedAt = 0;
String scannedOptions;

void netBegin() {
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  WiFi.setHostname(HOSTNAME);
  if (ssid.isEmpty()) {
    startSetupPortal();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // snappier HTTP replies
  WiFi.begin(ssid.c_str(), pass.c_str());
  wifiStartedAt = millis();
  Serial.printf("[WIFI] connecting to '%s'\n", ssid.c_str());
}

void netLoop() {
  if (setupMode) {
    dns.processNextRequest();
    server.handleClient();
    return;
  }
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !wifiUp) onWifiUp();
  if (!up && wifiUp) Serial.println("[WIFI] lost connection, retrying");
  wifiUp = up;
  if (!up && !everConnected && millis() - wifiStartedAt > 30000) {
    Serial.println("[WIFI] could not connect, starting setup portal");
    startSetupPortal();
    return;
  }
  if (servicesUp) server.handleClient();
}

void onWifiUp() {
  Serial.printf("[WIFI] connected, IP %s, http://%s.local\n",
                WiFi.localIP().toString().c_str(), HOSTNAME);
  everConnected = true;
  // Keep the location's offset if the weather already set one
  char tz[32];
  strlcpy(tz, tzOffsetApplied == INT32_MIN ? TZ_INFO : getenv("TZ"), sizeof(tz));
  configTzTime(tz, "pool.ntp.org", "time.google.com");
  if (servicesUp) return;
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
  server.on("/", HTTP_GET, [] {
    server.send(200, "text/plain", "Hi, I'm your desk buddy. GET /api/state for debug info, POST /api/location to pin the weather location, POST /api/wake to wake the Mac.\n");
  });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/location", HTTP_POST, handleLocation);
  // curl -X POST http://buddy.local/api/wake  — wake the Mac the buddy is plugged into
  server.on("/api/wake", HTTP_POST, [] { server.send(200, "text/plain", String(wakeMac()) + "\n"); });
  // POST on=0|1 — the Mac's display state, sent by tools/mac-display
  server.on("/api/display", HTTP_POST, [] {
    if (!server.hasArg("on")) {
      server.send(400, "text/plain", "expected on=0|1\n");
      return;
    }
    setMacDisplay(server.arg("on") != "0");
    server.send(200, "text/plain", "ok\n");
  });
  server.begin();
  servicesUp = true;
}

// POST set=lat,lon[,Name] to pin the weather location, or set= (empty) to go back
// to the IP lookup:  curl -d set=52.52,13.40,Berlin http://buddy.local/api/location
void handleLocation() {
  if (!server.hasArg("set")) {
    server.send(400, "text/plain", "expected set=lat,lon[,Name]\n");
    return;
  }
  String body = server.arg("set");
  body.trim();
  if (!setLocation(body)) {
    server.send(400, "text/plain", "expected set=lat,lon[,Name]\n");
    return;
  }
  server.send(200, "text/plain", "ok\n");
}

void handleState() {
  JsonDocument out;
  out["ip"] = WiFi.localIP().toString();
  out["rssi"] = WiFi.RSSI();
  out["page"] = (int)page;
  out["expr"] = (int)expr;
  out["heap"] = ESP.getFreeHeap();
  out["uptime_s"] = millis() / 1000;
  out["fps"] = roundf(fps * 10) / 10;
  out["weather"] = wx.valid ? wx.city : "";
  out["temp"] = wx.valid ? wx.temp : NAN;
  out["location"] = pinnedLoc.valid ? "pinned" : "ip";
  out["usb"] = usbStateName();
  out["mac_display"] = macDisplayOff() ? "off" : "on";
  String body;
  serializeJson(out, body);
  server.send(200, "application/json", body);
}

// ---- captive setup portal ----

static String htmlEscape(const String &s) {
  String o;
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

void startSetupPortal() {
  setupMode = true;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks();
  scannedOptions = "";
  for (int i = 0; i < n; i++) {
    scannedOptions += "<option value=\"" + htmlEscape(WiFi.SSID(i)) + "\">";
  }
  WiFi.softAP(SETUP_AP);
  dns.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, [] {
    String page =
        "<!doctype html><meta name=viewport content='width=device-width'>"
        "<style>body{font-family:system-ui;background:#111;color:#eee;padding:24px;max-width:420px;margin:auto}"
        "input,button{width:100%;padding:12px;margin:6px 0 14px;font-size:16px;border-radius:10px;border:1px solid #444;background:#222;color:#eee}"
        "button{background:#40e0ff;color:#000;font-weight:600;border:0}</style>"
        "<h2>&#128064; Desk Buddy WiFi</h2><form method=post action=/save>"
        "<label>Network</label><input name=ssid list=nets autocomplete=off required>"
        "<datalist id=nets>" + scannedOptions + "</datalist>"
        "<label>Password</label><input name=pass type=password>"
        "<button>Save &amp; connect</button></form>";
    server.send(200, "text/html", page);
  });
  server.on("/save", HTTP_POST, [] {
    prefs.putString("ssid", server.arg("ssid"));
    prefs.putString("pass", server.arg("pass"));
    server.send(200, "text/html", "<h2>Saved! Buddy is rebooting&hellip;</h2>");
    delay(500);
    ESP.restart();
  });
  server.onNotFound([] {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });
  server.begin();
  Serial.printf("[SETUP] AP '%s' up at %s\n", SETUP_AP, WiFi.softAPIP().toString().c_str());
}
