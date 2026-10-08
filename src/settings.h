#pragma once
#include <stdint.h>

enum Eol : uint8_t { EOL_CR, EOL_LF, EOL_CRLF, EOL_NONE, EOL_COUNT };

struct Settings {
  uint32_t baud = 115200;
  uint8_t autoBaud = 1;
  uint8_t frame = 0;      // index into FRAMES
  uint8_t swapPins = 0;   // 0: RX=G1 TX=G2, 1: RX=G2 TX=G1
  uint8_t rawMode = 0;    // 0: line mode, 1: raw keystrokes
  uint8_t eol = EOL_CR;
  uint8_t localEcho = 1;
  uint8_t logEnabled = 0;
  uint8_t logTimestamps = 1;
  uint8_t brightness = 160;
  uint16_t logCounter = 0;
};

extern Settings cfg;

void settingsLoad();
void settingsSave();
