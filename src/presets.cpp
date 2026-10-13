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
   "115200 baud, 8 data bits, no parity, 1 stop bit: the most common setting for dev boards, "
   "Linux consoles and modules. Enter sends CR; text is edited in line mode."},
};

const Preset DEV[] = {
  {"U-Boot / Linux console", "Linux", 115200, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "For U-Boot and Linux serial consoles. Raw keys mode sends every key at once, so Tab "
   "completion, Ctrl+C and the arrow keys (Alt+; . , /) work. The board echoes what you type."},
  {"Raspberry Pi console", "RPi", 115200, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "GPIO14 is TXD (pin 8), GPIO15 is RXD (pin 10), GND on pin 6. 3.3 V, so it connects "
   "straight to the Grove port. Enable the serial console in raspi-config."},
  {"ESP32 / Arduino", "Arduino", 115200, F_8N1, LF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Most sketches use 115200. Enter sends LF, like the Arduino Serial Monitor's Newline "
   "setting. Fn+[ ] or Fn+B if the sketch uses another rate."},
  {"ESP8266 boot log", "ESP8266", 74880, F_8N1, LF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "The ESP8266 boot ROM prints at 74880 baud. Most sketches then switch to 115200: press "
   "Fn+] or Fn+B after boot."},
  {"3D printer (Marlin)", "Marlin", 115200, F_8N1, LF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Marlin firmware: 115200 on most boards, 250000 on some. Try M115 (firmware info) or "
   "M105 (temperatures)."},
  {"GRBL CNC", "GRBL", 115200, F_8N1, LF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "GRBL controllers: $$ lists settings, ? shows status, $H homes the machine."},
};

const Preset NETWORK[] = {
  {"Cisco / HP / Aruba / Juniper", "Cisco", 9600, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "Console port of most managed switches and routers: 9600 8N1, no flow control. The RJ45 "
   "console is RS-232 (+-12 V): use a USB-RS232 console cable on the USB port, never the Grove "
   "port directly. Fn+K sends BREAK for password recovery."},
  {"MikroTik / Ubiquiti / OpenWrt", "Router", 115200, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "Internal UART headers are 3.3 V: connect TX, RX and GND, never VCC. DB9 serial ports (some "
   "MikroTik models) are RS-232 and need a USB-RS232 cable."},
};

const Preset VOIP[] = {
  {"IP phone: Yealink/Polycom/GS", "IP phone", 115200, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "Yealink, Polycom and Grandstream phones have an internal 3.3 V UART header with the U-Boot "
   "and Linux console. Connect TX, RX and GND only."},
  {"Cisco IP phone", "Cisco tel", 115200, F_8N1, CR, RAW, HIDE, 0, 0, 0, 0, KEEP,
   "Debug console at 115200 on an internal header or the AUX port, depending on the model. "
   "Check the levels before connecting."},
};

