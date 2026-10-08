#include "sdlog.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "settings.h"

namespace sdlog {
namespace {

constexpr int PIN_SCK = 40, PIN_MISO = 39, PIN_MOSI = 14, PIN_CS = 12;
constexpr const char *DIR = "/uart_logs";
constexpr size_t BUF = 4096;
constexpr uint32_t SYNC_MS = 2000;

SPIClass sdSpi(FSPI);
bool mounted = false;
File file;
bool open = false;
char name[40];
char buf[BUF];
size_t len = 0;
bool lineStart = true;  // next RX char begins a new line
uint32_t t0 = 0, lastSync = 0;

void flushBuf() {
  if (len) {
    file.write((const uint8_t *)buf, len);
    len = 0;
  }
}

void put(const char *s, size_t n) {
  if (len + n > BUF) flushBuf();
  if (n > BUF) {
    file.write((const uint8_t *)s, n);
    return;
  }
  memcpy(buf + len, s, n);
  len += n;
}

void put(const char *s) { put(s, strlen(s)); }

void stamp() {
  if (!cfg.logTimestamps) return;
  uint32_t ms = millis() - t0;
  char ts[24];
  int n = snprintf(ts, sizeof ts, "[%02lu:%02lu:%02lu.%03lu] ", (unsigned long)(ms / 3600000),
                   (unsigned long)(ms / 60000 % 60), (unsigned long)(ms / 1000 % 60),
                   (unsigned long)(ms % 1000));
  put(ts, n);
}

void endRxLine() {
  if (!lineStart) {
    put("\n", 1);
    lineStart = true;
  }
}

}  // namespace

bool start(const char *header) {
  if (open) return true;
  if (!mounted) {
    sdSpi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
    mounted = SD.begin(PIN_CS, sdSpi, 25000000);
    if (!mounted) {
      sdSpi.end();
      return false;
    }
  }
  if (!SD.exists(DIR)) SD.mkdir(DIR);
  for (int tries = 0; tries < 10000; tries++) {
    cfg.logCounter = cfg.logCounter % 9999 + 1;
    snprintf(name, sizeof name, "%s/uart_%04u.txt", DIR, cfg.logCounter);
    if (!SD.exists(name)) break;
  }
  file = SD.open(name, FILE_WRITE);
  if (!file) {
    SD.end();
    sdSpi.end();
    mounted = false;
    return false;
  }
  open = true;
  len = 0;
  lineStart = true;
  t0 = lastSync = millis();
  note(header);
  return true;
}

void stop() {
  if (!open) return;
  endRxLine();
  flushBuf();
  file.close();
  open = false;
}

bool active() { return open; }

const char *fileName() { return open ? name + strlen(DIR) + 1 : ""; }

void rx(uint8_t c) {
  if (!open) return;
  if (c == '\n') {
    if (lineStart) stamp();
    put("\n", 1);
    lineStart = true;
    return;
  }
  if (lineStart) {
    stamp();
    lineStart = false;
  }
  if ((c >= 0x20 && c < 0x7F) || c == '\t') {
    char ch = c;
    put(&ch, 1);
  } else {
    char esc[5];
    snprintf(esc, sizeof esc, "\\x%02X", c);
    put(esc, 4);
  }
}

void tx(const char *s, size_t n) {
  if (!open) return;
  endRxLine();
  stamp();
  put("> ", 2);
  put(s, n);
  put("\n", 1);
}

void note(const char *s) {
  if (!open) return;
  endRxLine();
  put("# ");
  put(s);
  put("\n", 1);
}

void poll() {
  if (!open) return;
  uint32_t now = millis();
  if (len >= BUF / 2 || (len && now - lastSync >= SYNC_MS)) flushBuf();
  if (now - lastSync >= SYNC_MS) {
    file.flush();
    lastSync = now;
  }
}

}  // namespace sdlog
