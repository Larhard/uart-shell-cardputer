#include "keyboard.h"

#include <Arduino.h>
#include <Wire.h>

namespace kbd {
namespace {

constexpr uint8_t ADDR = 0x34;
constexpr int PIN_SDA = 8, PIN_SCL = 9, PIN_INT = 11;
constexpr uint8_t REG_CFG = 0x01, REG_INT_STAT = 0x02, REG_KEY_LCK_EC = 0x03, REG_KEY_EVENT_A = 0x04,
                  REG_KP_GPIO1 = 0x1D, REG_KP_GPIO2 = 0x1E, REG_KP_GPIO3 = 0x1F;
constexpr uint32_t REPEAT_DELAY = 400, REPEAT_RATE = 50;

// Modifier markers inside the layout table.
constexpr uint8_t M_FN = 0xF0, M_SHIFT = 0xF1, M_CTRL = 0xF2, M_ALT = 0xF3, M_OPT = 0xF4;

// Layout as seen on the keycaps: {normal, shifted, fn}. 4 rows x 14 columns.
struct KV { uint8_t n, s, f; };
const KV LAYOUT[4][14] = {
  {{'`', '~', K_ESC}, {'1', '!', K_F1}, {'2', '@', K_F1 + 1}, {'3', '#', K_F1 + 2}, {'4', '$', K_F1 + 3},
   {'5', '%', K_F1 + 4}, {'6', '^', K_F1 + 5}, {'7', '&', K_F1 + 6}, {'8', '*', K_F1 + 7}, {'9', '(', K_F1 + 8},
   {'0', ')', K_F1 + 9}, {'-', '_', K_F1 + 10}, {'=', '+', K_F1 + 11}, {K_BKSP, K_BKSP, K_DEL}},
  {{K_TAB, K_TAB, 0}, {'q', 'Q', 0}, {'w', 'W', 0}, {'e', 'E', 0}, {'r', 'R', 0}, {'t', 'T', 0},
   {'y', 'Y', 0}, {'u', 'U', 0}, {'i', 'I', 0}, {'o', 'O', 0}, {'p', 'P', 0}, {'[', '{', 0},
   {']', '}', 0}, {'\\', '|', 0}},
  {{M_FN, M_FN, M_FN}, {M_SHIFT, M_SHIFT, M_SHIFT}, {'a', 'A', 0}, {'s', 'S', 0}, {'d', 'D', 0},
   {'f', 'F', 0}, {'g', 'G', 0}, {'h', 'H', 0}, {'j', 'J', 0}, {'k', 'K', 0}, {'l', 'L', 0},
   {';', ':', K_UP}, {'\'', '"', 0}, {K_ENTER, K_ENTER, K_ENTER}},
  {{M_CTRL, M_CTRL, M_CTRL}, {M_OPT, M_OPT, M_OPT}, {M_ALT, M_ALT, M_ALT}, {'z', 'Z', 0}, {'x', 'X', 0},
   {'c', 'C', 0}, {'v', 'V', 0}, {'b', 'B', 0}, {'n', 'N', 0}, {'m', 'M', 0}, {',', '<', K_LEFT},
   {'.', '>', K_DOWN}, {'/', '?', K_RIGHT}, {' ', ' ', ' '}},
};

bool ok = false;
uint8_t mods = 0;
Key queue[16];
uint8_t qHead = 0, qTail = 0;
uint8_t repeatEvent = 0;  // raw TCA key number being held (0 = none)
Key repeatKey;
uint32_t repeatAt = 0;
uint32_t lastPoll = 0;

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(ADDR, (uint8_t)1) != 1) return 0;
  return Wire.read();
}

void push(const Key &k) {
  uint8_t next = (qHead + 1) & 15;
  if (next == qTail) return;  // full, drop
  queue[qHead] = k;
  qHead = next;
}

// TCA8418 key number (1-based, row*10+col) -> layout position (same remap as M5Cardputer).
const KV &lookup(uint8_t num) {
  uint8_t idx = num - 1;
  uint8_t r = idx / 10, c = idx % 10;
  uint8_t x = r * 2 + (c > 3 ? 1 : 0);
  uint8_t y = c % 4;
  if (x > 13 || y > 3) return LAYOUT[0][0];
  return LAYOUT[y][x];
}

uint8_t modBit(uint8_t m) {
  switch (m) {
    case M_FN: return MOD_FN;
    case M_SHIFT: return MOD_SHIFT;
    case M_CTRL: return MOD_CTRL;
    case M_ALT: return MOD_ALT;
    case M_OPT: return MOD_OPT;
  }
  return 0;
}

void handleEvent(uint8_t ev) {
  bool down = ev & 0x80;
  uint8_t num = ev & 0x7F;
  if (num == 0 || num > 80) return;
  const KV &kv = lookup(num);

  if (uint8_t bit = modBit(kv.n)) {
    if (down) {
      mods |= bit;
      if (bit == MOD_OPT) push({K_OPT, mods, K_OPT});
    } else {
      mods &= ~bit;
    }
    return;
  }

  if (!down) {
    if (repeatEvent == num) repeatEvent = 0;
    return;
  }

  Key k;
  k.base = kv.n;
  k.mods = mods;
  if ((mods & MOD_FN) && kv.f)
    k.code = kv.f;
  else if (mods & MOD_SHIFT)
    k.code = kv.s;
  else
    k.code = kv.n;
  push(k);
  repeatEvent = num;
  repeatKey = k;
  repeatAt = millis() + REPEAT_DELAY;
}

}  // namespace

bool begin() {
  pinMode(PIN_INT, INPUT_PULLUP);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Wire.beginTransmission(ADDR);
  ok = Wire.endTransmission() == 0;
  if (!ok) return false;
  writeReg(REG_KP_GPIO1, 0x7F);  // 7 rows
  writeReg(REG_KP_GPIO2, 0xFF);  // 8 columns
  writeReg(REG_KP_GPIO3, 0x00);
  for (int i = 0; i < 16 && readReg(REG_KEY_EVENT_A); i++) {}
  writeReg(REG_INT_STAT, 0x03);
  writeReg(REG_CFG, 0x01);  // key event interrupt enable
  return true;
}

void poll() {
  if (!ok) return;
  uint32_t now = millis();
  // INT is active-low; also poll occasionally in case an edge was missed.
  if (digitalRead(PIN_INT) == LOW || now - lastPoll > 100) {
    lastPoll = now;
    uint8_t n = readReg(REG_KEY_LCK_EC) & 0x0F;
    while (n--) {
      uint8_t ev = readReg(REG_KEY_EVENT_A);
      if (!ev) break;
      handleEvent(ev);
    }
    writeReg(REG_INT_STAT, 0x01);
  }
  if (repeatEvent && (int32_t)(now - repeatAt) >= 0) {
    push(repeatKey);
    repeatAt = now + REPEAT_RATE;
  }
}

bool get(Key &k) {
  if (qHead == qTail) return false;
  k = queue[qTail];
  qTail = (qTail + 1) & 15;
  return true;
}

}  // namespace kbd
