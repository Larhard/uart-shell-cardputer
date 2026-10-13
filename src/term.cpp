#include "term.h"

#include <string.h>

namespace term {

static Line lines[LINES];
static uint32_t lastIdx = 0;
uint32_t version = 0;

static Line &curMut() { return lines[lastIdx % LINES]; }

void newline() {
  lastIdx++;
  Line &l = curMut();
  l.len = 0;
  l.info = 0;
  version++;
}

void put(uint8_t cell) {
  if (curMut().len >= COLS || curMut().info) newline();
  Line &l = curMut();
  l.c[l.len++] = cell;
  version++;
}

void backspace() {
  Line &l = curMut();
  if (l.len && !l.info) {
    l.len--;
    version++;
  }
}

void info(const char *s) {
  if (curMut().len) newline();
  size_t n = strlen(s);
  do {
    // Wrap at the last space that fits; a single word longer than a line is split.
    size_t take = n;
    if (n > (size_t)COLS) {
      take = COLS;
      while (take && s[take] != ' ') take--;
      if (!take) take = COLS;
    }
    Line &l = curMut();
    l.info = 1;
    l.len = take;
    memcpy(l.c, s, take);
    s += take;
    n -= take;
    while (n && *s == ' ') s++, n--;
    newline();
  } while (n);
}

void clear() {
  lastIdx = 0;
  lines[0].len = 0;
  lines[0].info = 0;
  version++;
}

uint32_t last() { return lastIdx; }
uint32_t first() { return lastIdx >= LINES ? lastIdx - LINES + 1 : 0; }
const Line &at(uint32_t abs) { return lines[abs % LINES]; }
const Line &cur() { return lines[lastIdx % LINES]; }

}  // namespace term
