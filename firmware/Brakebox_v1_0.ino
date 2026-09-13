// ═══════════════════════════════════════════════════════════════════
// SECTION 1 — Includes & defines
// ═══════════════════════════════════════════════════════════════════
#define LED_PIN       16
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define USBD_MANUFACTURER "BrakeBox"
#define USBD_PRODUCT "BrakeBox v1.0"

#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/TomThumb.h>
#include "HX711.h"
#include <EEPROM.h>
#include <Adafruit_TinyUSB.h>

// ---- Interface 1: Joystick (its own endpoint) ----
uint8_t const hid_report_descriptor[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x04,        // Usage (Joystick)
  0xA1, 0x01,        // Collection (Application)
  0x09, 0x32,        //   Usage (Z)
  0x16, 0x01, 0x80,  //   Logical Minimum (-32767)
  0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
  0x75, 0x10,        //   Report Size (16)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0xC0               // End Collection
};

// ---- Interface 2: OLED framebuffer chunk, own endpoint, own interface ----
// No Report ID needed - it's the only report on this interface, so we
// get the FULL 64-byte endpoint buffer for payload, not 63.
uint8_t const hid_fb_descriptor[] = {
  0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
  0x09, 0x01,        // Usage (Vendor Usage 1)
  0xA1, 0x01,        // Collection (Application)
  0x09, 0x01,        //   Usage
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x00,  //   Logical Maximum (255)
  0x75, 0x08,        //   Report Size (8 bits)
  0x95, 0x40,        //   Report Count (64 bytes)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0xC0               // End Collection
};

Adafruit_USBD_HID usb_hid(hid_report_descriptor, sizeof(hid_report_descriptor),
                           HID_ITF_PROTOCOL_NONE, 1, false);
Adafruit_USBD_HID usb_hid_fb(hid_fb_descriptor, sizeof(hid_fb_descriptor),
                              HID_ITF_PROTOCOL_NONE, 1, false);

// simple struct matching the descriptor — just one 16-bit signed field
struct GamepadReport {
  int16_t z;
};
GamepadReport gp;

Adafruit_NeoPixel strip(1, LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);
HX711 scale;

// ═══════════════════════════════════════════════════════════════════
// SECTION 2 — Pin map
// ═══════════════════════════════════════════════════════════════════
const int ENC_CLK[]     = {6,  14, 27};
const int ENC_DT[]      = {7,  15, 28};
const int ENC_SW[]      = {8,  26, 29};
const int LOADCELL_DOUT = 4;
const int LOADCELL_SCK  = 5;

// ═══════════════════════════════════════════════════════════════════
// SECTION 3 — Encoder state
// ═══════════════════════════════════════════════════════════════════
int  lastCLK[3];
int  lastDT[3];
bool lastSW[3];
unsigned long swDownTime[3];
bool swHeld[3];
bool swConsumed[3];
int8_t encState[3] = {0, 0, 0};
const int LONG_PRESS_MS = 1500;

// ═══════════════════════════════════════════════════════════════════
// SECTION 4 — Modes & enums
// ═══════════════════════════════════════════════════════════════════
enum AppMode {
  NORMAL, PROFILE_SELECT, PROFILE_NAME, CALIBRATION,
  PROFILE_RESET_CONFIRM, PROFILE_DESC, FACTORY_RESET_CONFIRM,
  PEDAL_MISSING, UNSAVED_CHANGES_CONFIRM,
  BANK_SELECT, BANK_NAME, BANK_RESET_CONFIRM
};
enum CurveMode { SINGLE, SYM_S, ASYM_S };
AppMode   appMode   = NORMAL;
CurveMode curveMode = SINGLE;
AppMode   preOverlayMode = NORMAL;

// ═══════════════════════════════════════════════════════════════════
// SECTION 5 — Profile struct & defaults
// ═══════════════════════════════════════════════════════════════════
const int EEPROM_MAGIC       = 0xBEFC;
const int BANK_COUNT         = 25;
const int PROFILES_PER_BANK  = 20;
const int MAX_PROFILES       = BANK_COUNT * PROFILES_PER_BANK;  // 500
const int NAME_LEN           = 10;
const int DESC_LEN           = 64;
const int BANK_NAME_LEN      = 10;

struct Profile {
  char name[NAME_LEN + 1];
  char desc[DESC_LEN + 1];
  int  dznLo, dznHi;
  int  bia_c,  crv_c;
  int  bia_s,  crv_s;
  int  bia_a,  bia2_a;
  int  crv_a,  crv2_a;
  int  outputLimit;
  int  smoothing;
  CurveMode mode;
};

// Bank 0's 20 factory presets. All other banks (1-24) start blank.
const Profile FACTORY_BANK0[PROFILES_PER_BANK] = {
  {"GT3       ", "PROGRESSIVE CURVE WITH SMOOTH MID-RANGE BITE. GOOD ALL-ROUNDER.",
   0,100, 40,65, 50,50, 30,70,50,50, 100,0, SINGLE},
  {"F1        ", "TINY DEADZONE, LATE AGGRESSIVE BITE. MIMICS BRAKE-BY-WIRE FEEL.",
   5,95, 25,75, 50,50, 30,70,50,50, 100,0, SINGLE},
  {"RALLY     ", "EARLY BITE FOR LOOSE SURFACES. EASES OFF FOR TRAIL-BRAKING.",
   0,100, 50,50, 55,70, 30,70,50,50, 100,20, SYM_S},
  {"ROAD CAR  ", "SOFT EARLY BITE, FLATTENS OFF. COMFORTABLE AND FORGIVING.",
   10,90, 60,35, 50,50, 30,70,50,50, 100,0, SINGLE},
  {"KART      ", "NEAR ON/OFF RESPONSE. MATCHES SHORT KART PEDAL TRAVEL.",
   0,100, 30,20, 50,50, 30,70,50,50, 100,0, SINGLE},
  {"DRIFT     ", "STRONG S-CURVE FOR PRECISE TRAIL-BRAKE DRIFT ENTRIES.",
   0,100, 50,50, 45,85, 30,70,50,50, 100,0, SYM_S},
  {"HYBRID    ", "TWO-STAGE CURVE MIMICS REGEN THEN MECHANICAL BITE.",
   0,100, 50,50, 50,50, 20,80,40,60, 100,0, ASYM_S},
  {"ENDURANCE ", "NEUTRAL LINEAR FEEL. EASIEST ON THE ANKLE OVER LONG STINTS.",
   5,95, 50,50, 50,50, 30,70,50,50, 100,0, SINGLE},
  {"OVAL      ", "LONG DEAD ZONE THEN SHARP BITE. MINIMAL BRAKE USE.",
   0,100, 70,30, 50,50, 30,70,50,50, 100,0, SINGLE},
  {"WET GT3   ", "SOFTER BITE AND WIDER DEADZONE FOR REDUCED WET-TRACK GRIP.",
   10,90, 45,70, 50,50, 30,70,50,50, 85,10, SINGLE},
  {"WET RALLY ", "VERY PROGRESSIVE FEEL FOR LOOSE, SLICK GRAVEL OR MUD.",
   10,95, 55,80, 55,75, 30,70,50,50, 80,25, SYM_S},
  {"FORMULA   ", "RAZOR-SHARP INITIAL BITE FOR OPEN-WHEEL PRECISION BRAKING.",
   0,100, 20,80, 50,50, 30,70,50,50, 100,0, SINGLE},
  {"TOURING   ", "SHARP EARLY BITE SUITED TO FRONT-DRIVE TRAIL-BRAKING STYLES.",
   0,100, 35,72, 50,50, 30,70,50,50, 100,0, SINGLE},
  {"HYPERCAR  ", "AERO-DEPENDENT PROGRESSIVE BITE, STRONG LATE-STAGE FORCE.",
   0,100, 55,68, 50,50, 30,70,50,50, 100,0, SINGLE},
  {"VINTAGE   ", "SOFT, LINEAR FEEL SUITED TO LOW-GRIP CLASSIC-ERA CARS.",
   5,95, 50,50, 50,50, 30,70,50,50, 90,15, SINGLE},
  {"DRAG      ", "SIMPLE HIGH-LIMIT LINEAR RESPONSE. MINIMAL FINESSE NEEDED.",
   0,100, 50,50, 50,50, 30,70,50,50, 100,0, SINGLE},
  {"HEAVY     ", "LONG SOFT RAMP FOR TRUCKS AND OTHER HEAVY SIM VEHICLES.",
   15,100, 65,30, 50,50, 30,70,50,50, 100,20, SINGLE},
  {"SNOW ICE  ", "EXTREMELY SOFT AND PROGRESSIVE FOR VERY LOW GRIP SURFACES.",
   15,95, 60,85, 60,80, 30,70,50,50, 70,30, SYM_S},
  {"TIME TRIAL", "NEUTRAL, PRECISE, NEAR-LINEAR FOR CONSISTENT HOTLAPPING.",
   0,100, 50,52, 50,50, 30,70,50,50, 100,0, SINGLE},
  {"RAIN ROAD ", "SOFT AND FORGIVING WET-WEATHER ROAD CAR SETTING.",
   10,90, 55,60, 50,50, 30,70,50,50, 85,15, SINGLE}
};

