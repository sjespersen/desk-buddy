// ===================== IMU: tilt to look, shake to get dizzy =====================
// The board ships with an LSM6DS3; QMI8658 is handled too, as in Seeed's demos.

enum ImuType : uint8_t { IMU_NONE, IMU_QMI8658, IMU_LSM6 };
ImuType imuType = IMU_NONE;
uint8_t imuAddr = 0;

float gX = 0, gY = 0, gZ = 0;        // low-passed gravity
float restX = 0, restY = 0, restZ = 0;
bool restSet = false;
uint32_t restSince = 0;
float shakeEnergy = 0;
float prevAX = 0, prevAY = 0, prevAZ = 0;

// Which IMU axis maps to screen X/Y in landscape; flip signs here if the eyes
// look the wrong way when tilting.
static constexpr float TILT_SIGN_X = -1.0f;
static constexpr float TILT_SIGN_Y = 1.0f;

static bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static int16_t le16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

static bool initQmi(uint8_t addr) {
  uint8_t who = 0;
  if (!i2cRead(addr, 0x00, &who, 1) || who != 0x05) return false;
  i2cWrite8(addr, 0x02, 0x60);
  i2cWrite8(addr, 0x03, 0x03);
  i2cWrite8(addr, 0x04, 0x53);
  i2cWrite8(addr, 0x08, 0x03);
  imuType = IMU_QMI8658;
  imuAddr = addr;
  return true;
}

static bool initLsm(uint8_t addr) {
  uint8_t who = 0;
  if (!i2cRead(addr, 0x0F, &who, 1)) return false;
  if (who != 0x69 && who != 0x6A && who != 0x6C) return false;
  i2cWrite8(addr, 0x10, 0x60);  // accel 416 Hz, ±2 g
  i2cWrite8(addr, 0x11, 0x60);
  imuType = IMU_LSM6;
  imuAddr = addr;
  return true;
}

void imuInit() {
  bool ok = initLsm(0x6A) || initLsm(0x6B) || initQmi(0x6B) || initQmi(0x6A);
  Serial.printf("[IMU] %s at 0x%02X\n",
                ok ? (imuType == IMU_LSM6 ? "LSM6" : "QMI8658") : "not found", imuAddr);
}

static bool readAccel(float &x, float &y, float &z) {
  uint8_t d[6];
  if (imuType == IMU_LSM6) {
    if (!i2cRead(imuAddr, 0x28, d, 6)) return false;
    x = le16(&d[0]) * 0.000061f;
    y = le16(&d[2]) * 0.000061f;
    z = le16(&d[4]) * 0.000061f;
    return true;
  }
  if (imuType == IMU_QMI8658) {
    if (!i2cRead(imuAddr, 0x35, d, 6)) return false;
    x = le16(&d[0]) / 16384.0f;
    y = le16(&d[2]) / 16384.0f;
    z = le16(&d[4]) / 16384.0f;
    return true;
  }
  return false;
}

void imuUpdate(float dt) {
  if (imuType == IMU_NONE) return;
  float ax, ay, az;
  if (!readAccel(ax, ay, az)) return;
  uint32_t now = millis();

  // Shake: sum of sample-to-sample jerk, leaky.
  float jerk = fabsf(ax - prevAX) + fabsf(ay - prevAY) + fabsf(az - prevAZ);
  prevAX = ax; prevAY = ay; prevAZ = az;
  shakeEnergy = shakeEnergy * expf(-dt * 4) + jerk;
  if (shakeEnergy > 6.0f) {
    shakeEnergy = 0;
    if (page == PAGE_FACE) faceShake();
  }

  float k = 1 - expf(-dt * 6);
  gX += (ax - gX) * k;
  gY += (ay - gY) * k;
  gZ += (az - gZ) * k;

  // The resting orientation slowly adapts, so the buddy only reacts to
  // changes in tilt, not to how the case happens to stand.
  if (!restSet && now > 1500) {
    restX = gX; restY = gY; restZ = gZ;
    restSet = true;
  }
  if (!restSet) return;
  float kr = 1 - expf(-dt * 0.15f);
  restX += (gX - restX) * kr;
  restY += (gY - restY) * kr;
  restZ += (gZ - restZ) * kr;

  float dx = (gY - restY) * TILT_SIGN_X;
  float dy = (gX - restX) * TILT_SIGN_Y;
  bool active = fabsf(dx) > 0.12f || fabsf(dy) > 0.12f;
  if (active) noteInteraction();
  faceTilt(active, fmaxf(-1, fminf(1, dx * 2.5f)), fmaxf(-1, fminf(1, dy * 2.5f)));
}
