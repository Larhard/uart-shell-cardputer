#pragma once
#include <stdint.h>

#include "settings.h"

// Connection presets: built-in groups plus user presets kept on the SD card in
// /uart_presets.txt (INI style, editable on a PC).
namespace presets {

// Frame indices, same order as FRAMES in main.cpp.
enum Frame : uint8_t { F_8N1, F_8E1, F_8O1, F_8N2, F_7E1, F_7O1, F_COUNT };
extern const char *const FRAME_NAMES[F_COUNT];

enum PortSel : uint8_t { KEEP, GROVE, USB };  // port a preset switches to

struct Preset {
  const char *name;   // picker label
  const char *shortName;  // status bar, < PRESET_NAME_LEN chars
  uint32_t baud;
  uint8_t frame;      // Frame
  uint8_t eol;        // Eol
  uint8_t raw;        // 0 line mode, 1 raw keys
  uint8_t echo;       // EchoMode
  uint8_t dtr, rts;
  uint8_t hex, flow;
  uint8_t port;       // PortSel
  const char *note;   // shown when applied (wiring, levels, example commands); may be null
};

struct Group {
  const char *name;
  const Preset *items;
  uint8_t count;
};

extern const Group GROUPS[];
extern const int GROUP_COUNT;

// User presets (the "My presets" group).
constexpr int USER_MAX = 16;
struct UserPreset {
  char name[24];
  char shortName[PRESET_NAME_LEN];
  char note[48];
  Preset p;  // name/shortName/note point into the fields above
};

// Reads /uart_presets.txt. Returns the number of presets, or -1 without an SD card.
int loadUser(UserPreset *out, int max);
// Appends one preset to /uart_presets.txt.
bool saveUser(const char *name, const Preset &p);

}  // namespace presets
