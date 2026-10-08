#pragma once
#include <stdint.h>

// Scrollback store: fixed-width, already-wrapped lines in a ring buffer.
// Cell byte: bit7 = TX, low 7 bits = ASCII 0x20..0x7E, or 0x7F for a non-printable byte.
namespace term {

constexpr int COLS = 40;
constexpr uint32_t LINES = 2000;
constexpr uint8_t CELL_TX = 0x80;
constexpr uint8_t NONPRINT = 0x7F;

struct Line {
  uint8_t len;
  uint8_t info;  // status/info line drawn in its own color
  uint8_t c[COLS];
};

void put(uint8_t cell);
void newline();
void backspace();
void info(const char *s);
void clear();

uint32_t last();  // absolute index of the current (bottom) line
uint32_t first();  // oldest absolute index still stored
const Line &at(uint32_t abs);
const Line &cur();
extern uint32_t version;  // bumps on every change

}  // namespace term
