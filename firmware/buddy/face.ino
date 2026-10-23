// ===================== Buddy face: expressions, eyes, particles =====================

EyeParams eyeCur, eyeTgt;
Expr expr = EX_NEUTRAL;
Expr baseExprPrev = EX_NEUTRAL;
Expr reactExpr = EX_NEUTRAL;
uint32_t reactUntil = 0;
Expr queuedReact = EX_NEUTRAL;
uint32_t queuedReactMs = 0;
uint32_t dizzyUntil = 0;
float colR = 64, colG = 224, colB = 255;

// Blinking
uint32_t nextBlinkAt = 0, blinkStart = 0;
bool blinking = false, doubleBlink = false;
float blinkF = 1.0f;

// Looking around
float sacX = 0, sacY = 0;
uint32_t nextSaccadeAt = 0;
bool lookingAtTouch = false;
float touchLX = 0, touchLY = 0;
bool tiltActive = false;
float tiltLX = 0, tiltLY = 0;

// Poking an eye closes it for a moment
int pokedEye = -1;  // 0 = left, 1 = right
uint32_t pokeUntil = 0;

// Rapid boops make it grumpy
uint32_t boopTimes[6] = {0};
int boopIdx = 0;

float bob = 0;
uint32_t lastPetAt = 0;
uint32_t lastSpawn = 0;

static constexpr int MAX_PARTICLES = 28;
Particle parts[MAX_PARTICLES];

static constexpr float EYE_GAP = 68;  // centre-to-face-centre distance
static constexpr float FACE_CY = 80;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float frand(float lo, float hi) { return lo + (hi - lo) * (random(10000) / 10000.0f); }

uint16_t blend565(uint16_t a, uint16_t b, float t) {  // t=0 -> a, t=1 -> b
  t = clampf(t, 0, 1);
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return ((int)(ar + (br - ar) * t) << 11) | ((int)(ag + (bg - ag) * t) << 5) |
         (int)(ab + (bb - ab) * t);
}

// ---- fast anti-aliased shapes, drawn straight into the sprite buffer ----
// (the library's smooth primitives build a renderer per call: ~41 ms per eye)

static inline void plotAA(uint16_t *fb, int px, int py, uint16_t col, float a) {
  if (a <= 0) return;
  uint16_t &d = fb[py * W + px];
  d = a >= 1 ? col : blend565(col, d, 1 - a);
}

void aaRoundRect(float x, float y, float w, float h, float r, uint16_t col) {
  uint16_t *fb = (uint16_t *)spr.frameBuffer(0);
  r = fminf(r, fminf(w, h) / 2);
  int x0 = max(0, (int)floorf(x)), x1 = min(W, (int)ceilf(x + w));
  int y0 = max(0, (int)floorf(y)), y1 = min(H, (int)ceilf(y + h));
  float ix0 = x + r, ix1 = x + w - r, iy0 = y + r, iy1 = y + h - r;  // inner rect
  auto cover = [&](int px, float dy) {
    float cx = px + 0.5f;
    float dx = fmaxf(fmaxf(ix0 - cx, cx - ix1), 0);
    float d = (dx > 0 && dy > 0) ? sqrtf(dx * dx + dy * dy) : fmaxf(dx, dy);
    return r - d + 0.5f;
  };
  for (int py = y0; py < y1; py++) {
    float cy = py + 0.5f;
    float dy = fmaxf(fmaxf(iy0 - cy, cy - iy1), 0);
    // Anti-alias only the edge pixels; the solid middle of the row is a plain fill.
    int l = x0, rgt = x1 - 1;
    for (; l <= rgt; l++) {
      float a = cover(l, dy);
      if (a >= 1 && l + 0.5f >= ix0) break;
      plotAA(fb, l, py, col, a);
    }
    for (; rgt >= l; rgt--) {
      float a = cover(rgt, dy);
      if (a >= 1 && rgt + 0.5f <= ix1) break;
      plotAA(fb, rgt, py, col, a);
    }
    for (uint16_t *p = fb + py * W + l, *e = fb + py * W + rgt; p <= e; p++) *p = col;
  }
}