Profile profiles[MAX_PROFILES];
char    bankNames[BANK_COUNT][BANK_NAME_LEN + 1];
// ═══════════════════════════════════════════════════════════════════
// SECTION 6 — Global state variables
// ═══════════════════════════════════════════════════════════════════
int  currentProfile  = 0;   // absolute index (bank*PROFILES_PER_BANK + slot)
int  currentBank     = 0;
int  selectedParam   = 0;
bool editingHiBound  = false;
bool editingSlot2    = false;
bool profileDirty    = false;

unsigned long lastInputTime = 0;
const unsigned long HIGHLIGHT_TIMEOUT = 6000;

bool pedalDetected = true;
unsigned long pedalFailStart = 0;
const unsigned long PEDAL_FAIL_TIMEOUT = 2000;
bool pedalWasOk = true;

// ═══════════════════════════════════════════════════════════════════
// SECTION 7 — Calibration variables
// ═══════════════════════════════════════════════════════════════════
long  tareValue    = 0;
long  forceMax     = 90000;
long  calPeak      = 0;
bool  calZeroed    = false;
bool  isCalibrated = false;
long  lastRawSpan  = 0;
long  lastCalRawAbs = 0;

// ═══════════════════════════════════════════════════════════════════
// SECTION 8 — Profile select / naming state
// ═══════════════════════════════════════════════════════════════════
int  selectingProfile = 0;   // absolute index within currentBank being viewed
int  selectingBank     = 0;
int  namePos          = 0;
char nameBuf[11];
int  nameCharIdx      = 0;
const char ASCII_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
const int  ASCII_LEN = sizeof(ASCII_CHARS) - 1;
int  descViewProfile  = 0;
int  pendingProfileSwitch = 0;
bool renamingBank = false;   // true = current PROFILE_NAME/BANK_NAME session is for a bank name, not a profile name

// ═══════════════════════════════════════════════════════════════════
// SECTION 9 — Force & redraw state
// ═══════════════════════════════════════════════════════════════════
float currentForce  = 0.0f;
float smoothedForce = 0.0f;
bool  needsRedraw   = true;
float curveTable[101];

// ═══════════════════════════════════════════════════════════════════
// SECTION 10 — EEPROM functions
// ═══════════════════════════════════════════════════════════════════
const int ADDR_MAGIC       = 0;
const int ADDR_CAL_FLAG    = 4;
const int ADDR_FORCE_MAX   = 5;
const int ADDR_PROFILE     = 9;
const int ADDR_BANKNAMES   = 13;
const int ADDR_PROFILES    = ADDR_BANKNAMES + (BANK_COUNT * (BANK_NAME_LEN + 1));

int profileIndex(int bank, int slot) { return bank * PROFILES_PER_BANK + slot; }

void makeBlankProfile(Profile &p) {
  memset(p.name, ' ', NAME_LEN); p.name[NAME_LEN] = 0;
  p.desc[0] = 0,
  p.dznLo=0; p.dznHi=100;
  p.bia_c=50; p.crv_c=50;
  p.bia_s=50; p.crv_s=50;
  p.bia_a=30; p.bia2_a=70; p.crv_a=50; p.crv2_a=50;
  p.outputLimit=100; p.smoothing=0;
  p.mode = SINGLE;
}

void saveCalToEEPROM() {
  EEPROM.put(ADDR_MAGIC,     EEPROM_MAGIC);
  EEPROM.put(ADDR_CAL_FLAG,  isCalibrated);
  EEPROM.put(ADDR_FORCE_MAX, forceMax);
  EEPROM.commit();
}

void saveToEEPROM() {
  EEPROM.put(ADDR_MAGIC,     EEPROM_MAGIC);
  EEPROM.put(ADDR_CAL_FLAG,  isCalibrated);
  EEPROM.put(ADDR_FORCE_MAX, forceMax);
  EEPROM.put(ADDR_PROFILE,   currentProfile);
  for (int b = 0; b < BANK_COUNT; b++)
    EEPROM.put(ADDR_BANKNAMES + b*(BANK_NAME_LEN+1), bankNames[b]);
  for (int i = 0; i < MAX_PROFILES; i++)
    EEPROM.put(ADDR_PROFILES + i * sizeof(Profile), profiles[i]);
  EEPROM.commit();
  profileDirty = false;
}

void loadFromEEPROM() {
  // defaults first: bank 0 = presets, all other banks = blank, bank names blank
  for (int slot = 0; slot < PROFILES_PER_BANK; slot++)
    profiles[profileIndex(0, slot)] = FACTORY_BANK0[slot];
  for (int b = 1; b < BANK_COUNT; b++)
    for (int slot = 0; slot < PROFILES_PER_BANK; slot++)
      makeBlankProfile(profiles[profileIndex(b, slot)]);
  for (int b = 0; b < BANK_COUNT; b++) {
    snprintf(bankNames[b], BANK_NAME_LEN+1, (b==0) ? "PRESETS   " : "BANK %-4d ", b+1);
  }

  int magic;
  EEPROM.get(ADDR_MAGIC, magic);
  if (magic != EEPROM_MAGIC) return;  // fresh boot, keep defaults above

  EEPROM.get(ADDR_CAL_FLAG,  isCalibrated);
  EEPROM.get(ADDR_FORCE_MAX, forceMax);
  EEPROM.get(ADDR_PROFILE,   currentProfile);
  if (currentProfile < 0 || currentProfile >= MAX_PROFILES) currentProfile = 0;
  currentBank = currentProfile / PROFILES_PER_BANK;

  for (int b = 0; b < BANK_COUNT; b++)
    EEPROM.get(ADDR_BANKNAMES + b*(BANK_NAME_LEN+1), bankNames[b]);
  for (int i = 0; i < MAX_PROFILES; i++)
    EEPROM.get(ADDR_PROFILES + i * sizeof(Profile), profiles[i]);

  curveMode    = profiles[currentProfile].mode;
  profileDirty = false;
}

void resetProfileToFactory(int idx) {
  int bank = idx / PROFILES_PER_BANK;
  int slot = idx % PROFILES_PER_BANK;
  if (bank == 0) profiles[idx] = FACTORY_BANK0[slot];
  else           makeBlankProfile(profiles[idx]);
  if (idx == currentProfile) {
    curveMode = profiles[currentProfile].mode;
    buildLookup();
  }
  profileDirty = true;
}

void resetBankToFactory(int bank) {
  for (int slot = 0; slot < PROFILES_PER_BANK; slot++) {
    int idx = profileIndex(bank, slot);
    if (bank == 0) profiles[idx] = FACTORY_BANK0[slot];
    else           makeBlankProfile(profiles[idx]);
  }
  if (bank != 0) snprintf(bankNames[bank], BANK_NAME_LEN+1, "BANK %-4d ", bank+1);
  if (bank == currentBank) {
    curveMode = profiles[currentProfile].mode;
    buildLookup();
  }
  profileDirty = true;
}

