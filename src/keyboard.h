#pragma once
#include <stdint.h>

// Minimal driver for the Cardputer ADV keyboard (TCA8418 over I2C).
namespace kbd {

enum : uint8_t {
  MOD_CTRL = 1,
  MOD_SHIFT = 2,
  MOD_ALT = 4,
  MOD_FN = 8,
  MOD_OPT = 16,
};

// Key codes: printable ASCII as-is, plus the following.
enum : uint8_t {
  K_BKSP = 0x08,
  K_TAB = 0x09,
  K_ENTER = 0x0D,
  K_ESC = 0x1B,
  K_DEL = 0x7F,
  K_UP = 0x80,
  K_DOWN,
  K_LEFT,
  K_RIGHT,
  K_OPT,
  K_F1,  // K_F1 + n for F1..F12
};

struct Key {
  uint8_t code;
  uint8_t mods;   // modifiers held when the key went down
  uint8_t base;   // unshifted key value (e.g. 'c' for Ctrl+C)
};

bool begin();
void poll();
bool get(Key &k);

}  // namespace kbd