// Thick line with round caps (r = half the width); a zero-length line is a dot.
void aaLine(float x0, float y0, float x1, float y1, float width, uint16_t col) {
  uint16_t *fb = (uint16_t *)spr.frameBuffer(0);
  float r = width / 2;
  int bx0 = max(0, (int)floorf(fminf(x0, x1) - r - 1)), bx1 = min(W, (int)ceilf(fmaxf(x0, x1) + r + 1));
  int by0 = max(0, (int)floorf(fminf(y0, y1) - r - 1)), by1 = min(H, (int)ceilf(fmaxf(y0, y1) + r + 1));
  float vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
  for (int py = by0; py < by1; py++) {
    for (int px = bx0; px < bx1; px++) {
      float wx = px + 0.5f - x0, wy = py + 0.5f - y0;
      float t = len2 > 0 ? clampf((wx * vx + wy * vy) / len2, 0, 1) : 0;
      float ex = wx - t * vx, ey = wy - t * vy;
      plotAA(fb, px, py, col, r - sqrtf(ex * ex + ey * ey) + 0.5f);
    }
  }
}

void aaCircle(float cx, float cy, float r, uint16_t col) { aaLine(cx, cy, cx, cy, 2 * r, col); }

// Ring segment between radii ir..r. Angles in degrees, 0 = 6 o'clock, clockwise
// (same convention as drawSmoothArc).
void aaArc(float cx, float cy, float r, float ir, float a0, float a1, uint16_t col, bool roundEnds) {
  uint16_t *fb = (uint16_t *)spr.frameBuffer(0);
  bool full = a1 - a0 >= 360;
  int x0 = max(0, (int)floorf(cx - r - 1)), x1 = min(W, (int)ceilf(cx + r + 1));
  int y0 = max(0, (int)floorf(cy - r - 1)), y1 = min(H, (int)ceilf(cy + r + 1));
  for (int py = y0; py < y1; py++) {
    for (int px = x0; px < x1; px++) {
      float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
      float d = sqrtf(dx * dx + dy * dy);
      float a = fminf(r - d + 0.5f, d - ir + 0.5f);
      if (a <= 0) continue;
      if (!full) {
        float ang = atan2f(-dx, dy) * (180.0f / PI);
        if (ang < 0) ang += 360;
        if (ang < a0 || ang > a1) continue;
      }
      plotAA(fb, px, py, col, a);
    }
  }
  if (roundEnds && !full) {
    float rm = (r + ir) / 2, cap = (r - ir) / 2;
    for (float ang : {a0, a1}) {
      float t = ang * (PI / 180.0f);
      aaCircle(cx - sinf(t) * rm, cy + cosf(t) * rm, cap, col);
    }
  }
}

EyeParams paramsFor(Expr e) {
  EyeParams p{0, 0, 62, 78, 1, 0, 0, 7};
  switch (e) {
    case EX_HAPPY:     p.happy = 1; p.mouth = 14; p.h = 74; break;
    case EX_LOVE:      p.happy = 1; p.mouth = 16; p.w = 66; p.h = 72; break;
    case EX_SURPRISED: p.w = 70; p.h = 92; p.mouth = 0; break;
    case EX_ANGRY:     p.slant = 0.9f; p.h = 66; p.mouth = -8; break;
    case EX_SAD:       p.slant = -0.8f; p.h = 70; p.ly = 0.35f; p.mouth = -10; break;
    case EX_SLEEPY:    p.open = 0.38f; p.ly = 0.3f; p.mouth = 3; break;
    case EX_ASLEEP:    p.open = 0.06f; p.ly = 0.25f; p.mouth = 0; break;
    case EX_DIZZY:     p.mouth = -4; break;
    default: break;
  }
  return p;
}