void factoryResetAll() {
  for (int slot = 0; slot < PROFILES_PER_BANK; slot++)
    EEPROM.put(ADDR_PROFILES + profileIndex(0,slot) * sizeof(Profile), FACTORY_BANK0[slot]);
  Profile blank;
  makeBlankProfile(blank);
  for (int b = 1; b < BANK_COUNT; b++)
    for (int slot = 0; slot < PROFILES_PER_BANK; slot++)
      EEPROM.put(ADDR_PROFILES + profileIndex(b,slot) * sizeof(Profile), blank);
  int badMagic = 0;
  EEPROM.put(ADDR_MAGIC, badMagic);
  EEPROM.commit();
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 11 — Layout constants
// ═══════════════════════════════════════════════════════════════════
const int TOP_LINE = 9;
const int BAR_Y    = 44;
const int BAR_H    = 20;
const int COL_W    = 42;
const int CX       = 4;
const int CY       = 42;
const int CW       = 120;
const int CH       = 31;

// ═══════════════════════════════════════════════════════════════════
// SECTION 12 — Curve lookup table
// ═══════════════════════════════════════════════════════════════════
void getModeParams(float &bia, float &bia2, float &pow1, float &pow2) {
  Profile &p = profiles[currentProfile];
  switch (curveMode) {
     case SINGLE:
      bia  = p.bia_c / 100.0f;
      bia2 = bia;
      pow1 = expf((p.crv_c - 50) / 20.0f);
      pow2 = pow1;
      break;
    case SYM_S:
      bia  = p.bia_s / 100.0f;
      bia2 = bia;
      pow1 = expf((p.crv_s - 50) / 20.0f);
      pow2 = pow1;
      break;
    case ASYM_S:
      bia  = p.bia_a  / 100.0f;
      bia2 = p.bia2_a / 100.0f;
      pow1 = expf((p.crv_a  - 50) / 20.0f);
      pow2 = expf((p.crv2_a - 50) / 20.0f);
      break;
     }
  bia2 = max(bia2, bia + 0.01f);
}
float evalCurveShape(float xn, CurveMode mode, float bia, float bia2, float pow1, float pow2) {
  float y;
  switch (mode) {
    case SINGLE: {
      float b = constrain(bia, 0.01f, 0.99f);
      float i1 = (profiles[currentProfile].crv_c - 50) / 50.0f;
      float p1x = constrain(b - i1*0.6f, 0.02f, 0.98f);
      float p1y = constrain(b + i1*0.6f, 0.02f, 0.98f);
      float p2x = constrain(b + 0.1f,    0.02f, 0.98f);
      float p2y = constrain(b + 0.1f,    0.02f, 0.98f);
      float best_t = xn;
      for (int iter = 0; iter < 8; iter++) {
        float bx  = 2*(1-best_t)*best_t*p1x + best_t*best_t;
        float dbx = 2*(1-2*best_t)*p1x + 2*best_t;
        if (fabsf(dbx) > 0.0001f) best_t -= (bx - xn) / dbx;
        best_t = constrain(best_t, 0.0f, 1.0f);
      }
      y = 2*(1-best_t)*best_t*p1y + best_t*best_t;
      break;
    }
    case SYM_S: {
      float bn = constrain(bia, 0.01f, 0.99f);
      if (xn <= bn) y = bn * powf(xn / bn, 1.0f / pow1);
      else y = bn + (1.0f - bn) * powf((xn - bn) / (1.0f - bn), pow1);
      break;
    }
    case ASYM_S: {
      float bn  = constrain(bia,  0.01f, 0.99f);
      float bn2 = constrain(bia2, bn + 0.01f, 0.99f);
      float mid = (bn + bn2) / 2.0f;
      if (xn <= bn) y = bn * powf(xn / bn, 1.0f / pow1);
      else if (xn <= mid) { float t=(xn-bn)/max(mid-bn,0.001f); y = bn + (mid-bn)*powf(t,pow1); }
      else if (xn <= bn2) { float t=(xn-mid)/max(bn2-mid,0.001f); y = mid + (bn2-mid)*powf(t,1.0f/pow2); }
      else { float t=(xn-bn2)/(1.0f-bn2); y = bn2 + (1.0f-bn2)*powf(t,pow2); }
      break;
    }
  }
  return constrain(y, 0.0f, 1.0f);
}

void buildLookup() {
  Profile &p = profiles[currentProfile];
  float bia, bia2, pow1, pow2;
  getModeParams(bia, bia2, pow1, pow2);

  float loF = p.dznLo / 100.0f;

  for (int i = 0; i <= 100; i++) {
    float x = i / 100.0f;
    float y;

    if (x <= loF) {
      y = 0.0f;
    } else {
      float xn = (x - loF) / max(1.0f - loF, 0.001f);
      y = evalCurveShape(xn, curveMode, bia, bia2, pow1, pow2);
    }
    curveTable[i] = constrain(y, 0.0f, 1.0f);
  }
}

void checkPedalDetection(long raw) {
  bool railed  = (raw <= -8380000 || raw >= 8380000);
  bool floating = (raw > -50000 && raw < 50000);  // implausibly close to zero = disconnected
  bool badSignal = railed || floating;

  if (badSignal) {
    if (pedalFailStart == 0) pedalFailStart = millis();
    if (millis() - pedalFailStart > 300) {
      if (pedalDetected) {
        pedalDetected = false;
        isCalibrated  = false;
        if (appMode != PEDAL_MISSING) {
          preOverlayMode = appMode;
          appMode = PEDAL_MISSING;
        }
        needsRedraw = true;
      }
    }
  } else {
    pedalFailStart = 0;
    if (!pedalDetected) {
      pedalDetected = true;
      if (appMode == PEDAL_MISSING) {
        appMode = preOverlayMode;
      }
      needsRedraw = true;
    }
  }
}
// ═══════════════════════════════════════════════════════════════════
// SECTION 13 — Smoothing & output
// ═══════════════════════════════════════════════════════════════════
float applySmoothing(float raw) {
  Profile &p = profiles[currentProfile];
  float alpha = 1.0f - (p.smoothing / 100.0f) * 0.95f;
  smoothedForce = smoothedForce + alpha * (raw - smoothedForce);
  return smoothedForce;
}

long applyOutputLimit(float force) {
  Profile &p = profiles[currentProfile];
  int idx = constrain((int)(force * 100.0f), 0, 100);
  float curved = curveTable[idx];

  // DZN H cap: flat linear percentage of max output.
  float capValue = p.dznHi / 100.0f;
  if (curved > capValue) curved = capValue;

  curved *= (p.outputLimit / 100.0f);
  return (long)(constrain(curved, 0.0f, 1.0f) * 32767.0f);
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 14 — Text helpers
// ═══════════════════════════════════════════════════════════════════
void tt(int x, int y, const char* str, bool inv = false) {
  display.setFont(&TomThumb);
  display.setTextColor(inv ? SSD1306_BLACK : SSD1306_WHITE);
  display.setCursor(x, y);
  display.print(str);
  display.setFont(NULL);
  display.setTextColor(SSD1306_WHITE);
}

void ttC(int x, int w, int y, const char* str, bool inv = false) {
  int tw = strlen(str) * 4;
  tt(x + (w - tw) / 2, y, str, inv);
}

void drawX(int x, int y) {
  display.drawLine(x-2, y-2, x+2, y+2, SSD1306_WHITE);
  display.drawLine(x+2, y-2, x-2, y+2, SSD1306_WHITE);
}

// 3-segment progress bar shown in place of the title while ENC1 is held
// on a reset-confirm screen. Each segment fills every 500ms; the middle
// segment carries a "HOLD" label that inverts once that segment fills.
void drawHoldBar(unsigned long heldMs, int yOffset = 3) {
  const int segW = 38, gap = 2, h = 9;
  const int totalW = segW*3 + gap*2;
  const int startX = (128 - totalW) / 2;
  const int y = yOffset;

  for (int s = 0; s < 3; s++) {
    int x = startX + s*(segW+gap);
    bool filled = heldMs >= (unsigned long)(s * 500UL);
    if (filled) display.fillRect(x, y, segW, h, SSD1306_WHITE);
    else        display.drawRect(x, y, segW, h, SSD1306_WHITE);

    if (s == 1) {
      ttC(x, segW, y+7, "HOLD", filled);
    }
  }
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 15 — drawTopRow()
// ═══════════════════════════════════════════════════════════════════
void drawTopRow() {
  Profile &p = profiles[currentProfile];

  bool holdingSave = (swHeld[2] && !swConsumed[2]);
  unsigned long heldMs = holdingSave ? (millis() - swDownTime[2]) : 0;

  // Only show the hold bar once the press has been sustained past a
  // short grace period — avoids flashing it on every quick click,
  // since ENC3 still has its own short-press behaviour on this screen.
  if (holdingSave && heldMs >= 200) {
    drawHoldBar(heldMs, 0);
    return;
  }

  char trimName[NAME_LEN + 1];
  strncpy(trimName, p.name, NAME_LEN + 1);
  for (int i = NAME_LEN-1; i >= 0 && trimName[i] == ' '; i--) trimName[i] = 0;
  tt(1, 7, trimName);

  int rx = 126;

  const char* ms = (curveMode == SINGLE) ? "C" :
                   (curveMode == SYM_S)  ? "S" : "A";
  tt(rx-3, 7, ms);
  rx -= 7;

  if (!isCalibrated) {
    bool flash = ((millis()/500) % 2 == 0);
    if (flash) {
      display.fillRect(rx-13, 0, 13, TOP_LINE, SSD1306_WHITE);
      tt(rx-12, 7, "CAL", true);
    } else {
      tt(rx-12, 7, "CAL");
    }
    rx -= 16;
  }

  if (profileDirty) {
    bool flash = ((millis()/500) % 2 == 0);
    if (flash) {
      display.fillRect(rx-16, 0, 17, TOP_LINE, SSD1306_WHITE);
      tt(rx-15, 7, "SAVE", true);
    } else {
      tt(rx-15, 7, "SAVE");
    }
    rx -= 19;
  }

  if (!pedalDetected) {
    bool flash = ((millis()/500) % 2 == 0);
    if (flash) {
      display.fillRect(rx-20, 0, 21, TOP_LINE, SSD1306_WHITE);
      tt(rx-19, 7, "INPUT", true);
    } else {
      tt(rx-19, 7, "INPUT");
    }
  }

  display.drawLine(0, TOP_LINE, 127, TOP_LINE, SSD1306_WHITE);
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 16 — drawGraph()
// ═══════════════════════════════════════════════════════════════════
void drawGraph() {
  Profile &p = profiles[currentProfile];

  float loF  = p.dznLo      / 100.0f;
  float hiF  = p.dznHi      / 100.0f;
  float limF = p.outputLimit / 100.0f;

  float biaF, bia2F, pw1, pw2;
  getModeParams(biaF, bia2F, pw1, pw2);

  // axes
  display.drawLine(CX, CY-CH, CX, CY, SSD1306_WHITE);
  display.drawLine(CX, CY, CX+CW, CY, SSD1306_WHITE);

  // DZN H — flat horizontal line at a direct linear percentage of
  // the Y axis, showing the output cap.
  if (p.dznHi < 100) {
    int capY = CY - (int)((p.dznHi / 100.0f) * CH);
    for (int x = CX; x <= CX+CW; x += 3)
      display.drawPixel(x, capY, SSD1306_WHITE);
  }

  // draw curve from table
  int bia1ScreenX = -1, bia1ScreenY = -1;
  int bia2ScreenX = -1, bia2ScreenY = -1;
  int prevPx = CX, prevPy = CY;

  for (int i = 1; i <= 100; i++) {
    float x = i / 100.0f;
    float y = curveTable[i];
    int px = CX + (int)(x * CW);
    int py = constrain(CY - (int)(y * CH), CY-CH, CY);
    display.drawLine(prevPx, prevPy, px, py, SSD1306_WHITE);
    prevPx = px; prevPy = py;

    // BIA1 marker at clamped position
    if (bia1ScreenX == -1 && x >= biaF) {
      bia1ScreenX = px; bia1ScreenY = py;
    }
    // BIA2 marker at clamped position (Asym only)
    if (curveMode == ASYM_S && bia2ScreenX == -1 && x >= bia2F) {
      bia2ScreenX = px; bia2ScreenY = py;
    }
  }

  if (bia1ScreenX != -1) drawX(bia1ScreenX, bia1ScreenY);
  if (curveMode == ASYM_S && bia2ScreenX != -1)
    drawX(max(bia2ScreenX, bia1ScreenX+5), bia2ScreenY);

  // force dot
  float rawF = constrain((float)lastRawSpan /
               (float)max(forceMax,(long)1), 0.0f, 1.0f);
  int dotX = constrain(CX + (int)(rawF*CW), CX, CX+CW);
  int idx  = constrain((int)(rawF*100.0f), 0, 100);
  int dotY = constrain(CY-(int)(curveTable[idx]*CH), CY-CH, CY);
  display.fillRect(dotX-1, dotY-1, 3, 3, SSD1306_WHITE);
}


// ═══════════════════════════════════════════════════════════════════
// SECTION 17 — drawParamBar()
// ═══════════════════════════════════════════════════════════════════
void drawParamBar() {
  Profile &p    = profiles[currentProfile];
  bool    hilit = (millis() - lastInputTime < HIGHLIGHT_TIMEOUT);

  display.drawLine(0,       BAR_Y-1, 127,     BAR_Y-1, SSD1306_WHITE);
  display.drawLine(COL_W,   BAR_Y-1, COL_W,   63,      SSD1306_WHITE);
  display.drawLine(COL_W*2, BAR_Y-1, COL_W*2, 63,      SSD1306_WHITE);

  if (appMode == NORMAL) {
    bool sel0 = hilit && selectedParam == 0;
    if (sel0) display.fillRect(0, BAR_Y, COL_W, BAR_H, SSD1306_WHITE);
    ttC(0, COL_W, BAR_Y+6, "DZN", sel0);
    tt(1,  BAR_Y+12, editingHiBound  ? "*" : " ", sel0);
    tt(5,  BAR_Y+12, "H:", sel0);
    char hv[5]; snprintf(hv, sizeof(hv), "%d", p.dznHi);
    tt(13, BAR_Y+12, hv, sel0);
    tt(1,  BAR_Y+18, !editingHiBound ? "*" : " ", sel0);
    tt(5,  BAR_Y+18, "L:", sel0);
    char lv[5]; snprintf(lv, sizeof(lv), "%d", p.dznLo);
    tt(13, BAR_Y+18, lv, sel0);

    bool sel1 = hilit && selectedParam == 1;
    if (sel1) display.fillRect(COL_W, BAR_Y, COL_W, BAR_H, SSD1306_WHITE);
    int biaVal = 0;
    switch (curveMode) {
      case SINGLE: biaVal = p.bia_c; break;
      case SYM_S:  biaVal = p.bia_s; break;
      case ASYM_S: biaVal = editingSlot2 ? p.bia2_a : p.bia_a; break;
    }
    const char* biaLbl = (curveMode == ASYM_S) ?
                         (editingSlot2 ? "BIA 2" : "BIA 1") : "BIA";
    ttC(COL_W, COL_W, BAR_Y+6,  biaLbl, sel1);
    char bv[5]; snprintf(bv, sizeof(bv), "%d", biaVal);
    ttC(COL_W, COL_W, BAR_Y+13, bv, sel1);

    bool sel2 = hilit && selectedParam == 2;
    if (sel2) display.fillRect(COL_W*2, BAR_Y, COL_W, BAR_H, SSD1306_WHITE);
    int crvVal = 0;
    switch (curveMode) {
      case SINGLE: crvVal = p.crv_c; break;
      case SYM_S:  crvVal = p.crv_s; break;
      case ASYM_S: crvVal = editingSlot2 ? p.crv2_a : p.crv_a; break;
    }
    const char* crvLbl = (curveMode == ASYM_S) ?
                         (editingSlot2 ? "CRV 2" : "CRV 1") : "CRV";
    ttC(COL_W*2, COL_W, BAR_Y+6,  crvLbl, sel2);
    char cv[5]; snprintf(cv, sizeof(cv), "%d", crvVal);
    ttC(COL_W*2, COL_W, BAR_Y+12, cv, sel2);
    if (profileDirty)
      ttC(COL_W*2, COL_W, BAR_Y+18, "L: SAVE", sel2);

  } else if (appMode == CALIBRATION) {
    if (!calZeroed) {
      ttC(0,       COL_W, BAR_Y+12, "NEXT",   false);
      ttC(COL_W*2, COL_W, BAR_Y+12, "CANCEL", false);
    } else {
      ttC(0,       COL_W, BAR_Y+12, "SAVE",   false);
      ttC(COL_W,   COL_W, BAR_Y+12, "RESET",  false);
      ttC(COL_W*2, COL_W, BAR_Y+12, "CANCEL", false);
    }

  } else if (appMode == PROFILE_SELECT) {
    tt(3, BAR_Y+8,  "T: SCROLL", false);
    tt(3, BAR_Y+14, "C: SELECT", false);
    tt(3, BAR_Y+20, "L: DETAIL", false);
    tt(COL_W+3, BAR_Y+8,  "T: -----",  false);
    tt(COL_W+3, BAR_Y+14, "C: BANK",   false);
    tt(COL_W+3, BAR_Y+20, "L: RENAME", false);
    tt(COL_W*2+3, BAR_Y+8,  "T: -----",  false);
    tt(COL_W*2+3, BAR_Y+14, "C: CANCEL", false);
    tt(COL_W*2+3, BAR_Y+20, "L: DEFAULT",false);

  } else if (appMode == BANK_SELECT) {
    tt(3, BAR_Y+8,  "T: SCROLL", false);
    tt(3, BAR_Y+14, "C: SELECT", false);
    tt(3, BAR_Y+20, "L: -----",  false);
    tt(COL_W+3, BAR_Y+8,  "T: -----",  false);
    tt(COL_W+3, BAR_Y+14, "C: PROFILE",false);
    tt(COL_W+3, BAR_Y+20, "L: RENAME", false);
    tt(COL_W*2+3, BAR_Y+8,  "T: -----",  false);
    tt(COL_W*2+3, BAR_Y+14, "C: CANCEL", false);
    tt(COL_W*2+3, BAR_Y+20, "L: DEFAULT",false);

  } else if (appMode == BANK_RESET_CONFIRM) {
    ttC(0,       COL_W, BAR_Y+12, "YES", false);
    ttC(COL_W*2, COL_W, BAR_Y+12, "NO",  false);

  } else if (appMode == PROFILE_RESET_CONFIRM) {
    ttC(0,       COL_W, BAR_Y+12, "YES", false);
    ttC(COL_W*2, COL_W, BAR_Y+12, "NO",  false);

  } else if (appMode == PROFILE_NAME || appMode == BANK_NAME) {
    tt(3, BAR_Y+8,  "T: SELECT", false);
    tt(3, BAR_Y+14, "C: SAVE",   false);
    tt(3, BAR_Y+20, "L: -----",  false);
    tt(COL_W+3, BAR_Y+8,  "T: CHANGE", false);
    tt(COL_W+3, BAR_Y+14, "C: -----",  false);
    tt(COL_W+3, BAR_Y+20, "L: -----",  false);
    tt(COL_W*2+3, BAR_Y+8,  "T: -----",  false);
    tt(COL_W*2+3, BAR_Y+14, "C: CANCEL", false);
    tt(COL_W*2+3, BAR_Y+20, "L: -----",  false);
  

  } else if (appMode == UNSAVED_CHANGES_CONFIRM) {
    ttC(0,       COL_W, BAR_Y+12, "SAVE",    false);
    ttC(COL_W,   COL_W, BAR_Y+12, "DISCARD", false);
    ttC(COL_W*2, COL_W, BAR_Y+12, "CANCEL",  false);
  }
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 18 — drawProfileSelect()
// ═══════════════════════════════════════════════════════════════════
void drawProfileSelect() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  char trimBank[BANK_NAME_LEN+1];
  strncpy(trimBank, bankNames[currentBank], BANK_NAME_LEN+1);
  for (int i = BANK_NAME_LEN-1; i >= 0 && trimBank[i]==' '; i--) trimBank[i]=0;
  char title[24];
  snprintf(title, sizeof(title), "PROFILES: %s", trimBank);
  int tw = strlen(title) * 6;
  display.setCursor((128-tw)/2, 7);
  display.print(title);
  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  int visStart = constrain(selectingProfile-1, 0, PROFILES_PER_BANK-3);
  for (int i = 0; i < 3; i++) {
    int slot = visStart + i;
    if (slot >= PROFILES_PER_BANK) break;
    int idx = profileIndex(currentBank, slot);
    int y = 16 + i * 9;
    bool sel = (slot == selectingProfile);
    if (sel) {
      display.fillRect(0, y, 128, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    char line[20];
    snprintf(line, sizeof(line), "%2d. %s", slot+1, profiles[idx].name);
    tt(2, y+7, line, sel);
    display.setTextColor(SSD1306_WHITE);
  }
  drawParamBar();
}

void drawProfileResetConfirm() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  bool holdingReset = (swHeld[0] && !swConsumed[0]);
  unsigned long heldMs = holdingReset ? (millis() - swDownTime[0]) : 0;

  if (holdingReset) {
    drawHoldBar(heldMs);
  } else {
    int tw = 14 * 6;
    display.setCursor((128-tw)/2, 7);
    display.print("RESET PROFILE?");
  }
  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  display.setFont(&TomThumb);
  char line1[24];
  snprintf(line1, sizeof(line1), "RESET");
  int tw1 = strlen(line1) * 4;
  display.setCursor((128-tw1)/2, 25);
  display.print(line1);

  char trimName[NAME_LEN+1];
  strncpy(trimName, profiles[profileIndex(currentBank, selectingProfile)].name, NAME_LEN+1);
  for (int i = NAME_LEN-1; i >= 0 && trimName[i]==' '; i--) trimName[i]=0;
  int tw2 = strlen(trimName) * 4;
  display.setCursor((128-tw2)/2, 32);
  display.print(trimName);

  const char* line3 = "TO FACTORY DEFAULT?";
  int tw3 = strlen(line3) * 4;
  display.setCursor((128-tw3)/2, 39);
  display.print(line3);
  display.setFont(NULL);

  display.drawLine(0,       BAR_Y-1, 127,     BAR_Y-1, SSD1306_WHITE);
  display.drawLine(COL_W,   BAR_Y-1, COL_W,   63,      SSD1306_WHITE);
  display.drawLine(COL_W*2, BAR_Y-1, COL_W*2, 63,      SSD1306_WHITE);
  ttC(0,       COL_W, BAR_Y+12, "YES", false);
  ttC(COL_W*2, COL_W, BAR_Y+12,  "NO",  false);
}

void drawUnsavedChangesConfirm() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int tw = 14 * 6;
  display.setCursor((128-tw)/2, 7);
  display.print("UNSAVED CHANGES");
  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  display.setFont(&TomThumb);
  const char* line1 = "CURRENT PROFILE IS";
  int tw1 = strlen(line1) * 4;
  display.setCursor((128-tw1)/2, 27);
  display.print(line1);

  const char* line2 = "NOT SAVED!";
  int tw2 = strlen(line2) * 4;
  display.setCursor((128-tw2)/2, 35);
  display.print(line2);
  display.setFont(NULL);

  drawParamBar();
}

void drawFactoryResetConfirm(unsigned long heldMs = 0) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (heldMs > 0) {
    drawHoldBar(heldMs);
  } else {
    int tw = 14 * 6;
    display.setCursor((128-tw)/2, 7);
    display.print("FACTORY RESET?");
  }
  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  display.setFont(&TomThumb);
  const char* line1 = "RESET ALL PROFILES TO";
  int tw1 = strlen(line1) * 4;
  display.setCursor((128-tw1)/2, 25);
  display.print(line1);

  const char* line2 = "FACTORY DEFAULT?";
  int tw2 = strlen(line2) * 4;
  display.setCursor((128-tw2)/2, 32);
  display.print(line2);
  display.setFont(NULL);

  display.drawLine(0,       BAR_Y-1, 127,     BAR_Y-1, SSD1306_WHITE);
  display.drawLine(COL_W,   BAR_Y-1, COL_W,   63,      SSD1306_WHITE);
  display.drawLine(COL_W*2, BAR_Y-1, COL_W*2, 63,      SSD1306_WHITE);
  ttC(0,       COL_W, BAR_Y+12, "YES", false);
  ttC(COL_W*2, COL_W, BAR_Y+12,  "NO",  false);
}

void drawBankSelect() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int tw = 11 * 6;
  display.setCursor((128-tw)/2, 7);
  display.print("SELECT BANK");
  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  int visStart = constrain(selectingBank-1, 0, BANK_COUNT-3);
  for (int i = 0; i < 3; i++) {
    int idx = visStart + i;
    if (idx >= BANK_COUNT) break;
    int y = 16 + i * 9;
    bool sel = (idx == selectingBank);
    if (sel) {
      display.fillRect(0, y, 128, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    char line[20];
    snprintf(line, sizeof(line), "%2d. %s", idx+1, bankNames[idx]);
    tt(2, y+7, line, sel);
    display.setTextColor(SSD1306_WHITE);
  }
  drawParamBar();
}

void drawBankResetConfirm() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  bool holdingReset = (swHeld[0] && !swConsumed[0]);
  unsigned long heldMs = holdingReset ? (millis() - swDownTime[0]) : 0;

  if (holdingReset) {
    drawHoldBar(heldMs);
  } else {
    int tw = 20 * 6;
    display.setCursor((128-tw)/2, 7);
    display.print("RESET ENTIRE BANK?");
  }
  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  display.setFont(&TomThumb);
  char trimName[BANK_NAME_LEN+1];
  strncpy(trimName, bankNames[selectingBank], BANK_NAME_LEN+1);
  for (int i = BANK_NAME_LEN-1; i >= 0 && trimName[i]==' '; i--) trimName[i]=0;
  int tw2 = strlen(trimName) * 4;
  display.setCursor((128-tw2)/2, 27);
  display.print(trimName);

  const char* line2 = "ALL 20 PROFILES WILL BE";
  int tw3 = strlen(line2) * 4;
  display.setCursor((128-tw3)/2, 35);
  display.print(line2);

  const char* line3 = "RESET TO FACTORY DEFAULT";
  int tw4 = strlen(line3) * 4;
  display.setCursor((128-tw4)/2, 42);
  display.print(line3);
  display.setFont(NULL);

  display.drawLine(0,       BAR_Y-1, 127,     BAR_Y-1, SSD1306_WHITE);
  display.drawLine(COL_W,   BAR_Y-1, COL_W,   63,      SSD1306_WHITE);
  display.drawLine(COL_W*2, BAR_Y-1, COL_W*2, 63,      SSD1306_WHITE);
  ttC(0,       COL_W, BAR_Y+12, "YES", false);
  ttC(COL_W*2, COL_W, BAR_Y+12,  "NO",  false);
}

void drawProfileDesc() {
  // draw profile select behind it first
  drawProfileSelect();

  // overlay box — centred, leaving margin to see select screen behind
  int boxX = 6, boxY = 8, boxW = 116, boxH = 48;
  display.fillRect(boxX, boxY, boxW, boxH, SSD1306_BLACK);
  display.drawRect(boxX, boxY, boxW, boxH, SSD1306_WHITE);

  // title — profile name
  display.setFont(&TomThumb);
  char trimName[NAME_LEN+1];
  strncpy(trimName, profiles[descViewProfile].name, NAME_LEN+1);
  for (int i = NAME_LEN-1; i >= 0 && trimName[i]==' '; i--) trimName[i]=0;
  int tw = strlen(trimName) * 4;
  display.setCursor(boxX + (boxW-tw)/2, boxY+7);
  display.print(trimName);
  display.drawLine(boxX+2, boxY+9, boxX+boxW-2, boxY+9, SSD1306_WHITE);

  // body text — word wrapped, ~26 chars per line at TomThumb in this width
  const char* desc = profiles[descViewProfile].desc;
  int maxCharsPerLine = (boxW - 6) / 4;  // 4px per TomThumb char roughly
  int y = boxY + 16;
  int len = strlen(desc);
  int pos = 0;
  while (pos < len && y < boxY + boxH - 4) {
    int lineLen = min(maxCharsPerLine, len - pos);
    // break at last space if possible
    if (pos + lineLen < len) {
      int breakPos = lineLen;
      while (breakPos > 0 && desc[pos+breakPos] != ' ') breakPos--;
      if (breakPos > 0) lineLen = breakPos;
    }
    char lineBuf[32];
    strncpy(lineBuf, desc+pos, lineLen);
    lineBuf[lineLen] = 0;
    display.setCursor(boxX+3, y);
    display.print(lineBuf);
    y += 7;
    pos += lineLen;
    while (pos < len && desc[pos] == ' ') pos++;
  }
  display.setFont(NULL);
}

void drawPedalMissing() {
  // draw whatever's behind it first
  switch (preOverlayMode) {
    case NORMAL:         drawTopRow(); drawGraph(); drawParamBar(); break;
    case PROFILE_SELECT: drawProfileSelect(); break;
    default: break;
  }

  int boxX = 10, boxY = 16, boxW = 108, boxH = 32;
  display.fillRect(boxX, boxY, boxW, boxH, SSD1306_BLACK);
  display.drawRect(boxX, boxY, boxW, boxH, SSD1306_WHITE);

  display.setFont(&TomThumb);
  const char* line1 = "NO PEDAL DETECTED";
  int tw1 = strlen(line1) * 4;
  display.setCursor(boxX + (boxW-tw1)/2, boxY+12);
  display.print(line1);

  const char* line2 = "CHECK CONNECTION";
  int tw2 = strlen(line2) * 4;
  display.setCursor(boxX + (boxW-tw2)/2, boxY+22);
  display.print(line2);
  display.setFont(NULL);
}
// ═══════════════════════════════════════════════════════════════════
// SECTION 19 — drawProfileName()
// ═══════════════════════════════════════════════════════════════════
void drawProfileName() {
  int len = renamingBank ? BANK_NAME_LEN : NAME_LEN;

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  const char* title = renamingBank ? "NAME BANK" : "NAME PROFILE";
  int tw = strlen(title) * 6;
  display.setCursor((128-tw)/2, 7);
  display.print(title);
  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  int trimLen = len;
  while (trimLen > 0 && nameBuf[trimLen-1] == ' ') trimLen--;
  int displayLen = max(trimLen, min(namePos + 1, len));

  int totalW = displayLen * 7 - 1;
  int startX = (128 - totalW) / 2;

  for (int i = 0; i < displayLen; i++) {
    int cx = startX + i * 7;
    bool sel = (i == namePos);
    if (sel) {
      display.fillRect(cx-1, 24, 8, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(cx, 26);
    display.print(nameBuf[i]);
    display.setTextColor(SSD1306_WHITE);
  }
  drawParamBar();
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 20 — drawCalibration()
// ═══════════════════════════════════════════════════════════════════
void drawCalibration() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int tw = 11 * 6;
  display.setCursor((128-tw)/2, 7);
  display.print("CALIBRATION");
  display.drawLine(0, 15, 127, 15, SSD1306_WHITE);

  if (!calZeroed) {
    display.setCursor(2, 21);
    display.print("Foot OFF brake");
    display.setCursor(2, 30);
    display.print("Click Next");
  } else {
    display.setCursor(2, 21);
    display.print("Press max force");
    display.setCursor(2, 30);
    display.print("Click Save");

    display.setFont(&TomThumb);
    char pk[16]; snprintf(pk, sizeof(pk), "PEAK:%6ld", calPeak);
    display.setCursor(126 - (11*4), 42);
    display.print(pk);
    display.setFont(NULL);
  }
  drawParamBar();
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 21 — redraw()
// ═══════════════════════════════════════════════════════════════════
void redraw() {
  display.clearDisplay();
  switch (appMode) {
    case NORMAL:
      drawTopRow();
      drawGraph();
      drawParamBar();
      break;
    case PROFILE_SELECT:          drawProfileSelect();          break;
    case PROFILE_NAME:            drawProfileName();            break;
    case CALIBRATION:             drawCalibration();            break;
    case PROFILE_RESET_CONFIRM:   drawProfileResetConfirm();    break;
    case PROFILE_DESC:            drawProfileDesc();            break;
    case PEDAL_MISSING:           drawPedalMissing();           break;
    case UNSAVED_CHANGES_CONFIRM: drawUnsavedChangesConfirm();  break;
    case BANK_SELECT:             drawBankSelect();             break;
    case BANK_NAME:                drawProfileName();            break;  // reuses same visual, different data
    case BANK_RESET_CONFIRM:      drawBankResetConfirm();       break;
    default: break;
  }
  display.display();
  needsRedraw = false;
}
// ═══════════════════════════════════════════════════════════════════
// SECTION 22 — handleTurn()
// ═══════════════════════════════════════════════════════════════════
void handleTurn(int enc, int dir) {
  Profile &p    = profiles[currentProfile];
  lastInputTime = millis();
  needsRedraw   = true;

  if (appMode == PEDAL_MISSING) {
    appMode = preOverlayMode;
    return;
  }

  if (appMode == PROFILE_DESC) {
    appMode = PROFILE_SELECT;
    return;
  }

  if (appMode == PROFILE_SELECT) {
    if (enc == 0)
      selectingProfile = constrain(selectingProfile+dir, 0, PROFILES_PER_BANK-1);
    return;
  }

  if (appMode == BANK_SELECT) {
    if (enc == 0)
      selectingBank = constrain(selectingBank+dir, 0, BANK_COUNT-1);
    return;
  }

  if (appMode == PROFILE_NAME || appMode == BANK_NAME) {
    int len = renamingBank ? BANK_NAME_LEN : NAME_LEN;
    if (enc == 0) namePos = constrain(namePos+dir, 0, len-1);
    if (enc == 1) {
      nameCharIdx      = (nameCharIdx+dir+ASCII_LEN) % ASCII_LEN;
      nameBuf[namePos] = ASCII_CHARS[nameCharIdx];
    }
    return;
  }

  if (appMode == CALIBRATION) return;

  profileDirty = true;
  switch (enc) {
    case 0:
      if (!editingHiBound)
        p.dznLo = constrain(p.dznLo+dir*2, 0, p.dznHi-2);
      else
        p.dznHi = constrain(p.dznHi+dir*2, p.dznLo+2, 100);
      break;
    case 1:
      switch (curveMode) {
        case SINGLE: p.bia_c = constrain(p.bia_c+dir*2, 0, 100); break;
        case SYM_S:  p.bia_s = constrain(p.bia_s+dir*2, 0, 100); break;
        case ASYM_S:
          if (!editingSlot2)
            p.bia_a  = constrain(p.bia_a +dir*2, 0, p.bia2_a-2);
          else
            p.bia2_a = constrain(p.bia2_a+dir*2, p.bia_a+2, 100);
          break;
      }
      break;
    case 2:
      switch (curveMode) {
        case SINGLE: p.crv_c = constrain(p.crv_c+dir*2, 0, 100); break;
        case SYM_S:  p.crv_s = constrain(p.crv_s+dir*2, 0, 100); break;
        case ASYM_S:
          if (!editingSlot2)
            p.crv_a  = constrain(p.crv_a +dir*2, 0, 100);
          else
            p.crv2_a = constrain(p.crv2_a+dir*2, 0, 100);
          break;
      }
      break;
  }
  selectedParam = enc;
  buildLookup();
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 23 — handleShortPress()
// ═══════════════════════════════════════════════════════════════════
void handleShortPress(int enc) {
  needsRedraw = true;

  if (appMode == PEDAL_MISSING) {
    appMode = preOverlayMode;
    return;
  }

  if (appMode == PROFILE_DESC) {
    appMode = PROFILE_SELECT;
    return;
  }

  if (appMode == PROFILE_RESET_CONFIRM) {
    if (enc == 2) {
      appMode = PROFILE_SELECT;
    }
    return;
  }

  if (appMode == BANK_RESET_CONFIRM) {
    if (enc == 2) {
      appMode = BANK_SELECT;
    }
    return;
  }

  if (appMode == PROFILE_SELECT) {
    switch (enc) {
      case 0: {
        int absIdx = profileIndex(currentBank, selectingProfile);
        if (profileDirty && absIdx != currentProfile) {
          pendingProfileSwitch = absIdx;
          appMode = UNSAVED_CHANGES_CONFIRM;
        } else {
          currentProfile = absIdx;
          curveMode      = profiles[currentProfile].mode;
          editingSlot2   = false;
          profileDirty   = false;
          buildLookup();
          appMode        = NORMAL;
        }
        break;
      }
      case 1:
        selectingBank = currentBank;
        appMode       = BANK_SELECT;
        break;
      case 2:
        appMode = NORMAL;
        break;
    }
    return;
  }

  if (appMode == BANK_SELECT) {
    switch (enc) {
      case 0: {
        currentBank = selectingBank;
        int absIdx = profileIndex(currentBank, 0);
        if (profileDirty && absIdx != currentProfile) {
          pendingProfileSwitch = absIdx;
          selectingProfile = 0;
          appMode = UNSAVED_CHANGES_CONFIRM;
        } else {
          currentProfile   = absIdx;
          selectingProfile = 0;
          curveMode        = profiles[currentProfile].mode;
          editingSlot2     = false;
          profileDirty     = false;
          buildLookup();
          appMode          = PROFILE_SELECT;
        }
        break;
      }
      case 1:
        appMode = PROFILE_SELECT;   // back to profile screen, bank unchanged
        break;
      case 2:
        appMode = NORMAL;
        break;
    }
    return;
  }

  if (appMode == UNSAVED_CHANGES_CONFIRM) {
    switch (enc) {
      case 0:  // Save
        saveToEEPROM();
        currentProfile   = pendingProfileSwitch;
        currentBank      = currentProfile / PROFILES_PER_BANK;
        curveMode        = profiles[currentProfile].mode;
        editingSlot2     = false;
        buildLookup();
        appMode          = NORMAL;
        break;
      case 1:  // Discard
        currentProfile   = pendingProfileSwitch;
        currentBank      = currentProfile / PROFILES_PER_BANK;
        curveMode        = profiles[currentProfile].mode;
        editingSlot2     = false;
        profileDirty     = false;
        buildLookup();
        appMode          = NORMAL;
        break;
      case 2:  // Cancel
        appMode = PROFILE_SELECT;
        break;
    }
    return;
  }

  if (appMode == PROFILE_NAME) {
    switch (enc) {
      case 0:
        memcpy(profiles[currentProfile].name, nameBuf, NAME_LEN+1);
        profileDirty = true;
        appMode      = PROFILE_SELECT;
        break;
      case 2:
        appMode = PROFILE_SELECT;
        break;
    }
    return;
  }

  if (appMode == BANK_NAME) {
    switch (enc) {
      case 0:
        memcpy(bankNames[selectingBank], nameBuf, BANK_NAME_LEN+1);
        profileDirty = true;
        appMode      = BANK_SELECT;
        break;
      case 2:
        appMode = BANK_SELECT;
        break;
    }
    return;
  }

  if (appMode == CALIBRATION) {
    switch (enc) {
      case 0:
        if (!calZeroed) {
          long sum = 0;
          for (int i = 0; i < 5; i++) {
            while (!scale.is_ready()) delay(1);
            sum += scale.read();
          }
          tareValue = sum / 5;
          calPeak   = 0;
          calZeroed = true;
        } else {
          if (calPeak > 10000) {
            forceMax     = calPeak;
            isCalibrated = true;
            saveCalToEEPROM();
          }
          calZeroed = false;
          appMode   = NORMAL;
        }
        break;
      case 1:
        if (calZeroed) {
          calPeak     = 0;
          needsRedraw = true;
        }
        break;
      case 2:
        calZeroed = false;
        appMode   = NORMAL;
        break;
    }
    return;
  }

  // normal mode
  switch (enc) {
    case 0:
      editingHiBound = !editingHiBound;
      selectedParam  = 0;
      lastInputTime  = millis();
      break;
    case 1:
      if (curveMode == ASYM_S) {
        editingSlot2  = !editingSlot2;
        selectedParam = 1;
        lastInputTime = millis();
      } else {
        selectingProfile = currentProfile % PROFILES_PER_BANK;
        appMode          = PROFILE_SELECT;
      }
      break;
    case 2:
      curveMode                     = (CurveMode)((curveMode+1) % 3);
      profiles[currentProfile].mode = curveMode;
      editingSlot2                  = false;
      profileDirty                  = true;
      selectedParam                 = 2;
      lastInputTime                 = millis();
      buildLookup();
      break;
  }
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 24 — handleLongPress()
// ═══════════════════════════════════════════════════════════════════
void handleLongPress(int enc) {
  needsRedraw = true;

  if (appMode == PROFILE_RESET_CONFIRM && enc == 0) {
    resetProfileToFactory(profileIndex(currentBank, selectingProfile));
    showResetConfirmedMessage("PROFILE RESET!");
    appMode = PROFILE_SELECT;
    return;
  }

  if (appMode == BANK_RESET_CONFIRM && enc == 0) {
    resetBankToFactory(selectingBank);
    showResetConfirmedMessage("BANK RESET!");
    appMode = BANK_SELECT;
    return;
  }

  if (appMode == PROFILE_SELECT) {
    if (enc == 0) {
      descViewProfile = profileIndex(currentBank, selectingProfile);
      appMode = PROFILE_DESC;
      return;
    }
    if (enc == 1) {
      renamingBank = false;
      int absIdx = profileIndex(currentBank, selectingProfile);
      memcpy(nameBuf, profiles[absIdx].name, NAME_LEN+1);
      namePos     = 0;
      nameCharIdx = 0;
      appMode     = PROFILE_NAME;
      return;
    }
    if (enc == 2) {
      appMode = PROFILE_RESET_CONFIRM;
      return;
    }
  }

  if (appMode == BANK_SELECT) {
    if (enc == 1) {
      renamingBank = true;
      memcpy(nameBuf, bankNames[selectingBank], BANK_NAME_LEN+1);
      namePos     = 0;
      nameCharIdx = 0;
      appMode     = BANK_NAME;
      return;
    }
    if (enc == 2) {
      appMode = BANK_RESET_CONFIRM;
      return;
    }
    return;  // enc 0 long-press disabled on this screen
  }

  if (appMode == PROFILE_NAME || appMode == BANK_NAME ||
      appMode == PROFILE_DESC || appMode == UNSAVED_CHANGES_CONFIRM) {
    return;  // no long-press functions defined on these screens
  }

  if (appMode == CALIBRATION) {
    return;
  }

  switch (enc) {
    case 0:
      appMode   = CALIBRATION;
      calZeroed = false;
      calPeak   = 0;
      break;
    case 1:
      if (appMode == NORMAL) {
        selectingProfile = currentProfile % PROFILES_PER_BANK;
        appMode          = PROFILE_SELECT;
      }
      break;
    case 2:
      if (appMode == NORMAL) {
        saveToEEPROM();
        showResetConfirmedMessage("PROFILE SAVED!");
        for (int i = 0; i < 3; i++) {
          strip.setPixelColor(0, strip.Color(0, 255, 0));
          strip.show(); delay(120);
          strip.setPixelColor(0, strip.Color(0, 0, 0));
          strip.show(); delay(120);
        }
        strip.setPixelColor(0, strip.Color(0, 0, 255));
        strip.show();
        needsRedraw = true;
      }
      break;
  }
}

void showResetConfirmedMessage(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int tw = strlen(msg) * 6;
  display.setCursor((128-tw)/2, 28);
  display.print(msg);
  display.display();
  delay(1000);
}
// ═══════════════════════════════════════════════════════════════════
// SECTION 25 — Encoder interrupt handlers & dispatch
// ═══════════════════════════════════════════════════════════════════
// quadrature lookup table for robust decoding
// index = (prevCLK<<3)|(prevDT<<2)|(curCLK<<1)|(curDT)
const int8_t QUAD_TABLE[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

void handleEncoders() {
  for (int i = 0; i < 3; i++) {
    int c = digitalRead(ENC_CLK[i]);
    int d = digitalRead(ENC_DT[i]);

    int idx = (lastCLK[i] << 3) | (lastDT[i] << 2) | (c << 1) | d;
    int8_t movement = QUAD_TABLE[idx];

    if (movement != 0) {
      encState[i] += movement;
      if (encState[i] >= 4) {
        handleTurn(i, 1);
        encState[i] = 0;
      } else if (encState[i] <= -4) {
        handleTurn(i, -1);
        encState[i] = 0;
      }
    }

    lastCLK[i] = c;
    lastDT[i]  = d;

    bool sw = digitalRead(ENC_SW[i]);
    if (sw == LOW && lastSW[i] == HIGH) {
      swDownTime[i]  = millis();
      swHeld[i]      = true;
      swConsumed[i]  = false;
    }
    // Fire long-press exactly once, the instant the threshold is
    // crossed, and mark it consumed IMMEDIATELY in the same pass —
    // closes the race window where a release landing right on the
    // threshold could let both long-press AND short-press fire.
    if (sw == LOW && swHeld[i] && !swConsumed[i]) {
      // While ENC1 is held on a reset-confirm screen, keep redrawing
      // so the hold-progress bar animates smoothly.
      if ((i == 0 && (appMode == PROFILE_RESET_CONFIRM || appMode == BANK_RESET_CONFIRM)) ||
      (i == 2 && appMode == NORMAL)) {
        needsRedraw = true;
      }

      if (millis() - swDownTime[i] >= LONG_PRESS_MS) {
        swConsumed[i] = true;
        handleLongPress(i);
      }
    }
    if (sw == HIGH && lastSW[i] == LOW) {
      if (swHeld[i] && !swConsumed[i]) {
        handleShortPress(i);
      }
      swHeld[i] = false;
    }
    lastSW[i] = sw;
  }
}
// ═══════════════════════════════════════════════════════════════════
// SECTION 26 — updateJoystick()
// ═══════════════════════════════════════════════════════════════════
void updateJoystick() {
  if (!usb_hid.ready()) return;
  gp.z = (int16_t)applyOutputLimit(smoothedForce);
  usb_hid.sendReport(0, &gp, sizeof(gp));
}

#define FB_CHUNK_PAYLOAD 61
#define FB_TOTAL_BYTES   1024

uint8_t fbSnapshot[FB_TOTAL_BYTES];

void streamFramebufferChunk() {
  static uint8_t chunkIndex = 0;
  const uint8_t totalChunks = (FB_TOTAL_BYTES + FB_CHUNK_PAYLOAD - 1) / FB_CHUNK_PAYLOAD;

  if (!usb_hid_fb.ready()) return;

  if (chunkIndex == 0) {
    memcpy(fbSnapshot, display.getBuffer(), FB_TOTAL_BYTES);
  }

  int offset = chunkIndex * FB_CHUNK_PAYLOAD;
  int remaining = FB_TOTAL_BYTES - offset;
  uint8_t thisLen = (remaining > FB_CHUNK_PAYLOAD) ? FB_CHUNK_PAYLOAD : remaining;

  uint8_t report[64] = {0};
  report[0] = chunkIndex;
  report[1] = totalChunks;
  report[2] = thisLen;
  memcpy(&report[3], &fbSnapshot[offset], thisLen);

  usb_hid_fb.sendReport(0, report, sizeof(report));

  chunkIndex++;
  if (chunkIndex >= totalChunks) chunkIndex = 0;
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 27 — setup()
// ═══════════════════════════════════════════════════════════════════
void setup() {

  TinyUSBDevice.setID(0x2E8A, 0x0003);
  TinyUSBDevice.setManufacturerDescriptor("BrakeBox");
  TinyUSBDevice.setProductDescriptor("BrakeBox v1.0");

  usb_hid.begin();
  usb_hid_fb.begin();

  strip.begin();
  strip.setPixelColor(0, strip.Color(255, 0, 0));
  strip.show();

  EEPROM.begin(70000);
  Serial.begin(115200);
  delay(3000);
  Serial.print("sizeof(Profile) = ");
  Serial.println(sizeof(Profile));
  Serial.print("Total EEPROM needed = ");
  Serial.println(ADDR_PROFILES + MAX_PROFILES * sizeof(Profile));

  pinMode(ENC_SW[0], INPUT_PULLUP);
  pinMode(ENC_SW[2], INPUT_PULLUP);
  delay(300);

  bool resetHeld = (digitalRead(ENC_SW[2]) == LOW);
  if (resetHeld) {
    for (int i = 0; i < 20; i++) {
      delay(100);
      if (digitalRead(ENC_SW[2]) == HIGH) { resetHeld = false; break; }
    }
  }

  loadFromEEPROM();
  isCalibrated = false;
  buildLookup();

  Wire1.setSDA(2);
  Wire1.setSCL(3);
  Wire1.begin();
  Wire1.setClock(400000);  // Fast Mode I2C (400kHz) instead of the 100kHz default

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  if (resetHeld) {
    display.clearDisplay();
    drawFactoryResetConfirm();
    display.display();

    while (digitalRead(ENC_SW[2]) == LOW) delay(20);
    delay(200);

    bool answered = false;
    bool doReset  = false;
    bool btn0Down = false;
    unsigned long btn0DownTime = 0;
    unsigned long lastRedraw = 0;

    while (!answered) {
      bool b0 = (digitalRead(ENC_SW[0]) == LOW);
      bool b2 = (digitalRead(ENC_SW[2]) == LOW);

      if (b0 && !btn0Down) { btn0Down = true; btn0DownTime = millis(); }
      if (!b0) btn0Down = false;

      unsigned long heldMs = btn0Down ? (millis() - btn0DownTime) : 0;

      if (millis() - lastRedraw >= 50) {
        lastRedraw = millis();
        display.clearDisplay();
        drawFactoryResetConfirm(heldMs);
        display.display();
      }

      if (btn0Down && heldMs >= 1500) { doReset = true; answered = true; }
      if (b2) { doReset = false; answered = true; }
      delay(10);
    }
    delay(200);

    if (doReset) {
      factoryResetAll();
      loadFromEEPROM();
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(10, 28);
      display.print("FACTORY RESET DONE");
      display.display();
      delay(1500);
    }
    display.clearDisplay();
  }

  appMode   = CALIBRATION;
  calZeroed = false;
  calPeak   = 0;
  needsRedraw = true;

  // splash screen
  display.drawCircle(22, 32, 20, SSD1306_WHITE);
  display.drawCircle(22, 32, 9,  SSD1306_WHITE);
  display.drawCircle(22, 32, 6,  SSD1306_WHITE);
  display.fillCircle(22, 32, 2,  SSD1306_BLACK);
  display.drawCircle(22, 32, 2,  SSD1306_WHITE);

  display.drawPixel(22, 21, SSD1306_WHITE);
  display.drawPixel(24, 19, SSD1306_WHITE);
  display.drawPixel(27, 17, SSD1306_WHITE);
  display.drawPixel(31, 17, SSD1306_WHITE);
  display.drawPixel(29, 23, SSD1306_WHITE);
  display.drawPixel(32, 23, SSD1306_WHITE);
  display.drawPixel(36, 24, SSD1306_WHITE);
  display.drawPixel(39, 26, SSD1306_WHITE);
  display.drawPixel(33, 30, SSD1306_WHITE);
  display.drawPixel(35, 32, SSD1306_WHITE);
  display.drawPixel(37, 35, SSD1306_WHITE);
  display.drawPixel(39, 38, SSD1306_WHITE);
  display.drawPixel(32, 38, SSD1306_WHITE);
  display.drawPixel(32, 41, SSD1306_WHITE);
  display.drawPixel(32, 44, SSD1306_WHITE);
  display.drawPixel(31, 47, SSD1306_WHITE);
  display.drawPixel(26, 43, SSD1306_WHITE);
  display.drawPixel(24, 45, SSD1306_WHITE);
  display.drawPixel(22, 48, SSD1306_WHITE);
  display.drawPixel(19, 50, SSD1306_WHITE);
  display.drawPixel(18, 43, SSD1306_WHITE);
  display.drawPixel(15, 44, SSD1306_WHITE);
  display.drawPixel(12, 44, SSD1306_WHITE);
  display.drawPixel(8,  43, SSD1306_WHITE);
  display.drawPixel(12, 38, SSD1306_WHITE);
  display.drawPixel(9,  37, SSD1306_WHITE);
  display.drawPixel(7,  35, SSD1306_WHITE);
  display.drawPixel(4,  32, SSD1306_WHITE);
  display.drawPixel(11, 30, SSD1306_WHITE);
  display.drawPixel(9,  27, SSD1306_WHITE);
  display.drawPixel(8,  24, SSD1306_WHITE);
  display.drawPixel(8,  21, SSD1306_WHITE);
  display.drawPixel(15, 23, SSD1306_WHITE);
  display.drawPixel(15, 20, SSD1306_WHITE);
  display.drawPixel(17, 17, SSD1306_WHITE);
  display.drawPixel(19, 14, SSD1306_WHITE);

  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(45, 18);
  display.print("BRAKEBOX");

  display.setFont(&TomThumb);
  display.setCursor(45, 32);
  display.print("LOADCELL BRAKE SYSTEM");
  display.setFont(NULL);

  display.drawLine(45, 34, 133, 34, SSD1306_WHITE);

  display.setFont(&TomThumb);
  display.setCursor(45, 48);
  display.print("v1.0");
  display.setFont(NULL);

  display.display();
  delay(2500);
  display.clearDisplay();
  // END SPLASH

  scale.begin(LOADCELL_DOUT, LOADCELL_SCK);
  delay(500);
  if (scale.is_ready()) tareValue = scale.read_average(5);

  // pedal detection — check for railed HX711 output (disconnected power wire)
  bool pedalOk = false;
  int  goodStreak = 0;
  for (int attempt = 0; attempt < 20; attempt++) {
    if (scale.is_ready()) {
      long raw = scale.read();
      bool railed   = (raw <= -8380000 || raw >= 8380000);
      bool floating = (raw > -50000 && raw < 50000);
      bool bad = railed || floating;
      if (!bad) {
        goodStreak++;
        if (goodStreak >= 5) { pedalOk = true; break; }
      } else {
        goodStreak = 0;
      }
    }
    delay(50);
  }

  if (!pedalOk) {
    pedalDetected  = false;
    isCalibrated   = false;
    preOverlayMode = CALIBRATION;
    appMode        = PEDAL_MISSING;

    display.clearDisplay();
    drawPedalMissing();
    display.display();

    while (!pedalOk) {
      delay(50);
      if (scale.is_ready()) {
        long raw = scale.read();
        bool railed   = (raw <= -8380000 || raw >= 8380000);
        bool floating = (raw > -50000 && raw < 50000);
        if (!railed && !floating) pedalOk = true;
      }
      // allow dismiss via any encoder input
      if (digitalRead(ENC_SW[0]) == LOW ||
          digitalRead(ENC_SW[1]) == LOW ||
          digitalRead(ENC_SW[2]) == LOW) {
        pedalOk = true;  // user chose to proceed anyway
        break;
      }
    }

    pedalDetected = true;
    appMode = preOverlayMode;
    display.clearDisplay();
  }

  for (int i = 0; i < 3; i++) {
    pinMode(ENC_CLK[i], INPUT_PULLUP);
    pinMode(ENC_DT[i],  INPUT_PULLUP);
    pinMode(ENC_SW[i],  INPUT_PULLUP);
    lastCLK[i]    = digitalRead(ENC_CLK[i]);
    lastDT[i]     = digitalRead(ENC_DT[i]);
    lastSW[i]     = HIGH;
    swHeld[i]     = false;
    swConsumed[i] = false;
    encState[i]   = 0;
  }

  strip.setPixelColor(0, strip.Color(0, 0, 255));
  strip.show();
  needsRedraw = true;
}

// ═══════════════════════════════════════════════════════════════════
// SECTION 28 — loop()
// ═══════════════════════════════════════════════════════════════════
void loop() {

  handleEncoders();

  if (scale.is_ready()) {
    long raw = scale.read();
    checkPedalDetection(raw);

    long rawSpan = constrain(tareValue - raw, 0, forceMax);

    // Deadband: treat anything within ~2% of forceMax as true zero.
    // This absorbs normal ADC noise at rest, preventing it from
    // constantly (and needlessly) triggering redraws — which was
    // stealing loop() time from encoder polling and making the
    // encoders feel laggy specifically while resting near zero.
    long deadband = (long)(forceMax * 0.05f);
    if (rawSpan < deadband) rawSpan = 0;

    lastRawSpan  = rawSpan;
    float newForce = (float)rawSpan / (float)max(forceMax,(long)1);
    newForce = constrain(newForce, 0.0f, 1.0f);
    newForce = applySmoothing(newForce);
    if (abs(newForce - currentForce) > 0.005f) {
      currentForce = newForce;
      needsRedraw  = true;
    }
    if (appMode == CALIBRATION && calZeroed) {
      long rawAbsSpan = abs(raw - tareValue);
      lastCalRawAbs = rawAbsSpan;
      if (rawAbsSpan > calPeak) { calPeak = rawAbsSpan; needsRedraw = true; }
    }
  }

  updateJoystick();  // back to every loop iteration, no throttle needed — own endpoint now

  if ((!isCalibrated || profileDirty) && appMode == NORMAL) {
    static unsigned long lastFlip = 0;
    if (millis() - lastFlip > 500) {
      lastFlip    = millis();
      needsRedraw = true;
    }
  }

  static bool wasHighlighting = false;
  bool isHighlighting = (millis() - lastInputTime < HIGHLIGHT_TIMEOUT);
  if (wasHighlighting && !isHighlighting) needsRedraw = true;
  wasHighlighting = isHighlighting;

  // throttle display updates to ~30fps max, decoupled from input rate
  static unsigned long lastDisplayUpdate = 0;
  if (needsRedraw && millis() - lastDisplayUpdate >= 20) {
    lastDisplayUpdate = millis();
    redraw();
  }

  static unsigned long lastFbChunk = 0;
  if (millis() - lastFbChunk >= 1) {
    lastFbChunk = millis();
    streamFramebufferChunk();
  }
}