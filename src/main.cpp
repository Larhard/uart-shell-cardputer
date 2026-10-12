// UART Shell for M5Stack Cardputer ADV
#include <Arduino.h>

#include "display.h"
#include "keyboard.h"
#include "sdlog.h"
#include "settings.h"
#include "term.h"
#include "usbserial.h"

#define APP_VERSION "1.2"

using kbd::Key;

// ---------------------------------------------------------------------------
// Layout and colors
// ---------------------------------------------------------------------------
constexpr int W = 240, H = 135, CW = 6, CH = 8;
constexpr int STATUS_H = 10;
constexpr int TERM_Y = 11;              // terminal sprite covers y 11..134
constexpr int TERM_H = H - TERM_Y;      // 124
constexpr int ROWS_RAW = 15, ROWS_LINE = 14;
constexpr int INPUT_SY = TERM_H - CH;   // input row inside the sprite (screen y 127)
constexpr int INPUT_MAX = 127;

constexpr uint16_t C_BG = 0x0000;
constexpr uint16_t C_RX = 0x9FF3;       // light green
constexpr uint16_t C_TX = 0xFE41;       // amber
constexpr uint16_t C_INFO = 0x5D1F;     // sky blue
constexpr uint16_t C_DIM = 0x632C;      // grey
constexpr uint16_t C_TEXT = 0xE71C;     // near white
constexpr uint16_t C_BAR = 0x18E3;      // dark grey
constexpr uint16_t C_RED = 0xF9A6;
constexpr uint16_t C_SEL = 0x2A8B;      // menu highlight

static LGFX lcd;
static LGFX_Sprite canvas(&lcd);
static LGFX_Sprite bar(&lcd);

// ---------------------------------------------------------------------------
// UART
// ---------------------------------------------------------------------------
constexpr int PIN_G1 = 1, PIN_G2 = 2;
static HardwareSerial &uart = Serial1;

// bits/parity/stop are used for USB adapters (parity: 0 none, 1 odd, 2 even).
struct Frame { uint32_t cfg; const char *name; uint8_t bits, parity, stop; };
static const Frame FRAMES[] = {
  {SERIAL_8N1, "8N1", 8, 0, 1}, {SERIAL_8E1, "8E1", 8, 2, 1}, {SERIAL_8O1, "8O1", 8, 1, 1},
  {SERIAL_8N2, "8N2", 8, 0, 2}, {SERIAL_7E1, "7E1", 7, 2, 1}, {SERIAL_7O1, "7O1", 7, 1, 1},
};
constexpr int FRAME_COUNT = sizeof(FRAMES) / sizeof(FRAMES[0]);

static const uint32_t BAUDS[] = {300, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 38400, 57600,
                                 74880, 115200, 230400, 250000, 460800, 500000, 921600, 1000000,
                                 1500000, 2000000};
constexpr int BAUD_COUNT = sizeof(BAUDS) / sizeof(BAUDS[0]);

// Scan order for auto detection: most common rates first.
static const uint32_t SCAN_BAUDS[] = {115200, 9600, 57600, 38400, 19200, 230400, 460800, 921600,
                                      74880, 4800, 2400, 1200, 250000, 500000, 1000000, 1500000,
                                      2000000, 14400, 28800};
constexpr int SCAN_COUNT = sizeof(SCAN_BAUDS) / sizeof(SCAN_BAUDS[0]);

static const char *EOL_NAMES[] = {"CR", "LF", "CRLF", "None"};
static const char *EOL_BYTES[] = {"\r", "\n", "\r\n", ""};

static uint32_t lastRxMs = 0, lastTxMs = 0;

// The serial link is either the Grove UART or a USB-serial adapter on the USB-C port.
static bool usbPort() { return cfg.port == PORT_USB; }

static void linkBegin() {
  const Frame &f = FRAMES[cfg.frame];
  uart.end();
  if (usbPort()) {
    usbserial::begin();
    usbserial::setLine(cfg.baud, f.bits, f.parity, f.stop);
    usbserial::setLines(cfg.dtr, cfg.rts);
  } else {
    uart.setRxBufferSize(16384);
    uart.begin(cfg.baud, f.cfg, cfg.swapPins ? PIN_G2 : PIN_G1, cfg.swapPins ? PIN_G1 : PIN_G2);
  }
}

static void linkSetBaud(uint32_t b) {
  if (usbPort()) usbserial::setBaud(b);
  else uart.updateBaudRate(b);
}

static size_t linkRead(uint8_t *buf, size_t n) {
  if (usbPort()) return usbserial::read(buf, n);
  int avail = uart.available();
  if (avail <= 0) return 0;
  return uart.read(buf, (size_t)avail < n ? (size_t)avail : n);
}

static void linkWrite(const uint8_t *buf, size_t n) {
  if (usbPort()) usbserial::write(buf, n);
  else uart.write(buf, n);
}

static void linkFlushInput() {
  if (usbPort()) usbserial::flushInput();
  else
    while (uart.available()) uart.read();
}