uint16_t colorFor(Expr e) {
  switch (e) {
    case EX_LOVE:     return COL_PINK;
    case EX_ANGRY:    return COL_RED;
    case EX_SAD:      return COL_BLUE;
    case EX_SLEEPY:
    case EX_ASLEEP:   return COL_BLUE;
    case EX_DIZZY:    return COL_PURPLE;
    default:          return COL_CYAN;
  }
}

Expr baseExpr() {
  uint32_t now = millis();
  if (!wifiUp) return now > 20000 ? EX_SAD : EX_NEUTRAL;
  struct tm tm;
  if (!getLocalTime(&tm, 0)) return EX_NEUTRAL;
  int h = tm.tm_hour;
  if (h >= SLEEP_HOUR || h < WAKE_HOUR) return EX_ASLEEP;
  if (h >= SLEEPY_HOUR) return EX_SLEEPY;
  return EX_NEUTRAL;
}

void faceReact(Expr e, uint32_t ms) {
  reactExpr = e;
  reactUntil = millis() + ms;
  queuedReactMs = 0;
}

void faceReactThen(Expr first, uint32_t ms1, Expr then, uint32_t ms2) {
  faceReact(first, ms1);
  queuedReact = then;
  queuedReactMs = ms2;
}

void spawn(ParticleKind k, float x, float y, float vx, float vy, float life, float size) {
  for (auto &p : parts) {
    if (p.kind == P_NONE) {
      p = {k, x, y, vx, vy, life, life, size};
      return;
    }
  }
}

void faceInit() {
  eyeCur = eyeTgt = paramsFor(EX_NEUTRAL);
  nextBlinkAt = millis() + 1500;
  for (auto &p : parts) p.kind = P_NONE;
  faceReactThen(EX_SURPRISED, 500, EX_HAPPY, 1500);  // hello!
}

bool faceIsAsleep() { return expr == EX_ASLEEP; }

void faceCelebrate() {
  faceReact(EX_LOVE, 4000);
  for (int i = 0; i < 14; i++) {
    float a = frand(0, 6.283f), s = frand(40, 110);
    spawn(i % 2 ? P_STAR : P_HEART, W / 2, FACE_CY, cosf(a) * s, sinf(a) * s - 30, frand(1.2f, 2.0f), frand(6, 10));
  }
}

// ---- input hooks (called from input.ino / imu.ino) ----

void faceLookAt(int x, int y) {
  lookingAtTouch = true;
  touchLX = clampf((x - W / 2) / (W / 2.0f), -1, 1);
  touchLY = clampf((y - FACE_CY) / 70.0f, -1, 1);
}

void faceRelease() { lookingAtTouch = false; }

void faceBoop(int x, int y) {
  uint32_t now = millis();
  boopTimes[boopIdx] = now;
  boopIdx = (boopIdx + 1) % 6;
  int recent = 0;
  for (uint32_t t : boopTimes) if (t && now - t < 2500) recent++;

  if (recent >= 5) {
    faceReact(EX_ANGRY, 2500);
    spawn(P_SWEAT, W / 2 + 104, 36, 0, 30, 1.0f, 7);
    return;
  }

  // Poked right in the eye?
  for (int i = 0; i < 2; i++) {
    float ex = W / 2 + (i ? EYE_GAP : -EYE_GAP);
    if (fabsf(x - ex) < 38 && fabsf(y - FACE_CY) < 48) {
      pokedEye = i;
      pokeUntil = now + 700;
      faceReactThen(EX_ANGRY, 700, EX_NEUTRAL, 1);
      return;
    }
  }
  faceReactThen(EX_SURPRISED, 420, EX_HAPPY, 1300);
  spawn(P_STAR, x, y, frand(-20, 20), -40, 0.7f, 6);
}

void facePet() {
  uint32_t now = millis();
  lastPetAt = now;
  if (reactExpr != EX_LOVE || now > reactUntil) faceReact(EX_LOVE, 2200);
  else reactUntil = now + 2200;
}

