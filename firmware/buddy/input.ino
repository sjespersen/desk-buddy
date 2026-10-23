// ===================== Touch gestures + side buttons =====================

bool tDown = false;
int32_t tx0, ty0, tx, ty, lastX, lastY;
uint32_t tStart = 0, lastTouchSeen = 0;
float petAccum = 0;
uint32_t touchSamples = 0;

Button btnA{BTN_A_PIN, HIGH, false, 0, false};
Button btnB{BTN_B_PIN, HIGH, false, 0, false};
bool buttonsInit = false;

void handleTap(int x, int y) {
  switch (page) {
    case PAGE_FACE: faceBoop(x, y); break;
    case PAGE_WEATHER: weatherTap(x, y); break;
    default: break;
  }
}

// Returns +1 on a short press, -1 when a long press (4 s) fires, 0 otherwise.
int pollButton(Button &b) {
  uint32_t now = millis();
  bool down = digitalRead(b.pin) != b.idleLevel;
  int result = 0;
  if (down != b.pressed && now - b.changedAt > 30) {
    b.pressed = down;
    b.changedAt = now;
    if (!down && !b.longFired) result = 1;
    b.longFired = false;
  }
  if (b.pressed && !b.longFired && now - b.changedAt > 4000) {
    b.longFired = true;
    result = -1;
  }
  return result;
}

void inputUpdate() {
  uint32_t now = millis();

  if (!buttonsInit) {
    // Record the idle level so either button polarity works.
    btnA.idleLevel = digitalRead(BTN_A_PIN);
    btnB.idleLevel = digitalRead(BTN_B_PIN);
    Serial.printf("[BTN] idle levels A=%d B=%d\n", btnA.idleLevel, btnB.idleLevel);
    buttonsInit = true;
  }
  int a = pollButton(btnA), b = pollButton(btnB);
  if (a == 1) { noteInteraction(); setPage(page + 1); }
  if (b == 1) { noteInteraction(); setPage(page - 1); }
  if (b == -1) {
    Serial.println("[BTN] long press B -> forget WiFi");
    prefs.remove("ssid");
    prefs.remove("pass");
    delay(100);
    ESP.restart();
  }

  if (setupMode) return;

  int32_t x = 0, y = 0;
  bool t = display.getTouch(&x, &y);
  if (t) {
    lastTouchSeen = now;
    noteInteraction();
    if (!tDown) {
      tDown = true;
      tx0 = lastX = x;
      ty0 = lastY = y;
      tStart = now;
      petAccum = 0;
      touchSamples = 0;
    }
    touchSamples++;
    tx = x;
    ty = y;
    if (page == PAGE_FACE) {
      if (now - tStart > 120) faceLookAt(x, y);
      if (now - tStart > 350) {
        petAccum += hypotf(x - lastX, y - lastY);
        if (petAccum > 90) { facePet(); petAccum = 0; }
      }
    }
    lastX = x;
    lastY = y;
  } else if (tDown && now - lastTouchSeen > 70) {
    tDown = false;
    faceRelease();
    int dx = tx - tx0, dy = ty - ty0;
    uint32_t dur = lastTouchSeen - tStart;
    if (perfLog) Serial.printf("[TOUCH] dx=%d dy=%d dur=%lu samples=%lu\n", dx, dy, (unsigned long)dur, (unsigned long)touchSamples);
    if (dur < 900 && abs(dx) > 45 && abs(dx) > abs(dy) * 1.2f) {
      setPage(page + (dx < 0 ? 1 : -1));
    } else if (abs(dx) < 20 && abs(dy) < 20 && dur < 600) {
      handleTap(tx0, ty0);
    }
  }
}
