#include "settings.h"

#include <Preferences.h>

// Stored as one NVS blob; bump the version when the struct layout changes.
static constexpr uint8_t VERSION = 1;
static const char *NS = "uartshell";

Settings cfg;

void settingsLoad() {
  Preferences p;
  if (!p.begin(NS, true)) return;
  if (p.getUChar("ver", 0) == VERSION && p.getBytesLength("cfg") == sizeof(Settings)) {
    p.getBytes("cfg", &cfg, sizeof(Settings));
  }
  p.end();
}

void settingsSave() {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.putUChar("ver", VERSION);
  p.putBytes("cfg", &cfg, sizeof(Settings));
  p.end();
}