void faceShake() {
  uint32_t now = millis();
  if (now < dizzyUntil) { dizzyUntil = now + 2500; return; }
  dizzyUntil = now + 3000;
  for (int i = 0; i < 6; i++) spawn(P_STAR, frand(60, 260), frand(20, 60), frand(-30, 30), frand(-30, 10), 1.2f, 6);
  noteInteraction();
}

void faceTilt(bool active, float lx, float ly) {
  tiltActive = active;
  tiltLX = lx;
  tiltLY = ly;
}

// ---- update ----

void faceUpdate(float dt) {
  uint32_t now = millis();

  Expr base = baseExpr();
  if (baseExprPrev == EX_ASLEEP && base != EX_ASLEEP && now > 5000) {
    // Welcome back!
    faceReactThen(EX_SURPRISED, 500, EX_HAPPY, 2200);
    for (int i = 0; i < 6; i++) spawn(P_STAR, frand(60, 260), frand(30, 70), frand(-20, 20), -30, 1.4f, 6);
  }
  baseExprPrev = base;

  if (now >= reactUntil && queuedReactMs) {
    faceReact(queuedReact, queuedReactMs);
  }
  if (now < dizzyUntil) expr = EX_DIZZY;
  else if (now < reactUntil) expr = reactExpr;
  else expr = base;

  eyeTgt = paramsFor(expr);

  // Where to look
  float lx, ly;
  if (lookingAtTouch) {
    lx = touchLX; ly = touchLY;
  } else if (tiltActive) {
    lx = tiltLX; ly = tiltLY;
  } else {
    if (now >= nextSaccadeAt) {
      if (random(100) < 35) { sacX = 0; sacY = 0; }
      else { sacX = frand(-0.85f, 0.85f); sacY = frand(-0.5f, 0.5f); }
      nextSaccadeAt = now + random(900, 3800);
    }
    lx = sacX; ly = sacY;
  }
  if (expr == EX_ASLEEP || expr == EX_DIZZY) { lx = 0; ly = 0; }
  eyeTgt.lx = clampf(eyeTgt.lx + lx, -1, 1);
  eyeTgt.ly = clampf(eyeTgt.ly + ly, -1, 1);

  // Blinking
  if (expr != EX_ASLEEP && !blinking && now >= nextBlinkAt) { blinking = true; blinkStart = now; }
  blinkF = 1.0f;
  if (blinking) {
    float ph = (now - blinkStart) / 150.0f;
    if (ph >= 1.0f) {
      blinking = false;
      if (doubleBlink) { doubleBlink = false; nextBlinkAt = now + 110; }
      else {
        nextBlinkAt = now + random(1800, 5500);
        doubleBlink = random(100) < 20;
      }
    } else {
      blinkF = ph < 0.5f ? 1 - ph * 2 : (ph - 0.5f) * 2;
    }
  }

  // Smooth towards the target
  float kLook = 1 - expf(-dt * 14), kShape = 1 - expf(-dt * 9);
  eyeCur.lx += (eyeTgt.lx - eyeCur.lx) * kLook;
  eyeCur.ly += (eyeTgt.ly - eyeCur.ly) * kLook;
  eyeCur.w += (eyeTgt.w - eyeCur.w) * kShape;
  eyeCur.h += (eyeTgt.h - eyeCur.h) * kShape;
  eyeCur.open += (eyeTgt.open - eyeCur.open) * kShape;
  eyeCur.slant += (eyeTgt.slant - eyeCur.slant) * kShape;
  eyeCur.happy += (eyeTgt.happy - eyeCur.happy) * kShape;
  eyeCur.mouth += (eyeTgt.mouth - eyeCur.mouth) * kShape;

  uint16_t tc = colorFor(expr);
  float kc = 1 - expf(-dt * 6);
  colR += (((tc >> 11) & 31) * 8.22f - colR) * kc;
  colG += (((tc >> 5) & 63) * 4.05f - colG) * kc;
  colB += ((tc & 31) * 8.22f - colB) * kc;

  // Breathing while asleep
  float bobTgt = 0;
  if (expr == EX_ASLEEP) bobTgt = sinf(now * 0.0018f) * 3;
  bob += (bobTgt - bob) * (1 - expf(-dt * 10));

  // Particle spawning
  uint32_t interval = 0;
  switch (expr) {
    case EX_LOVE: interval = 280; break;
    case EX_ASLEEP: interval = 1100; break;
    default: break;
  }
  if (interval && now - lastSpawn > interval) {
    lastSpawn = now;
    switch (expr) {
      case EX_LOVE: spawn(P_HEART, W / 2 + frand(-110, 110), frand(40, 90), frand(-12, 12), frand(-50, -30), 1.6f, frand(5, 9)); break;
      case EX_ASLEEP: spawn(P_ZZZ, W / 2 + 96, 52, 12, -14, 2.6f, 1); break;
      default: break;
    }
  }
  for (auto &p : parts) {
    if (p.kind == P_NONE) continue;
    p.life -= dt;
    if (p.life <= 0) { p.kind = P_NONE; continue; }
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    if (p.kind == P_ZZZ) p.size += dt * 0.5f;
    if (p.kind == P_STAR) p.vy += 40 * dt;
  }
}

