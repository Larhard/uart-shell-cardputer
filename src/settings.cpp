#include "settings.h"

#include <Preferences.h>
#include <stddef.h>

// Stored as one NVS blob; bump the version when the struct layout changes.
static constexpr uint8_t VERSION = 1;
static const char *NS = "uartshell";

Settings cfg;

void settingsLoad() {
  Preferences p;
  if (!p.begin(NS, true)) return;
  size_t n = p.getBytesLength("cfg");
  if (p.getUChar("ver", 0) == VERSION && n && n <= sizeof(Settings)) {
    p.getBytes("cfg", &cfg, n);
    // Saved by a version without presets: the settings may be anything, so no name.
    if (n < offsetof(Settings, presetName) + PRESET_NAME_LEN) cfg.presetName[0] = 0;
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
