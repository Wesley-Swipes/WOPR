// Credit to WesleySwipes on Makerworld for this version
// 2 boxes on left and right changing randomly , 2 vertical dots traveling up/down randomly in the middle

#include <MD_MAX72xx.h>
#include <SPI.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   5

// ESP32 pins
static const uint8_t PIN_MOSI = 21;  // DIN
static const uint8_t PIN_SCK  = 18;  // CLK
static const uint8_t CS_PIN   = 27;  // CS/LOAD

// SPI stability (slow + explicit config)
static const uint32_t SPI_HZ = 500000;     // 500 kHz
static const uint16_t STARTUP_SETTLE_MS = 250;
static const uint8_t  INIT_PASSES = 6;
static const uint16_t INIT_GAP_MS = 70;

const uint8_t H = 8;
const uint8_t W = MAX_DEVICES * 8;   // 40

const uint8_t INTENSITY = 1;

// ---------------- WINDOWS / CUTOUTS ----------------

// Main window (10x3)
static const uint8_t MAIN_WIN_W  = 10;
static const uint8_t MAIN_WIN_H  = 3;
static const uint8_t MAIN_WIN_X0 = 0;
static const uint8_t MAIN_WIN_Y0 = 0;

// Face area: rightmost 10 columns
static const uint8_t FACE_W  = 10;
static const uint8_t FACE_X0 = W - FACE_W; // 30..39

// Left big area
static const uint8_t LEFT_W  = FACE_X0;     // 30 cols (0..29)

// Eyes: 3x3 at bottom
static const uint8_t EYE_W  = 3;
static const uint8_t EYE_H  = 3;
static const uint8_t EYE_Y0 = H - EYE_H;    // 5..7

// Mouth: 4x3
static const uint8_t MOUTH_W  = 4;
static const uint8_t MOUTH_H  = 3;
static const uint8_t MOUTH_Y0 = 5;

// Pack face horizontally into 10 columns: [3][4][3]
static const uint8_t EYE_L_X0 = FACE_X0 + 0;                 // 30..32
static const uint8_t MOUTH_X0 = FACE_X0 + EYE_W;             // 33..36
static const uint8_t EYE_R_X0 = FACE_X0 + EYE_W + MOUTH_W;   // 37..39

// ---------------- FEEL / TIMING ----------------

// Left big area (30x8) column persistence
static const uint8_t  LEFT_CHANGE_DIV  = 2;
static const uint8_t  BIT_DENSITY      = 120;
static const uint16_t STEP_DELAYS[4]   = { 0, 15, 35, 90 };
static const uint16_t HOLD_CHANCE_DIV  = 60;
static const uint16_t HOLD_MIN_MS      = 250;
static const uint16_t HOLD_MAX_MS      = 1600;

// 10x3 window persistence
static const uint8_t MAIN_CHANGE_DIV   = 2;

// Eyes: random 3x3 pattern update timing
static const uint16_t EYE_PATTERN_MIN_MS = 250;
static const uint16_t EYE_PATTERN_MAX_MS = 1200;

// Mouth bounce speed
static const uint16_t MOUTH_STEP_MIN_MS = 300;
static const uint16_t MOUTH_STEP_MAX_MS = 700;

