// ===================== Clock, page dots, setup screen =====================

void drawPageDots() {
  if (page == PAGE_FACE && millis() - pageShownAt > 1500) return;
  int x0 = W / 2 - (PAGE_COUNT - 1) * 6;
  for (int i = 0; i < PAGE_COUNT; i++) {
    aaCircle(x0 + i * 12, H - 6, 2, i == page ? COL_WHITE : COL_DIM);
  }
}

// ---------- CLOCK ----------

void drawClock() {
  spr.fillSprite(BG);
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm tm;
  if (tv.tv_sec < 1700000000 || !localtime_r(&tv.tv_sec, &tm)) {
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(COL_GREY);
    spr.drawString(wifiUp ? "Setting the clock..." : "Connecting to WiFi...", W / 2, H / 2, 2);
    return;
  }
  char buf[40];

  // Date
  strftime(buf, sizeof(buf), "%A, %e %B", &tm);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(COL_GREY);
  spr.drawString(buf, W / 2, 8, 2);

  // HH:MM in the 75 px font, colon blinking with the seconds
  const int y = 30, colonW = spr.textWidth(":", FONT_72);
  snprintf(buf, sizeof(buf), "%02d", tm.tm_hour);
  spr.setTextColor(COL_WHITE);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(buf, W / 2 - colonW / 2, y, FONT_72);
  snprintf(buf, sizeof(buf), "%02d", tm.tm_min);
  spr.setTextDatum(TL_DATUM);
  spr.drawString(buf, W / 2 + colonW / 2, y, FONT_72);
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(tv.tv_usec < 500000 ? COL_CYAN : COL_DIM);
  spr.drawString(":", W / 2, y, FONT_72);

  // Seconds sweep
  float frac = (tm.tm_sec + tv.tv_usec / 1e6f) / 60.0f;
  const int bx0 = 60, bx1 = W - 60, by = 118;
  aaLine(bx0, by, bx1, by, 3, COL_DIM);
  aaLine(bx0, by, bx0 + (bx1 - bx0) * frac, by, 3, COL_CYAN);

  // Weather summary
  if (wx.valid) {
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(wx.temp));
    const char *cond = wxText(wx.code);
    int tw = spr.textWidth(buf, 4) + 12 + 8 + spr.textWidth(cond, 2);
    int x = W / 2 - (tw + 30) / 2;
    drawWxIcon(x + 12, 143, 0.5f, wx.code, wx.day);
    int xe = textDeg(buf, x + 30, 132, 4, COL_WHITE);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(COL_GREY);
    spr.drawString(cond, xe + 6, 137, 2);
  }
}

// ---------- WiFi setup ----------

void drawSetupScreen() {
  spr.fillSprite(BG);
  // A small sleepy pair of eyes so it still feels like the buddy.
  aaRoundRect(W / 2 - 50, 22, 34, 12, 6, COL_BLUE);
  aaRoundRect(W / 2 + 16, 22, 34, 12, 6, COL_BLUE);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(COL_CYAN);
  spr.drawString("I need WiFi!", W / 2, 60, 4);
  spr.setTextColor(COL_WHITE);
  spr.drawString("1. Join network  \"Buddy-Setup\"", W / 2, 96, 2);
  spr.drawString("2. Open  192.168.4.1", W / 2, 118, 2);
  spr.setTextColor(COL_GREY);
  spr.drawString("pick your network and save", W / 2, 142, 2);
}