static void portDesc(char *out, size_t sz) {
  if (usbPort()) snprintf(out, sz, "USB %s", usbserial::connected() ? usbserial::name() : "(none)");
  else snprintf(out, sz, "RX=G%d TX=G%d", cfg.swapPins ? 2 : 1, cfg.swapPins ? 1 : 2);
}

static void logConfigNote() {
  char s[64], port[24];
  portDesc(port, sizeof port);
  snprintf(s, sizeof s, "%lu %s %s", (unsigned long)cfg.baud, FRAMES[cfg.frame].name, port);
  sdlog::note(s);
}

// ---------------------------------------------------------------------------
// View state
// ---------------------------------------------------------------------------
static bool follow = true;       // view tracks the newest line
static uint32_t viewBottom = 0;  // bottom line when not following
static bool menuOpen = false;
static bool needFull = true;     // force full redraw
static uint32_t drawnVersion = 0;
static bool inputDirty = true;
static uint32_t lastFrame = 0;

static int termRows() { return cfg.rawMode ? ROWS_RAW : ROWS_LINE; }

static void jumpToEnd() {
  if (follow) return;
  follow = true;
  needFull = true;
}

static void info(const char *fmt, ...) {
  char s[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s, sizeof s, fmt, ap);
  va_end(ap);
  term::info(s);
}

// ---------------------------------------------------------------------------
// Auto baud: try rates until received bytes look like text.
// Only started on request (Fn+B); the result is saved so the next launch is instant.
// ---------------------------------------------------------------------------
static bool looksLikeText(uint8_t b) {
  return (b >= 0x20 && b < 0x7F) || b == '\r' || b == '\n' || b == '\t' || b == 0x08 ||
         b == 0x1B || b == 0x07;
}

struct {
  bool on = false;
  int idx = 0;
  uint32_t winStart = 0;
  uint16_t n = 0, good = 0;
  int bestIdx = -1;
  uint16_t bestScore = 0;
  uint8_t passes = 0;  // full passes that saw data
  bool passHadData = false;
  uint32_t prevBaud = 0;
} scan;

static void scanSet(int idx) {
  scan.idx = idx;
  linkSetBaud(SCAN_BAUDS[idx]);
  linkFlushInput();
  scan.winStart = millis();
  scan.n = scan.good = 0;
}

static void scanStart() {
  if (scan.on) return;
  scan.on = true;
  scan.prevBaud = cfg.baud;
  scan.bestIdx = -1;
  scan.bestScore = 0;
  scan.passes = 0;
  scan.passHadData = false;
  scanSet(0);
  info("[auto] detecting baud rate (Fn+` cancels)");
}

static void scanFinish(uint32_t baud, bool found) {
  scan.on = false;
  cfg.baud = baud;
  linkSetBaud(baud);
  if (found) {
    cfg.autoBaud = 1;
    settingsSave();
    info("[auto] locked at %lu baud", (unsigned long)baud);
    logConfigNote();
  } else {
    info("[auto] no readable text, keeping %lu", (unsigned long)baud);
  }
}

static void scanCancel() {
  if (!scan.on) return;
  scanFinish(scan.prevBaud, false);
}

static void scanByte(uint8_t b) {
  if (millis() - scan.winStart < 3) return;  // skip bytes torn by the rate switch
  scan.n++;
  if (looksLikeText(b)) scan.good++;
}

static void scanTick() {
  if (!scan.on) return;
  uint32_t el = millis() - scan.winStart;
  if (scan.n < 96 && el < 300) return;
  if (scan.n >= 16) {
    scan.passHadData = true;
    uint16_t score = (uint32_t)scan.good * 1000 / scan.n;
    if (score >= 980) return scanFinish(SCAN_BAUDS[scan.idx], true);
    if (score > scan.bestScore) {
      scan.bestScore = score;
      scan.bestIdx = scan.idx;
    }
  }
  int next = scan.idx + 1;
  if (next == SCAN_COUNT) {
    next = 0;
    if (scan.passHadData) {
      if (scan.bestScore >= 900) return scanFinish(SCAN_BAUDS[scan.bestIdx], true);
      if (++scan.passes >= 2) return scanFinish(scan.prevBaud, false);
    }
    scan.passHadData = false;
  }
  scanSet(next);
}

// ---------------------------------------------------------------------------
// RX path: strip ANSI escapes, normalise line endings, hide our own echo.
// ---------------------------------------------------------------------------
enum AnsiState : uint8_t { A_NONE, A_ESC, A_CSI, A_OSC, A_OSC_ESC };
static AnsiState ansi = A_NONE;
static uint8_t lastNl = 0;  // '\r'/'\n' that produced the last newline; 1 = local TX newline

static uint8_t echoQ[256];
static uint8_t eqHead = 0, eqTail = 0;
static uint32_t echoDeadline = 0;

