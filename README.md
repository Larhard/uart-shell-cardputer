# UART Shell for M5Stack Cardputer ADV

A small, fast-booting serial terminal for the **M5Stack Cardputer ADV**. Plug a device into the Grove port and start typing.

- Opens straight into the terminal. There is no splash screen or menu at boot.
- 2000-line scrollback.
- RX and TX text in different colors: RX is green, TX is amber, and status messages are blue.
- Set the baud rate by hand (preset list or any custom rate), or detect it automatically on demand. The detected rate is remembered.
- Logs to a timestamped file on the SD card, one file per session.
- Line mode (edit, then send) or raw mode (every key is sent at once).
- All settings are restored at the next launch, stored in NVS.
- Works with [Launcher](https://bmorcelli.github.io/Launcher/catalog.html). It's a plain app image of about 420 KB with no data partition.

Builds on ideas from [Evil-M5Project](https://github.com/7h30th3r0n3/Evil-M5project) (UART shell) and [UARTerm](https://github.com/Zolibot/UARTerm) (SD logging).

## Wiring

The default is the Grove port (Port A):

| Grove pin | Default | Swapped (`Fn+S`) |
|-----------|---------|------------------|
| G1        | RX      | TX               |
| G2        | TX      | RX               |
| GND       | GND     | GND              |

Connect the target's TX to the Cardputer's RX, and its RX to the Cardputer's TX. Logic level is **3.3 V only**. Don't connect 5 V UART signals directly.

## Keys

| Keys | Action |
|------|--------|
| `Opt` | Open or close the settings menu |
| `Fn+H` | Show the shortcut help in the terminal |
| `Fn+B` | Detect the baud rate now (`Fn+Esc` cancels). The result is saved |
| `Fn+[` / `Fn+]` | Previous / next standard baud rate |
| `Fn+F` | Cycle the data/parity/stop format (8N1, 8E1, 8O1, 8N2, 7E1, 7O1) |
| `Fn+S` | Swap the RX/TX pins |
| `Fn+R` | Switch between line mode and raw mode |
| `Fn+N` | Cycle what Enter sends: CR, LF, CRLF or nothing |
| `Fn+E` | Turn local echo on or off |
| `Fn+L` | Turn SD logging on or off (starts a new file) |
| `Fn+C` | Clear the screen and scrollback |
| `Fn+-` / `Fn+=` | Brightness down / up |
| `Fn+;` / `Fn+.` | Scroll up / down one line |
| `Fn+,` / `Fn+/` | Scroll up / down one page |
| `Fn+Enter` | Jump to the newest output |
| `Ctrl+<key>` | Send a control character (`Ctrl+C`, `Ctrl+D`, `Ctrl+[` = ESC, …) |
| `Alt+;` / `Alt+.` | Line mode: browse command history. Raw mode: send the Up/Down arrow keys |
| `Alt+,` / `Alt+/` | Raw mode: send the Left/Right arrow keys |
| `Fn+Esc` (the `` ` `` key) | Line mode: clear the input. Raw mode: send ESC |

**Line mode** has an input line at the bottom of the screen. The text is sent when you press Enter, followed by the line ending you selected. **Raw mode** sends every key as soon as you press it: Backspace sends DEL (0x7F) and Tab sends a tab, so shell completion and editors work.

### Settings menu (`Opt`)

`;` / `.` select an item, `,` / `/` / Enter change it, and `` ` ``, Opt or Backspace close the menu. On the *Baud rate* item, type digits and press Enter to set a custom rate. The menu also has an item to reboot the device, which returns you to Launcher.

## Baud rate detection

Detection only runs when you press `Fn+B`. It cycles through the common rates, from 115200 down to 1200 and up to 2 Mbaud, and picks the first one where the received data is readable text. The device has to be sending something while it scans. The rate it finds is saved, so the next launch starts with it immediately. In the status bar, `A:` before the baud rate means the rate came from detection.

## Local echo

With local echo on, what you type appears in the TX color. Many devices (Linux consoles, U-Boot, most shells) echo your input back. The terminal recognizes that echo and hides it, so you don't see every line twice, and it works the same way with devices that don't echo. If you want to see exactly what the device sends back, turn local echo off.

## SD logging

Log files are saved as `/uart_logs/uart_NNNN.txt`, with a new number each session. The format looks like this:

```
# UART Shell 1.0 log, 115200 8N1 RX=G1 TX=G2
[00:00:01.204] U-Boot 2023.04 (Jan 01 2024)
[00:00:03.871] => 
[00:00:05.112] > printenv
[00:00:05.130] arch=arm
```

- Lines starting with `> ` are what you sent.
- Lines starting with `# ` are notes, such as a baud rate change.
- Timestamps count from the start of the session. You can turn them off in settings.
- Non-printable bytes are written as `\xNN`, and ANSI escape sequences are removed.
- The log is written to the card every 2 seconds.

## Building

You need [PlatformIO Core](https://platformio.org/install/cli) and Python 3. The first build downloads the ESP32 toolchain (about 1 GB, and can take several minutes).

### Windows

```powershell
# One-time install of PlatformIO Core into its own virtualenv
Invoke-WebRequest https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -OutFile get-platformio.py
python get-platformio.py
$env:Path += ";$env:USERPROFILE\.platformio\penv\Scripts"

pio run                         # build
pio run -t upload               # build and flash over USB
```

### Linux / macOS

```bash
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
python3 get-platformio.py
export PATH="$PATH:$HOME/.platformio/penv/bin"

pio run
pio run -t upload
```

On Linux you may need to add yourself to the `dialout` (Debian/Ubuntu) or `uucp` (Arch) group, or install the [PlatformIO udev rules](https://docs.platformio.org/en/latest/core/installation/udev-rules.html), before you can upload.

You can also open the folder in VS Code with the PlatformIO extension and use the Build and Upload buttons.

If an upload doesn't start, hold `G0` (the BtnA key) while plugging in USB. This puts the board in download mode.

### Creating the dist package

Every `pio run` creates the package automatically. A post-build step ([tools/merge_bin.py](tools/merge_bin.py)) copies the release files into `dist/`, and takes the version number from `APP_VERSION` in [src/main.cpp](src/main.cpp):

| File | Use |
|------|-----|
| `dist/UART_Shell_v<ver>_CardputerADV.bin` | App image for Launcher. Copy it to the SD card and install it (see below), or flash it at `0x10000` |
| `dist/UART_Shell_v<ver>_CardputerADV_merged.bin` | Full image (bootloader, partition table and app). Flash it at `0x0` with esptool or a web flasher |

To make a release:

1. Change `#define APP_VERSION` in `src/main.cpp`.
2. Run `pio run`. The build log ends with a `dist: ...` line listing the files it created.
3. Optional: check the image with `esptool.py --chip esp32s3 image_info dist/UART_Shell_v<ver>_CardputerADV.bin`. The checksum and validation hash should both say `valid`.
4. Attach the two files to a GitHub release. `dist/` isn't tracked by git because `*.bin` is ignored.

To flash the merged image over USB:

```bash
esptool.py --chip esp32s3 write_flash 0x0 dist/UART_Shell_v1.0_CardputerADV_merged.bin
```

The same files also stay in `.pio/build/cardputer-adv/` as `firmware.bin` and `uart_shell_merged.bin`.

## Using it with Launcher

Copy `dist/UART_Shell_v<ver>_CardputerADV.bin` to the SD card (any folder). On the Cardputer, open Launcher, choose **SD**, select the file and then **Install**. The app doesn't use SPIFFS or FAT partitions, so Launcher only installs the 420 KB app image. Settings are stored in NVS under the `uartshell` namespace, so they survive reinstalls and switching between apps. To get back to Launcher, reset the device or use *Reboot* in the settings menu.

## Project layout

```
src/main.cpp       terminal, RX/TX handling, baud detection, menu and shortcuts
src/term.*         scrollback ring buffer (2000 lines x 40 columns)
src/keyboard.*     TCA8418 keyboard driver for the Cardputer ADV
src/display.h      fixed ST7789 panel config (LovyanGFX)
src/sdlog.*        buffered SD session logger
src/settings.*     settings stored in NVS
partitions.csv     8 MB layout with two OTA app slots and no data partitions
tools/merge_bin.py post-build step that creates the merged image and dist/ package
```

## Limitations

- Supports the Cardputer **ADV** only (the TCA8418 keyboard). The original Cardputer uses a GPIO keyboard matrix and is not supported.
- ANSI escape sequences (colors, cursor movement) are removed, not interpreted. Full-screen programs like `vi` or `top` won't display correctly.
- Baud detection needs the target to send text. It can't detect binary protocols, so set the rate by hand for those.
