#include "presets.h"

#include <Arduino.h>
#include <SD.h>

#include "sdlog.h"

namespace presets {

const char *const FRAME_NAMES[F_COUNT] = {"8N1", "8E1", "8O1", "8N2", "7E1", "7O1"};

namespace {

// Shorthands for the table below.
constexpr uint8_t CR = EOL_CR, LF = EOL_LF, CRLF = EOL_CRLF, NOEOL = EOL_NONE;
constexpr uint8_t LINE = 0, RAW = 1;
constexpr uint8_t HIDE = ECHO_HIDE, SHOW = ECHO_SHOW;

//  name, short, baud, frame, enter, input, echo, dtr, rts, hex, flow, port, note
const Preset GENERAL[] = {
  {"Default", "Default", 115200, F_8N1, CR, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "115200 8N1: most boards, consoles, modules"},
};

const Preset DEV[] = {
  {"U-Boot / Linux console", "Linux", 115200, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "raw keys: Tab completion and Ctrl+C work"},
  {"Raspberry Pi console", "RPi", 115200, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "GPIO14 TXD / GPIO15 RXD, 3.3 V"},
  {"ESP32 / Arduino", "Arduino", 115200, F_8N1, LF, LINE, HIDE, 0, 0, 0, 0, KEEP, nullptr},
  {"ESP8266 boot log", "ESP8266", 74880, F_8N1, LF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "boot ROM speaks 74880; sketches usually 115200 (Fn+])"},
  {"3D printer (Marlin)", "Marlin", 115200, F_8N1, LF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "some boards use 250000; try M115"},
  {"GRBL CNC", "GRBL", 115200, F_8N1, LF, LINE, HIDE, 0, 0, 0, 0, KEEP, "$$ settings, ? status"},
};

const Preset NETWORK[] = {
  {"Cisco / HP / Aruba / Juniper", "Cisco", 9600, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "RS-232 console: needs USB-RS232 cable. BREAK: Fn+K"},
  {"MikroTik / Ubiquiti / OpenWrt", "Router", 115200, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "board UARTs are 3.3 V; DB9 ports are RS-232"},
};

const Preset VOIP[] = {
  {"IP phone: Yealink/Polycom/GS", "IP phone", 115200, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "internal 3.3 V UART header"},
  {"Cisco IP phone", "Cisco tel", 115200, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "console via the AUX/RJ jack, 3.3 V"},
};

const Preset CAT_TEXT[] = {
  {"truSDX (TS-480 CAT)", "truSDX", 115200, F_8N1, NOEOL, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Kenwood commands, e.g. FA; IF; MD;"},
  {"uSDX (TS-480 CAT)", "uSDX", 38400, F_8N1, NOEOL, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "rate depends on the firmware build; FA; IF;"},
  {"FX-4CR (TS-590S CAT)", "FX-4CR", 115200, F_8N1, NOEOL, LINE, HIDE, 0, 0, 0, 0, USB,
   "RTS keys PTT: keep RTS off. FA; IF; ID;"},
  {"Kenwood TS-480/590/890/2000", "Kenwood", 9600, F_8N1, NOEOL, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "match the radio's COM speed; may need RTS/CTS"},
  {"Yaesu FT-991A/710/891/DX10", "Yaesu", 38400, F_8N2, NOEOL, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "match the CAT RATE menu (often 4800 by default)"},
  {"Elecraft K3/KX2/KX3", "Elecraft", 38400, F_8N1, NOEOL, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "match the radio's RS232 setting; FA; IF;"},
};

const Preset CAT_BIN[] = {
  {"Icom CI-V (IC-705/7300/9700)", "Icom", 115200, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "FE FE A4 E0 03 FD = read freq (705: A4, 7300: 94)"},
  {"Yaesu FT-817/857/897", "FT-817", 4800, F_8N2, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "00 00 00 00 03 = read freq; CAT 4800/9600/38400"},
};

const Preset TWO_WAY[] = {
  {"Baofeng UV-5R / BF-888", "Baofeng", 9600, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "binary clone protocol; cable: PL2303 or CH340"},
  {"Quansheng UV-K5/K6", "UV-K5", 38400, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "binary protocol (obfuscated)"},
  {"Kenwood TK series", "TK", 9600, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "programming port, binary"},
  {"Motorola GM/GP/CDM", "Motorola", 9600, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "most need a RIB or special cable; monitor only"},
};

const Preset AV[] = {
  {"Sanyo projector", "Sanyo", 19200, F_8N1, CR, LINE, SHOW, 0, 0, 0, 0, KEEP,
   "RS-232. C00 on, C01 off, CR0 status"},
  {"Epson projector (ESC/VP21)", "Epson", 9600, F_8N1, CR, LINE, SHOW, 0, 0, 0, 0, KEEP,
   "RS-232. PWR? / PWR ON / PWR OFF, ':' = ready"},
  {"BenQ projector", "BenQ", 115200, F_8N1, CR, LINE, SHOW, 0, 0, 0, 0, KEEP,
   "RS-232. *pow=?# / *pow=on# / *pow=off#"},
  {"NEC projector", "NEC", 38400, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "RS-232. 02 00 00 00 00 02 = power on"},
  {"Samsung TV (EX-Link)", "Samsung", 9600, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "RS-232. 08 22 00 00 00 02 D4 = power on"},
};

const Preset MODULES[] = {
  {"GPS (NMEA)", "GPS", 9600, F_8N1, CRLF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "some receivers use 38400 or 115200 (Fn+B)"},
  {"AT modem / SIM800 / ESP-AT", "AT", 115200, F_8N1, CRLF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "AT, ATI, AT+CSQ"},
  {"HC-05 Bluetooth (AT mode)", "HC-05", 38400, F_8N1, CRLF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "hold the button at power-up for AT mode"},
  {"Meshtastic console", "Meshtastic", 115200, F_8N1, LF, LINE, HIDE, 0, 0, 0, 0, KEEP, nullptr},
  {"Victron VE.Direct", "VE.Direct", 19200, F_8N1, CRLF, LINE, SHOW, 0, 0, 0, 0, KEEP,
   "read-only text frames; 3.3 V or 5 V by model!"},
  {"Modbus RTU", "Modbus", 19200, F_8E1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "RS-485 needs an adapter; frames end with CRC"},
  {"APC UPS (Smart protocol)", "APC UPS", 2400, F_8N1, NOEOL, RAW, SHOW, 0, 0, 0, 0, KEEP,
   "needs APC 940-0024 cable; send Y first"},
};

#define GROUP(name, arr) {name, arr, sizeof(arr) / sizeof(arr[0])}

constexpr const char *FILE_PATH = "/uart_presets.txt";

const char *const EOL_KEYS[] = {"cr", "lf", "crlf", "none"};
const char *const ECHO_KEYS[] = {"off", "hide", "show"};
const char *const PORT_KEYS[] = {"keep", "grove", "usb"};

int lookup(const char *v, const char *const *keys, int n, int def) {
  for (int i = 0; i < n; i++)
    if (strcasecmp(v, keys[i]) == 0) return i;
  return def;
}

}  // namespace

const Group GROUPS[] = {
  GROUP("General", GENERAL),
  GROUP("Dev boards & Linux", DEV),
  GROUP("Network gear", NETWORK),
  GROUP("VoIP & embedded", VOIP),
  GROUP("Ham CAT (text)", CAT_TEXT),
  GROUP("Ham CAT (binary)", CAT_BIN),
  GROUP("Two-way radios", TWO_WAY),
  GROUP("Projectors & TVs", AV),
  GROUP("Modules & devices", MODULES),
};
const int GROUP_COUNT = sizeof(GROUPS) / sizeof(GROUPS[0]);

int loadUser(UserPreset *out, int max) {
  if (!sdlog::mount()) return -1;
  File f = SD.open(FILE_PATH, FILE_READ);
  if (!f) return 0;
  int n = -1;  // index of the section being read
  char line[96];
  while (f.available()) {
    size_t len = f.readBytesUntil('\n', line, sizeof line - 1);
    line[len] = 0;
    char *s = line;
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
    if (!*s || *s == '#' || *s == ';') continue;
    if (*s == '[') {
      if (n + 1 >= max) break;
      UserPreset &u = out[++n];
      char *close = strchr(s, ']');
      if (close) *close = 0;
      snprintf(u.name, sizeof u.name, "%s", s + 1);
      snprintf(u.shortName, sizeof u.shortName, "%s", u.name);
      u.note[0] = 0;
      u.p = {u.name, u.shortName, 115200, F_8N1, EOL_CR, 0, ECHO_HIDE, 0, 0, 0, 0, KEEP, nullptr};
      continue;
    }
    if (n < 0) continue;
    char *eq = strchr(s, '=');
    if (!eq) continue;
    *eq = 0;
    char *key = s, *val = eq + 1;
    for (char *k = eq - 1; k > key && (*k == ' ' || *k == '\t'); k--) *k = 0;
    while (*val == ' ' || *val == '\t') val++;
    // Inline comment: ';' after whitespace (a ';' inside a value, as in "FA;", stays).
    for (char *c = val; *c; c++) {
      if (*c == ';' && c > val && (c[-1] == ' ' || c[-1] == '\t')) {
        *c = 0;
        while (c > val && (c[-1] == ' ' || c[-1] == '\t')) *--c = 0;
        break;
      }
    }
    UserPreset &u = out[n];
    Preset &p = u.p;
    if (!strcasecmp(key, "short")) snprintf(u.shortName, sizeof u.shortName, "%s", val);
    else if (!strcasecmp(key, "note")) {
      snprintf(u.note, sizeof u.note, "%s", val);
      p.note = u.note;
    } else if (!strcasecmp(key, "baud")) p.baud = strtoul(val, nullptr, 10) ?: p.baud;
    else if (!strcasecmp(key, "format")) p.frame = lookup(val, FRAME_NAMES, F_COUNT, p.frame);
    else if (!strcasecmp(key, "enter")) p.eol = lookup(val, EOL_KEYS, 4, p.eol);
    else if (!strcasecmp(key, "input")) p.raw = !strcasecmp(val, "raw");
    else if (!strcasecmp(key, "echo")) p.echo = lookup(val, ECHO_KEYS, 3, p.echo);
    else if (!strcasecmp(key, "dtr")) p.dtr = atoi(val) != 0;
    else if (!strcasecmp(key, "rts")) p.rts = atoi(val) != 0;
    else if (!strcasecmp(key, "hex")) p.hex = atoi(val) != 0;
    else if (!strcasecmp(key, "flow")) p.flow = !strcasecmp(val, "rtscts");
    else if (!strcasecmp(key, "port")) p.port = lookup(val, PORT_KEYS, 3, p.port);
  }
  f.close();
  return n + 1;
}

bool saveUser(const char *name, const Preset &p) {
  if (!sdlog::mount()) return false;
  bool isNew = !SD.exists(FILE_PATH);
  File f = SD.open(FILE_PATH, FILE_APPEND);
  if (!f) return false;
  if (isNew)
    f.print("# UART Shell presets. One [section] per preset; edit freely on a PC.\n"
            "# format=8N1|8E1|8O1|8N2|7E1|7O1 enter=cr|lf|crlf|none input=line|raw\n"
            "# echo=off|hide|show flow=none|rtscts port=keep|grove|usb\n");
  f.printf("\n[%s]\nshort=%.11s\nbaud=%lu\nformat=%s\nenter=%s\ninput=%s\necho=%s\n"
           "dtr=%u\nrts=%u\nhex=%u\nflow=%s\nport=%s\n",
           name, name, (unsigned long)p.baud, FRAME_NAMES[p.frame % F_COUNT], EOL_KEYS[p.eol % 4],
           p.raw ? "raw" : "line", ECHO_KEYS[p.echo % 3], p.dtr, p.rts, p.hex,
           p.flow ? "rtscts" : "none", PORT_KEYS[p.port % 3]);
  f.close();
  return true;
}

}  // namespace presets