// ---- drawing ----

void drawEye(float cx, float cy, float w, float h, float open, float slant, float happy,
             bool left, uint16_t col) {
  float hh = fmaxf(5, h * open);
  float x = cx - w / 2, y = cy - hh / 2;
  float r = fminf(fminf(w, hh) / 2, 22);
  aaRoundRect(x, y, w, hh, r, col);

  if (open < 0.15f) return;
  if (fabsf(slant) > 0.02f) {
    float drop = fabsf(slant) * hh * 0.5f;
    float xin = left ? x + w + 3 : x - 3;  // corner nearest the nose
    float xout = left ? x - 3 : x + w + 3;
    float top = y - 3;
    if (slant > 0) spr.fillTriangle(xin, top, xin, top + drop, xout, top, BG);
    else spr.fillTriangle(xout, top, xout, top + drop, xin, top, BG);
  }
  if (happy > 0.02f) {
    float ry = hh * 0.62f;
    float ecy = y + hh * (1.64f - 0.67f * happy);
    spr.fillEllipse((int)cx, (int)ecy, (int)(w * 0.72f), (int)ry, BG);
  }
}

void drawSpiral(float cx, float cy, uint16_t col, float phase) {
  float px = cx, py = cy;
  for (float t = 0; t < 14; t += 0.35f) {
    float r = 2 + t * 2.4f;
    float x = cx + cosf(t + phase) * r, y = cy + sinf(t + phase) * r;
    aaLine(px, py, x, y, 4, col);
    px = x; py = y;
  }
}

void drawCurve(float cx, float cy, float hw, float bend, float thick, uint16_t col) {
  float px = -hw, py = cy - bend / 2;
  for (int i = 1; i <= 12; i++) {
    float x = -hw + 2 * hw * i / 12.0f;
    float n = x / hw;
    float y = cy - bend / 2 + bend * (1 - n * n);
    aaLine(cx + px, py, cx + x, y, thick, col);
    px = x; py = y;
  }
}

void drawMouth(float cx, float cy, uint16_t col) {
  uint32_t now = millis();
  switch (expr) {
    case EX_SURPRISED:
      aaCircle(cx, cy, 8, col);
      aaCircle(cx, cy, 4, BG);
      break;
    case EX_ASLEEP:
      aaCircle(cx, cy, 3 + 1.5f * sinf(now * 0.0018f), col);
      break;
    case EX_DIZZY: {
      float px = cx - 16, py = cy;
      for (int x = -14; x <= 16; x += 2) {
        float y = cy + sinf(x * 0.45f + now * 0.012f) * 3;
        aaLine(px, py, cx + x, y, 3, col);
        px = cx + x; py = y;
      }
      break;
    }
    default:
      drawCurve(cx, cy, 13 + fabsf(eyeCur.mouth) * 0.35f, eyeCur.mouth, 3.5f, col);
  }
}