MD_MAX72XX mx(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// ------------ RNG ------------
uint32_t rng32() {
  static uint32_t x = 0xA3C59AC3u;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return x;
}
uint8_t  urand8() { return (uint8_t)(rng32() & 0xFF); }
uint16_t urand16(uint16_t n) { return (uint16_t)(rng32() % n); }
uint16_t urandRange(uint16_t a, uint16_t b) { return a + urand16((uint16_t)(b - a + 1)); }

inline void setPx(uint8_t x, uint8_t y, bool on) {
  if (x < W && y < H) mx.setPoint(y, x, on);
}

inline bool inRect(uint8_t x, uint8_t y, uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) {
  return (x >= x0 && x < (uint8_t)(x0 + w) && y >= y0 && y < (uint8_t)(y0 + h));
}

void clearRect(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) {
  for (uint8_t x = x0; x < (uint8_t)(x0 + w); x++)
    for (uint8_t y = y0; y < (uint8_t)(y0 + h); y++)
      setPx(x, y, false);
}

// Mask ONLY the face area (right 10 cols).
void enforceFaceMaskOnly() {
  for (uint8_t x = FACE_X0; x < (uint8_t)(FACE_X0 + FACE_W); x++) {
    for (uint8_t y = 0; y < H; y++) {
      bool ok =
        inRect(x, y, EYE_L_X0, EYE_Y0,   EYE_W,   EYE_H) ||
        inRect(x, y, EYE_R_X0, EYE_Y0,   EYE_W,   EYE_H) ||
        inRect(x, y, MOUTH_X0, MOUTH_Y0, MOUTH_W, MOUTH_H);

      if (!ok) setPx(x, y, false);
    }
  }
}

// ------------ LEFT BIG SCREEN (30x8) ------------
uint8_t leftCols[LEFT_W];

uint8_t makeRandomCol8() {
  uint8_t v = 0;
  for (uint8_t y = 0; y < 8; y++) if (urand8() < BIT_DENSITY) v |= (1u << y);
  return v;
}
void drawLeftCol(uint8_t x, uint8_t bits8) {
  for (uint8_t y = 0; y < 8; y++) setPx(x, y, (bits8 >> y) & 1u);
}
void stepLeftScreen() {
  for (uint8_t x = 0; x < LEFT_W; x++) {
    if (urand16(LEFT_CHANGE_DIV) == 0) {
      leftCols[x] = makeRandomCol8();
      drawLeftCol(x, leftCols[x]);
    }
  }
}

// ------------ MAIN 10x3 WINDOW ------------
uint8_t mainCols[MAIN_WIN_W];

uint8_t makeRandomCol3() {
  uint8_t v = 0;
  for (uint8_t y = 0; y < MAIN_WIN_H; y++) if (urand8() < BIT_DENSITY) v |= (1u << y);
  return v;
}
void drawMainCol(uint8_t i, uint8_t bits3) {
  uint8_t x = (uint8_t)(MAIN_WIN_X0 + i);
  for (uint8_t y = 0; y < MAIN_WIN_H; y++) {
    setPx(x, (uint8_t)(MAIN_WIN_Y0 + y), (bits3 >> y) & 1u);
  }
}
void stepMainWindow() {
  for (uint8_t i = 0; i < MAIN_WIN_W; i++) {
    if (urand16(MAIN_CHANGE_DIV) == 0) {
      mainCols[i] = makeRandomCol3();
      drawMainCol(i, mainCols[i]);
    }
  }
}

// ------------ EYES: random 3x3 patterns ------------
uint32_t eyeL_nextPattern = 0;
uint32_t eyeR_nextPattern = 0;

uint16_t makeRandomBits9() {
  uint16_t v = 0;
  for (uint8_t i = 0; i < 9; i++) {
    if (urand8() < 140) v |= (1u << i);
  }
  return v;
}

void drawEyePattern(uint8_t x0, uint8_t y0, uint16_t bits9) {
  for (uint8_t ry = 0; ry < 3; ry++) {
    for (uint8_t rx = 0; rx < 3; rx++) {
      uint8_t i = (uint8_t)(ry * 3 + rx);
      setPx((uint8_t)(x0 + rx), (uint8_t)(y0 + ry), (bits9 >> i) & 1u);
    }
  }
}

void stepEyes(uint32_t now) {
  if ((int32_t)(now - eyeL_nextPattern) >= 0) {
    drawEyePattern(EYE_L_X0, EYE_Y0, makeRandomBits9());
    eyeL_nextPattern = now + urandRange(EYE_PATTERN_MIN_MS, EYE_PATTERN_MAX_MS);
  }
  if ((int32_t)(now - eyeR_nextPattern) >= 0) {
    drawEyePattern(EYE_R_X0, EYE_Y0, makeRandomBits9());
    eyeR_nextPattern = now + urandRange(EYE_PATTERN_MIN_MS, EYE_PATTERN_MAX_MS);
  }
}

// ------------ MOUTH (bouncing dot, vertically flipped) ------------
int8_t mouthPos = 0;
int8_t mouthDir = 1;
uint32_t mouth_nextStep = 0;

void drawMouth() {
  clearRect(MOUTH_X0, MOUTH_Y0, MOUTH_W, MOUTH_H);

  uint8_t x = (uint8_t)(MOUTH_X0 + 1);
  uint8_t y = (uint8_t)(MOUTH_Y0 + (MOUTH_H - 1 - mouthPos)); // vertical flip

  setPx(x, y, true);
  if (urand16(3) == 0 && (x + 1) < (uint8_t)(MOUTH_X0 + MOUTH_W)) setPx((uint8_t)(x + 1), y, true);
}

void stepMouth(uint32_t now) {
  if ((int32_t)(now - mouth_nextStep) < 0) return;

  mouthPos += mouthDir;
  if (mouthPos <= 0) { mouthPos = 0; mouthDir = 1; }
  if (mouthPos >= (int8_t)(MOUTH_H - 1)) { mouthPos = (int8_t)(MOUTH_H - 1); mouthDir = -1; }

  mouth_nextStep = now + urandRange(MOUTH_STEP_MIN_MS, MOUTH_STEP_MAX_MS);
  drawMouth();
}

// ------------ LOOP SCHEDULING ------------
uint32_t nextStepAt = 0;

void initAll() {
  mx.clear();

  for (uint8_t x = 0; x < LEFT_W; x++) {
    leftCols[x] = makeRandomCol8();
    drawLeftCol(x, leftCols[x]);
  }

  for (uint8_t i = 0; i < MAIN_WIN_W; i++) {
    mainCols[i] = makeRandomCol3();
    drawMainCol(i, mainCols[i]);
  }

  uint32_t now = millis();
  eyeL_nextPattern = now + urandRange(0, EYE_PATTERN_MAX_MS);
  eyeR_nextPattern = now + urandRange(0, EYE_PATTERN_MAX_MS);
  mouth_nextStep   = now + urandRange(MOUTH_STEP_MIN_MS, MOUTH_STEP_MAX_MS);

  drawEyePattern(EYE_L_X0, EYE_Y0, makeRandomBits9());
  drawEyePattern(EYE_R_X0, EYE_Y0, makeRandomBits9());
  drawMouth();

  enforceFaceMaskOnly();
}

// Repeatable “hard” init to improve power-up stability
void maxInitPass(uint8_t intensity) {
  mx.control(MD_MAX72XX::SHUTDOWN, true);
  delay(15);
  mx.control(MD_MAX72XX::SHUTDOWN, false);
  mx.control(MD_MAX72XX::TEST, false);
  mx.control(MD_MAX72XX::INTENSITY, intensity);
  mx.clear();
}

void setup() {
  // Make lines sane before SPI starts
  pinMode(CS_PIN, OUTPUT);   digitalWrite(CS_PIN, HIGH);
  pinMode(PIN_SCK, OUTPUT);  digitalWrite(PIN_SCK, LOW);
  pinMode(PIN_MOSI, OUTPUT); digitalWrite(PIN_MOSI, LOW);
  delay(STARTUP_SETTLE_MS);

  SPI.begin(PIN_SCK, -1, PIN_MOSI, CS_PIN);
  SPI.setFrequency(SPI_HZ);
  SPI.setDataMode(SPI_MODE0);

  mx.begin();

  // Multiple init passes + dim ramp
  for (uint8_t i = 0; i < INIT_PASSES; i++) {
    maxInitPass(0);
    delay(INIT_GAP_MS);
  }
  for (uint8_t b = 0; b <= INTENSITY; b++) {
    mx.control(MD_MAX72XX::INTENSITY, b);
    delay(35);
  }

  rng32(); rng32(); rng32();

  initAll();
  nextStepAt = millis();
}

void loop() {
  uint32_t now = millis();

  stepEyes(now);
  stepMouth(now);

  if ((int32_t)(now - nextStepAt) >= 0) {
    if (urand16(HOLD_CHANCE_DIV) == 0) {
      nextStepAt = now + urandRange(HOLD_MIN_MS, HOLD_MAX_MS);
    } else {
      stepLeftScreen();
      stepMainWindow();
      nextStepAt = now + STEP_DELAYS[urand16(4)];
    }
  }

  enforceFaceMaskOnly();
}