const Preset CAT_TEXT[] = {
  {"truSDX (TS-480 CAT)", "truSDX", 115200, F_8N1, NOEOL, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Kenwood TS-480 commands over the radio's USB port. Commands end with ';' so Enter adds "
   "nothing: FA; (frequency), IF; (status), MD; (mode). Commands are case-sensitive."},
  {"uSDX (TS-480 CAT)", "uSDX", 38400, F_8N1, NOEOL, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Kenwood TS-480 commands. The rate depends on the firmware build (38400 on many); try "
   "Fn+[ ] if the radio stays silent. FA; IF; MD;"},
  {"FX-4CR (TS-590S CAT)", "FX-4CR", 115200, F_8N1, NOEOL, LINE, HIDE, 0, 0, 0, 0, USB,
   "Kenwood TS-590S commands over USB. RTS keys PTT, so DTR and RTS stay off. FA; IF; ID;. "
   "Unknown commands get no reply, and commands are case-sensitive."},
  {"Kenwood TS-480/590/890/2000", "Kenwood", 9600, F_8N1, NOEOL, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Commands end with ';' (FA; IF; MD;). Match the radio's COM speed menu. Some models expect "
   "RTS/CTS handshake: turn on Flow control here or off in the radio. The DB9 port is RS-232."},
  {"Yaesu FT-991A/710/891/DX10", "Yaesu", 38400, F_8N2, NOEOL, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Commands end with ';' (FA; IF; MD0;). Match the CAT RATE menu, often 4800 by default. "
   "8N2 also works with radios set to 1 stop bit."},
  {"Elecraft K3/KX2/KX3", "Elecraft", 38400, F_8N1, NOEOL, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Commands end with ';' (FA; IF;). Match the radio's RS232 rate setting."},
};

const Preset CAT_BIN[] = {
  {"Icom CI-V (IC-705/7300/9700)", "Icom", 115200, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "Binary, typed in hex: FE FE <radio> E0 <command> FD. Radio address: IC-705 A4, IC-7300 94, "
   "IC-9700 A2. FE FE A4 E0 03 FD reads the frequency; the reply is BCD, lowest digits first."},
  {"Yaesu FT-817/857/897", "FT-817", 4800, F_8N2, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "5-byte binary commands, typed in hex. 00 00 00 00 03 reads frequency and mode. Match the "
   "CAT rate menu (4800, 9600 or 38400) and use a CAT cable with matching levels."},
};

const Preset TWO_WAY[] = {
  {"Baofeng UV-5R / BF-888", "Baofeng", 9600, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "Programming port (Kenwood-style 2-pin jack), binary clone protocol shown in hex. K1 "
   "cables are usually PL2303 or CH340. This only watches traffic; it does not program."},
  {"Quansheng UV-K5/K6", "UV-K5", 38400, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "Programming port, binary and obfuscated protocol shown in hex. Watch only."},
  {"Kenwood TK series", "TK", 9600, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "Programming port, binary protocol shown in hex. Watch only."},
  {"Motorola GM/GP/CDM", "Motorola", 9600, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "Proprietary programming protocol, usually through a RIB box or a special cable. Shown in "
   "hex for monitoring only."},
};

const Preset AV[] = {
  {"Sanyo projector", "Sanyo", 19200, F_8N1, CR, LINE, SHOW, 0, 0, 0, 0, KEEP,
   "RS-232 port (+-12 V): use a USB-RS232 cable. Commands end with CR: C00 power on, C01 "
   "power off, CR0 status. See the projector's manual for the full list."},
  {"Epson projector (ESC/VP21)", "Epson", 9600, F_8N1, CR, LINE, SHOW, 0, 0, 0, 0, KEEP,
   "RS-232 port: use a USB-RS232 cable. PWR? (power status), PWR ON, PWR OFF, SOURCE?. A ':' "
   "reply means ready."},
  {"BenQ projector", "BenQ", 115200, F_8N1, CR, LINE, SHOW, 0, 0, 0, 0, KEEP,
   "RS-232 port: use a USB-RS232 cable. 115200 on most models, 9600 on some. *pow=on#, "
   "*pow=off#, *pow=?#, each sent with CR."},
  {"NEC projector", "NEC", 38400, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "RS-232 port: use a USB-RS232 cable. Binary commands in hex: 02 00 00 00 00 02 power on, "
   "02 01 00 00 00 03 power off."},
  {"Samsung TV (EX-Link)", "Samsung", 9600, F_8N1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "EX-Link (3.5 mm or DB9) is RS-232: use a USB-RS232 cable. Binary commands in hex: "
   "08 22 00 00 00 02 D4 power on, 08 22 00 00 00 01 D5 power off."},
};

const Preset MODULES[] = {
  {"GPS (NMEA)", "GPS", 9600, F_8N1, CRLF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Receivers send NMEA sentences ($GPGGA, $GPRMC, ...) at 9600 by default; some use 38400 or "
   "115200 (Fn+B finds it). Most modules are 3.3 V."},
  {"AT modem / SIM800 / ESP-AT", "AT", 115200, F_8N1, CRLF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Commands end with CRLF: AT, ATI, AT+CSQ (signal). Many modules detect the rate from the "
   "first few AT commands."},
  {"HC-05 Bluetooth (AT mode)", "HC-05", 38400, F_8N1, CRLF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Hold the module's button while powering it up for AT mode (38400). AT, AT+NAME?, "
   "AT+UART?. In data mode it uses its configured rate, 9600 by default."},
  {"Meshtastic console", "Meshtastic", 115200, F_8N1, LF, LINE, HIDE, 0, 0, 0, 0, KEEP,
   "Debug log of a Meshtastic node at 115200."},
  {"Victron VE.Direct", "VE.Direct", 19200, F_8N1, CRLF, LINE, SHOW, 0, 0, 0, 0, KEEP,
   "The device sends a text frame every second (read-only). The port is 3.3 V or 5 V "
   "depending on the model: check before connecting it to the Grove port."},
  {"Modbus RTU", "Modbus", 19200, F_8E1, NOEOL, LINE, SHOW, 0, 0, 1, 0, KEEP,
   "19200 8E1 is the Modbus default. Binary frames ending with a CRC, shown in hex. RS-485 "
   "buses need an RS-485 adapter."},
  {"APC UPS (Smart protocol)", "APC UPS", 2400, F_8N1, NOEOL, RAW, SHOW, 0, 0, 0, 0, KEEP,
   "Needs APC's 940-0024 cable (RS-232). Single-character commands in raw mode: Y enters smart "
   "mode, then B (battery voltage), f (battery %), Q (status)."},
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
  char line[200];
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