void drawHeart(float x, float y, float s, uint16_t col) {
  spr.fillCircle(x - s / 2, y, s / 2 + 1, col);
  spr.fillCircle(x + s / 2, y, s / 2 + 1, col);
  spr.fillTriangle(x - s - 1, y + 1, x + s + 1, y + 1, x, y + s * 1.4f, col);
}

void drawParticles() {
  for (auto &p : parts) {
    if (p.kind == P_NONE) continue;
    float fade = 1 - p.life / p.maxLife;  // 0 new -> 1 dead
    fade = fade < 0.6f ? 0 : (fade - 0.6f) / 0.4f;
    switch (p.kind) {
      case P_HEART: drawHeart(p.x, p.y, p.size, blend565(COL_PINK, BG, fade)); break;
      case P_ZZZ: {
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(blend565(COL_BLUE, BG, fade));
        spr.drawString("z", p.x, p.y, p.size < 1.8f ? 2 : 4);
        break;
      }
      case P_SWEAT: {
        uint16_t c = blend565(rgb(120, 200, 255), BG, fade);
        spr.fillCircle(p.x, p.y, 4, c);
        spr.fillTriangle(p.x - 4, p.y - 1, p.x + 4, p.y - 1, p.x, p.y - 9, c);
        break;
      }
      case P_STAR: {
        uint16_t c = blend565(COL_AMBER, BG, fade);
        aaLine(p.x - p.size, p.y, p.x + p.size, p.y, 2, c);
        aaLine(p.x, p.y - p.size, p.x, p.y + p.size, 2, c);
        break;
      }
      default: break;
    }
  }
}

void drawStatusBar() {
  struct tm tm;
  spr.setTextDatum(TL_DATUM);
  if (getLocalTime(&tm, 0)) {
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &tm);
    spr.setTextColor(rgb(95, 95, 110));
    spr.drawString(buf, 6, 3, 2);
  }
  if (!wifiUp) aaCircle(W - 10, 10, 3, COL_RED);
}

void faceDraw() {
  uint32_t now = millis();
  spr.fillSprite(BG);
  uint16_t col = rgb((uint8_t)colR, (uint8_t)colG, (uint8_t)colB);

  float lookX = eyeCur.lx * 34, lookY = eyeCur.ly * 20;
  float cy = FACE_CY + bob + lookY;
  float lcx = W / 2 - EYE_GAP + lookX, rcx = W / 2 + EYE_GAP + lookX;
  float sL = 1 - eyeCur.lx * 0.08f, sR = 1 + eyeCur.lx * 0.08f;  // a little depth

  // Blush when happy
  if (eyeCur.happy > 0.5f || expr == EX_LOVE) {
    uint16_t blush = rgb(110, 30, 60);
    spr.fillEllipse(lcx - 14, cy + 50, 16, 7, blush);
    spr.fillEllipse(rcx + 14, cy + 50, 16, 7, blush);
  }

  if (expr == EX_DIZZY) {
    float ph = now * 0.012f;
    drawSpiral(lcx, cy, col, ph);
    drawSpiral(rcx, cy, col, ph + 1.5f);
  } else {
    for (int i = 0; i < 2; i++) {
      bool left = i == 0;
      float s = left ? sL : sR;
      float open = eyeCur.open * blinkF;
      float slant = eyeCur.slant;
      if (pokedEye == i && now < pokeUntil) { open = 0.05f; }
      drawEye(left ? lcx : rcx, cy, eyeCur.w * s, eyeCur.h * s, open, slant, eyeCur.happy, left, col);
    }
  }

  drawMouth(W / 2 + lookX * 0.6f, FACE_CY + 62 + bob + lookY * 0.5f, col);
  drawParticles();
  drawStatusBar();
}