enum EchoMode : uint8_t { ECHO_OFF, ECHO_HIDE, ECHO_SHOW, ECHO_COUNT };
static const char *ECHO_NAMES[] = {"Off", "On, hide echo", "On, show all"};

static bool echoSwallow(uint8_t b) {
  if (cfg.localEcho != ECHO_HIDE || eqHead == eqTail) return false;
  if ((int32_t)(millis() - echoDeadline) > 0) {
    eqTail = eqHead;
    return false;
  }
  if (b == echoQ[eqTail]) {
    eqTail++;
    return true;
  }
  if (b == '\r' && echoQ[eqTail] == '\n') return true;  // tty turned LF into CRLF
  eqTail = eqHead;  // not an echo, stop filtering
  return false;
}

static void rxByte(uint8_t b) {
  if (echoSwallow(b)) return;

  switch (ansi) {
    case A_ESC: ansi = b == '[' ? A_CSI : b == ']' ? A_OSC : A_NONE; return;
    case A_CSI: if (b >= 0x40 && b <= 0x7E) ansi = A_NONE; return;
    case A_OSC: if (b == 0x07) ansi = A_NONE; else if (b == 0x1B) ansi = A_OSC_ESC; return;
    case A_OSC_ESC: ansi = A_NONE; return;
    default: break;
  }

  if (b == '\r' || b == '\n') {
    if (lastNl && (lastNl == 1 || lastNl != b)) {  // second half of CRLF / LFCR
      lastNl = 0;
      return;
    }
    lastNl = b;
    term::newline();
    sdlog::rx('\n');
    return;
  }
  lastNl = 0;

  if (b == 0x1B) {
    ansi = A_ESC;
  } else if (b == 0x08) {
    term::backspace();
  } else if (b == '\t') {
    do term::put(' ');
    while (term::cur().len % 8);
    sdlog::rx(b);
  } else if (b == 0x07 || b == 0) {
    // bell / padding
  } else if (b >= 0x20 && b < 0x7F) {
    term::put(b);
    sdlog::rx(b);
  } else {
    term::put(term::NONPRINT);
    sdlog::rx(b);
  }
}

static void pumpUart() {
  uint8_t buf[256];
  size_t n;
  while ((n = linkRead(buf, sizeof buf)) > 0) {
    lastRxMs = millis();
    for (size_t i = 0; i < n; i++) {
      if (scan.on) scanByte(buf[i]);
      else rxByte(buf[i]);
    }
  }
}

// ---------------------------------------------------------------------------
// TX path
// ---------------------------------------------------------------------------
static char input[INPUT_MAX + 1];
static uint8_t inputLen = 0;
static char rawLine[INPUT_MAX + 1];  // raw-mode keystrokes collected for the log
static uint8_t rawLen = 0;

constexpr int HIST = 8;
static char hist[HIST][INPUT_MAX + 1];
static uint8_t histCount = 0;
static int histPos = -1;

static void send(const char *s, size_t n, bool expectEcho) {
  if (!n) return;
  linkWrite((const uint8_t *)s, n);
  lastTxMs = millis();
  jumpToEnd();
  if (!expectEcho || cfg.localEcho != ECHO_HIDE) return;
  if ((int32_t)(millis() - echoDeadline) > 0) eqTail = eqHead;  // drop bytes that never came back
  for (size_t i = 0; i < n; i++) {
    uint8_t c = s[i];
    if ((c >= 0x20 && c < 0x7F) || c == '\r' || c == '\n') {
      if ((uint8_t)(eqHead + 1) == eqTail) break;
      echoQ[eqHead++] = c;
    }
  }
  echoDeadline = millis() + 1000;
}

static void send(const char *s) { send(s, strlen(s), false); }

static void echoText(const char *s, size_t n) {
  if (!cfg.localEcho) return;
  for (size_t i = 0; i < n; i++) {
    uint8_t c = s[i];
    term::put(((c >= 0x20 && c < 0x7F) ? c : ' ') | term::CELL_TX);
  }
}

static void echoNewline() {
  if (!cfg.localEcho || cfg.eol == EOL_NONE) return;
  term::newline();
  lastNl = 1;
}

static void sendEol() { send(EOL_BYTES[cfg.eol], strlen(EOL_BYTES[cfg.eol]), true); }

static void sendLine() {
  send(input, inputLen, true);
  sendEol();
  echoText(input, inputLen);
  echoNewline();
  sdlog::tx(input, inputLen);
  if (inputLen && (histCount == 0 || strcmp(hist[0], input) != 0)) {
    memmove(hist[1], hist[0], sizeof(hist[0]) * (HIST - 1));
    memcpy(hist[0], input, inputLen + 1);
    if (histCount < HIST) histCount++;
  }
  histPos = -1;
  inputLen = 0;
  input[0] = 0;
  inputDirty = true;
}

static void sendCtrl(uint8_t c) {
  char b = c;
  send(&b, 1, false);
  char s[3] = {'^', (char)(c == 0x7F ? '?' : c + 0x40), 0};
  sdlog::tx(s, 2);
}

static void histRecall(int dir) {
  int p = histPos + dir;
  if (p < -1 || p >= histCount) return;
  histPos = p;
  if (p < 0) {
    inputLen = 0;
  } else {
    inputLen = strlen(hist[p]);
    memcpy(input, hist[p], inputLen);
  }
  input[inputLen] = 0;
  inputDirty = true;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
static uint16_t cellColor(uint8_t c) {
  if ((c & 0x7F) == term::NONPRINT) return C_DIM;
  return (c & term::CELL_TX) ? C_TX : C_RX;
}

static void drawLine(const term::Line &l, int y) {
  char run[term::COLS + 1];
  int i = 0;
  while (i < l.len) {
    uint16_t color = l.info ? C_INFO : cellColor(l.c[i]);
    int start = i, n = 0;
    while (i < l.len && (l.info || cellColor(l.c[i]) == color)) {
      uint8_t ch = l.c[i++] & 0x7F;
      run[n++] = ch == term::NONPRINT ? '.' : ch;
    }
    run[n] = 0;
    canvas.setTextColor(color);
    canvas.drawString(run, start * CW, y);
  }
}

static void clampView() {
  int rows = termRows();
  uint32_t minBottom = term::first() + rows - 1;
  if (viewBottom < minBottom) viewBottom = minBottom;
  if (viewBottom >= term::last()) follow = true;
}

static void scrollBy(int delta) {
  int rows = termRows();
  if (term::last() - term::first() + 1 <= (uint32_t)rows && delta < 0) return;
  if (follow) {
    if (delta > 0) return;
    viewBottom = term::last();
    follow = false;
  }
  if (delta < 0 && (uint32_t)-delta > viewBottom) viewBottom = 0;
  else viewBottom += delta;
  clampView();
  needFull = true;
}

static void drawTerm() {
  int rows = termRows();
  canvas.fillScreen(C_BG);
  uint32_t bottom = follow ? term::last() : viewBottom;
  uint32_t first = term::first();
  for (int r = rows - 1; r >= 0; r--) {
    uint32_t back = rows - 1 - r;
    if (back > bottom || bottom - back < first) break;
    drawLine(term::at(bottom - back), r * CH);
  }
  if (follow && cfg.rawMode) {  // cursor
    int x = term::cur().len;
    int y = (rows - 1) * CH;
    if (x >= term::COLS) x = term::COLS - 1;
    canvas.fillRect(x * CW, y + CH - 1, CW, 1, C_TX);
  }
  if (!cfg.rawMode) {
    canvas.drawFastHLine(0, INPUT_SY - 3, W, C_BAR);
    canvas.setTextColor(C_DIM);
    canvas.drawString(">", 0, INPUT_SY);
    int vis = term::COLS - 3;
    int start = inputLen > vis ? inputLen - vis : 0;
    char tmp[INPUT_MAX + 1];
    int n = 0;
    for (int i = start; i < inputLen; i++) tmp[n++] = input[i] == '\t' ? ' ' : input[i];
    tmp[n] = 0;
    canvas.setTextColor(C_TX);
    canvas.drawString(tmp, 2 * CW, INPUT_SY);
    canvas.fillRect((2 + n) * CW, INPUT_SY, CW, CH, C_TX);
  }
  canvas.pushSprite(0, TERM_Y);
}

static char lastStatus[80];

static void drawStatus(bool force) {
  bool rxOn = millis() - lastRxMs < 80, txOn = millis() - lastTxMs < 80;
  char baud[20];
  if (scan.on)
    snprintf(baud, sizeof baud, "SCAN %lu", (unsigned long)SCAN_BAUDS[scan.idx]);
  else
    snprintf(baud, sizeof baud, "%s%lu", cfg.autoBaud ? "A:" : "", (unsigned long)cfg.baud);
  const char *logTxt = sdlog::active() ? "LOG" : (cfg.logEnabled ? "NOSD" : "");
  char scroll[12] = "";
  if (!follow) snprintf(scroll, sizeof scroll, "^%lu", (unsigned long)(term::last() - viewBottom));

  char sig[80];
  char port[12];
  if (usbPort()) snprintf(port, sizeof port, "U:%s", usbserial::connected() ? usbserial::name() : "--");
  else snprintf(port, sizeof port, "RX:G%d", cfg.swapPins ? 2 : 1);
  const char *lines = !usbPort() ? "" : cfg.dtr && cfg.rts ? "DR" : cfg.dtr ? "D" : cfg.rts ? "R" : "";
  snprintf(sig, sizeof sig, "%s|%s|%s|%s|%d|%d|%s|%s|%d%d", baud, FRAMES[cfg.frame].name, port, lines,
           cfg.rawMode, cfg.eol, logTxt, scroll, rxOn, txOn);
  if (!force && strcmp(sig, lastStatus) == 0) return;
  strcpy(lastStatus, sig);

  bar.fillScreen(C_BAR);
  int x = 2;
  auto seg = [&](const char *s, uint16_t color) {
    if (!*s) return;
    bar.setTextColor(color);
    bar.drawString(s, x, 1);
    x += (strlen(s) + 1) * CW;
  };
  seg(baud, scan.on ? C_TX : C_TEXT);
  seg(FRAMES[cfg.frame].name, C_DIM);
  seg(port, usbPort() && !usbserial::connected() ? C_RED : C_DIM);
  seg(lines, C_TX);
  seg(cfg.rawMode ? "RAW" : "LINE", C_INFO);
  seg(EOL_NAMES[cfg.eol], C_DIM);
  seg(logTxt, C_RED);
  seg(scroll, C_INFO);
  bar.fillCircle(W - 14, 4, 3, rxOn ? C_RX : 0x2124);
  bar.fillCircle(W - 5, 4, 3, txOn ? C_TX : 0x2124);
  bar.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// Settings menu
// ---------------------------------------------------------------------------
enum MenuItem { MI_PORT, MI_BAUD, MI_FRAME, MI_PINS, MI_LINES, MI_INPUT, MI_EOL, MI_ECHO, MI_LOG,
                MI_TS, MI_BRIGHT, MI_PRESET, MI_CLEAR, MI_REBOOT, MI_COUNT };
static const char *MENU_LABELS[] = {"Port", "Baud rate", "Data/parity/stop", "Grove pins",
                                    "USB DTR / RTS", "Input mode", "Enter sends", "Local echo",
                                    "SD logging", "Log timestamps", "Brightness",
                                    "FX-4CR CAT preset", "Clear screen", "Reboot (Launcher)"};
constexpr int MENU_ROWS = 11;
static int menuSel = 0, menuTop = 0;
static char baudEdit[9];
static uint8_t baudEditLen = 0;

static void menuValue(int item, char *out, size_t sz) {
  switch (item) {
    case MI_BAUD:
      if (baudEditLen) snprintf(out, sz, "%s_", baudEdit);
      else if (cfg.autoBaud) snprintf(out, sz, scan.on ? "Auto (scanning)" : "Auto %lu (Fn+B)", (unsigned long)cfg.baud);
      else snprintf(out, sz, "%lu", (unsigned long)cfg.baud);
      break;
    case MI_PORT:
      if (usbPort()) portDesc(out, sz);
      else snprintf(out, sz, "Grove");
      break;
    case MI_LINES: snprintf(out, sz, "DTR %s RTS %s", cfg.dtr ? "on" : "off", cfg.rts ? "on" : "off"); break;
    case MI_FRAME: snprintf(out, sz, "%s", FRAMES[cfg.frame].name); break;
    case MI_PINS: snprintf(out, sz, cfg.swapPins ? "RX=G2 TX=G1" : "RX=G1 TX=G2"); break;
    case MI_INPUT: snprintf(out, sz, cfg.rawMode ? "Raw keys" : "Line"); break;
    case MI_EOL: snprintf(out, sz, "%s", EOL_NAMES[cfg.eol]); break;
    case MI_ECHO: snprintf(out, sz, "%s", ECHO_NAMES[cfg.localEcho % ECHO_COUNT]); break;
    case MI_LOG:
      if (sdlog::active()) snprintf(out, sz, "%s", sdlog::fileName());
      else snprintf(out, sz, cfg.logEnabled ? "No SD card" : "Off");
      break;
    case MI_TS: snprintf(out, sz, cfg.logTimestamps ? "On" : "Off"); break;
    case MI_BRIGHT: snprintf(out, sz, "%d%%", (cfg.brightness * 100 + 127) / 255); break;
    default: out[0] = 0;
  }
}

static void drawMenu() {
  lcd.startWrite();
  lcd.fillScreen(C_BG);
  lcd.fillRect(0, 0, W, STATUS_H, C_BAR);
  lcd.setTextColor(C_TEXT);
  lcd.drawString("UART Shell " APP_VERSION " - settings", 2, 1);
  if (menuSel < menuTop) menuTop = menuSel;
  if (menuSel >= menuTop + MENU_ROWS) menuTop = menuSel - MENU_ROWS + 1;
  for (int i = menuTop; i < MI_COUNT && i < menuTop + MENU_ROWS; i++) {
    int y = 12 + (i - menuTop) * 10;
    bool sel = i == menuSel;
    if (sel) lcd.fillRect(0, y - 1, W, 10, C_SEL);
    lcd.setTextColor(sel ? C_TEXT : C_DIM);
    lcd.drawString(MENU_LABELS[i], 4, y);
    char v[32];
    menuValue(i, v, sizeof v);
    lcd.setTextColor(sel ? C_TX : C_RX);
    lcd.drawString(v, W - 4 - strlen(v) * CW, y);
  }
  lcd.setTextColor(C_DIM);
  lcd.drawString("; . select  , / change  ` close", 2, H - 9);
  lcd.endWrite();
}

static void startLog() {
  char hdr[96], port[24];
  portDesc(port, sizeof port);
  snprintf(hdr, sizeof hdr, "UART Shell %s log, %lu %s %s", APP_VERSION, (unsigned long)cfg.baud,
           FRAMES[cfg.frame].name, port);
  if (sdlog::start(hdr)) {
    settingsSave();  // persists the log counter
    info("[log] %s", sdlog::fileName());
  } else {
    info("[log] SD card not available");
  }
}

// FX-4CR (and other Kenwood TS-590S style CAT radios) over USB: 115200 8N1, commands end
// with ';' so Enter adds nothing, DTR/RTS off because RTS keys PTT on the FX-4CR.
static void applyRadioPreset() {
  cfg.port = PORT_USB;
  cfg.autoBaud = 0;
  cfg.baud = 115200;
  cfg.frame = 0;
  cfg.eol = EOL_NONE;
  cfg.rawMode = 0;
  cfg.dtr = cfg.rts = 0;
  scan.on = false;
  linkBegin();
  logConfigNote();
  info("[preset] FX-4CR CAT: USB 115200 8N1, Enter adds nothing");
  info("try FA; (freq) IF; (status) ID; (model)");
}

static void setManualBaud(uint32_t b) {
  if (scan.on) scan.on = false;
  cfg.autoBaud = 0;
  cfg.baud = b;
  linkSetBaud(b);
  logConfigNote();
}

static void changeItem(int item, int dir) {
  switch (item) {
    case MI_BAUD: {
      // Position 0 is Auto, then the manual list.
      int pos = 0;
      if (!cfg.autoBaud) {
        int i = 0;  // first standard rate >= current
        while (i < BAUD_COUNT && BAUDS[i] < cfg.baud) i++;
        if (i < BAUD_COUNT && BAUDS[i] == cfg.baud) pos = i + 1;
        else pos = dir > 0 ? i : i + 1;  // custom rate: step to its neighbour
      }
      pos = (pos + dir + BAUD_COUNT + 1) % (BAUD_COUNT + 1);
      if (pos == 0) {
        cfg.autoBaud = 1;  // keeps the last detected rate; Fn+B scans
      } else {
        setManualBaud(BAUDS[pos - 1]);
      }
      break;
    }
    case MI_PORT: cfg.port = usbPort() ? PORT_GROVE : PORT_USB; linkBegin(); logConfigNote(); break;
    case MI_FRAME: cfg.frame = (cfg.frame + dir + FRAME_COUNT) % FRAME_COUNT; linkBegin(); logConfigNote(); break;
    case MI_PINS:
      cfg.swapPins ^= 1;
      if (!usbPort()) linkBegin();
      logConfigNote();
      break;
    case MI_LINES: {
      uint8_t v = ((cfg.rts << 1 | cfg.dtr) + dir + 4) % 4;  // off, DTR, RTS, both
      cfg.dtr = v & 1;
      cfg.rts = v >> 1;
      usbserial::setLines(cfg.dtr, cfg.rts);
      break;
    }
    case MI_INPUT: cfg.rawMode ^= 1; rawLen = 0; break;
    case MI_EOL: cfg.eol = (cfg.eol + dir + EOL_COUNT) % EOL_COUNT; break;
    case MI_ECHO: cfg.localEcho = (cfg.localEcho + dir + ECHO_COUNT) % ECHO_COUNT; eqTail = eqHead; break;
    case MI_LOG:
      if (sdlog::active() || cfg.logEnabled) {
        sdlog::stop();
        cfg.logEnabled = 0;
      } else {
        cfg.logEnabled = 1;
        startLog();
      }
      break;
    case MI_TS: cfg.logTimestamps ^= 1; break;
    case MI_BRIGHT: {
      int b = cfg.brightness + dir * 51;
      cfg.brightness = b < 26 ? 26 : b > 255 ? 255 : b;
      lcd.setBrightness(cfg.brightness);
      break;
    }
    case MI_PRESET: applyRadioPreset(); break;
    case MI_CLEAR: term::clear(); follow = true; break;
    case MI_REBOOT:
      sdlog::stop();
      settingsSave();
      ESP.restart();
      break;
  }
  settingsSave();
}

// Step through the standard rates only (no Auto), used by Fn+[ / Fn+].
static void quickBaud(int dir) {
  int i = 0;
  while (i < BAUD_COUNT && BAUDS[i] < cfg.baud) i++;
  if (dir < 0) i--;
  else if (i < BAUD_COUNT && BAUDS[i] == cfg.baud) i++;
  i = (i + BAUD_COUNT) % BAUD_COUNT;
  setManualBaud(BAUDS[i]);
  settingsSave();
  info("[baud] %lu", (unsigned long)cfg.baud);
}

static void showHelp() {
  info("Opt settings  Fn+B detect baud");
  info("Fn+[ ] baud -/+  Fn+F frame  Fn+S swap");
  info("Fn+U Grove/USB  Fn+D DTR  Fn+T RTS");
  info("Fn+X FX-4CR CAT preset  Fn+I USB info");
  info("Fn+R raw/line  Fn+N enter  Fn+E echo");
  info("Fn+L SD log  Fn+C clear  Fn+- = bright");
  info("Fn+; . line  Fn+, / page  Fn+Ent end");
  info("Ctrl+key ctrl char  Alt+; . history");
  jumpToEnd();
}

// Fn+letter shortcuts. Returns false if the key is not a shortcut.
static bool shortcut(uint8_t base) {
  char v[32];
  int item;
  switch (base) {
    case 'b': scanStart(); return true;
    case '[': quickBaud(-1); return true;
    case ']': quickBaud(1); return true;
    case 'h': showHelp(); return true;
    case 'i':
      if (!usbPort()) info("[usb] port is Grove; Fn+U switches to USB");
      else usbserial::report();
      return true;
    case 'x': applyRadioPreset(); settingsSave(); needFull = true; inputDirty = true; return true;
    case 'f': item = MI_FRAME; break;
    case 's': item = MI_PINS; break;
    case 'r': item = MI_INPUT; break;
    case 'n': item = MI_EOL; break;
    case 'e': item = MI_ECHO; break;
    case 'l': item = MI_LOG; break;
    case 'u': item = MI_PORT; break;
    case 'd':
    case 't':
      if (base == 'd') cfg.dtr ^= 1;
      else cfg.rts ^= 1;
      usbserial::setLines(cfg.dtr, cfg.rts);
      settingsSave();
      menuValue(MI_LINES, v, sizeof v);
      info("[USB lines] %s", v);
      return true;
    case 'c': changeItem(MI_CLEAR, 1); needFull = true; return true;
    case '-': changeItem(MI_BRIGHT, -1); return true;
    case '=': changeItem(MI_BRIGHT, 1); return true;
    default: return false;
  }
  changeItem(item, 1);
  menuValue(item, v, sizeof v);
  info("[%s] %s", MENU_LABELS[item], v);
  needFull = true;
  inputDirty = true;
  return true;
}

static void closeMenu() {
  menuOpen = false;
  baudEditLen = 0;
  needFull = true;
  inputDirty = true;
  clampView();
}

static void menuKey(const Key &k) {
  uint8_t c = k.code;
  if (menuSel == MI_BAUD && c >= '0' && c <= '9' && baudEditLen < 7) {
    baudEdit[baudEditLen++] = c;
    baudEdit[baudEditLen] = 0;
  } else if (menuSel == MI_BAUD && baudEditLen && c == kbd::K_BKSP) {
    baudEdit[--baudEditLen] = 0;
  } else if (menuSel == MI_BAUD && baudEditLen && c == kbd::K_ENTER) {
    uint32_t b = strtoul(baudEdit, nullptr, 10);
    baudEditLen = 0;
    if (b >= 50 && b <= 5000000) {
      setManualBaud(b);
      settingsSave();
    }
  } else if (c == kbd::K_OPT || c == kbd::K_ESC || c == '`' || c == kbd::K_BKSP) {
    closeMenu();
    return;
  } else if (c == ';' || c == kbd::K_UP) {
    baudEditLen = 0;
    menuSel = (menuSel + MI_COUNT - 1) % MI_COUNT;
  } else if (c == '.' || c == kbd::K_DOWN) {
    baudEditLen = 0;
    menuSel = (menuSel + 1) % MI_COUNT;
  } else if (c == '/' || c == kbd::K_RIGHT || c == kbd::K_ENTER || c == ' ') {
    changeItem(menuSel, 1);
    if (menuSel == MI_CLEAR || menuSel == MI_PRESET) return closeMenu();
  } else if (c == ',' || c == kbd::K_LEFT) {
    changeItem(menuSel, -1);
  } else {
    return;
  }
  drawMenu();
}

// ---------------------------------------------------------------------------
// Terminal keys
// ---------------------------------------------------------------------------
static void lineKey(uint8_t c) {
  if (c >= 0x20 && c < 0x7F) {
    if (inputLen < INPUT_MAX) {
      input[inputLen++] = c;
      input[inputLen] = 0;
    }
  } else if (c == kbd::K_TAB) {
    if (inputLen < INPUT_MAX) input[inputLen++] = '\t', input[inputLen] = 0;
  } else if (c == kbd::K_BKSP) {
    if (inputLen) input[--inputLen] = 0;
  } else if (c == kbd::K_ENTER) {
    sendLine();
  } else if (c == kbd::K_ESC || c == kbd::K_DEL) {
    inputLen = 0;
    input[0] = 0;
  } else {
    return;
  }
  inputDirty = true;
}

static void rawKey(uint8_t c) {
  if (c >= 0x20 && c < 0x7F) {
    char ch = c;
    send(&ch, 1, true);
    echoText(&ch, 1);
    if (rawLen < INPUT_MAX) rawLine[rawLen++] = ch;
  } else if (c == kbd::K_ENTER) {
    sendEol();
    echoNewline();
    sdlog::tx(rawLine, rawLen);
    rawLen = 0;
  } else if (c == kbd::K_BKSP) {
    send("\x7f");
    if (rawLen) rawLen--;
  } else if (c == kbd::K_TAB) {
    send("\t");
  } else if (c == kbd::K_ESC) {
    send("\x1b");
  } else if (c == kbd::K_DEL) {
    send("\x1b[3~");
  }
}

static void onKey(const Key &k) {
  if (menuOpen) return menuKey(k);
  if (k.code == kbd::K_OPT) {
    menuOpen = true;
    menuSel = 0;
    drawMenu();
    return;
  }
  if (scan.on && k.code == kbd::K_ESC) return scanCancel();

  int page = termRows() - 1;
  switch (k.code) {
    case kbd::K_UP: return scrollBy(-1);
    case kbd::K_DOWN: return scrollBy(1);
    case kbd::K_LEFT: return scrollBy(-page);
    case kbd::K_RIGHT: return scrollBy(page);
  }
  if ((k.mods & kbd::MOD_FN) && k.code == kbd::K_ENTER) return jumpToEnd();
  if ((k.mods & kbd::MOD_FN) && shortcut(k.base)) return;

  if (k.mods & kbd::MOD_CTRL) {
    uint8_t b = k.base;
    if (b >= 'a' && b <= 'z') return sendCtrl(b - 'a' + 1);
    switch (b) {
      case '[': return sendCtrl(0x1B);
      case '\\': return sendCtrl(0x1C);
      case ']': return sendCtrl(0x1D);
      case '6': return sendCtrl(0x1E);
      case '-': return sendCtrl(0x1F);
      case '2': case ' ': return sendCtrl(0x00);
    }
    return;
  }

  if (k.mods & kbd::MOD_ALT) {
    const char *seq = nullptr;
    switch (k.base) {
      case ';': seq = "\x1b[A"; break;
      case '.': seq = "\x1b[B"; break;
      case '/': seq = "\x1b[C"; break;
      case ',': seq = "\x1b[D"; break;
    }
    if (!seq) return;
    if (cfg.rawMode) send(seq);
    else if (k.base == ';') histRecall(1);
    else if (k.base == '.') histRecall(-1);
    return;
  }

  if (cfg.rawMode) rawKey(k.code);
  else lineKey(k.code);
}

// ---------------------------------------------------------------------------
void setup() {
  settingsLoad();

  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(cfg.brightness);
  lcd.setFont(&fonts::Font0);
  lcd.fillScreen(C_BG);
  canvas.setColorDepth(16);
  if (!canvas.createSprite(W, TERM_H)) {
    canvas.setColorDepth(8);
    canvas.createSprite(W, TERM_H);
  }
  canvas.setFont(&fonts::Font0);
  bar.setColorDepth(16);
  bar.createSprite(W, STATUS_H);
  bar.setFont(&fonts::Font0);

  if (!kbd::begin()) info("Keyboard (TCA8418) not found");
  linkBegin();

  info("UART Shell " APP_VERSION "  Opt: settings  Fn+H: keys");
  drawStatus(true);
  drawTerm();

  if (cfg.logEnabled) startLog();  // after first frame so the screen shows up immediately
}

void loop() {
  pumpUart();
  if (usbPort()) {
    switch (usbserial::poll()) {
      case usbserial::EV_CONNECTED:
        info("[usb] %s adapter connected", usbserial::name());
        logConfigNote();
        break;
      case usbserial::EV_DISCONNECTED: info("[usb] adapter disconnected"); break;
      case usbserial::EV_UNSUPPORTED: info("[usb] not a supported serial adapter"); break;
      default: break;
    }
  }
  char usbLog[48];
  while (usbserial::nextLog(usbLog, sizeof usbLog)) info("[usb] %s", usbLog);
  kbd::poll();
  Key k;
  while (kbd::get(k)) onKey(k);
  scanTick();
  sdlog::poll();

  if (menuOpen) {
    static uint32_t lastMenuDraw = 0;
    static bool lastScan = false;
    if (scan.on != lastScan && millis() - lastMenuDraw > 200) {  // refresh "scanning" state
      lastScan = scan.on;
      lastMenuDraw = millis();
      drawMenu();
    }
    delay(1);
    return;
  }

  uint32_t now = millis();
  if (now - lastFrame >= 33) {
    if (needFull || inputDirty || (follow && term::version != drawnVersion) ||
        (!follow && term::first() + termRows() - 1 > viewBottom)) {
      if (!follow) clampView();
      drawTerm();
      drawnVersion = term::version;
      inputDirty = false;
      bool full = needFull;
      needFull = false;
      drawStatus(full);
    } else {
      drawStatus(false);
    }
    lastFrame = now;
  }
  delay(1);
}
