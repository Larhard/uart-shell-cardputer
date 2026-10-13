# UART Shell for M5Stack Cardputer ADV

A small, fast-booting serial terminal for the **M5Stack Cardputer ADV**. Plug a device into the Grove port and start typing.

- Opens straight into the terminal. There is no splash screen or menu at boot.
- 2000-line scrollback.
- RX and TX text in different colors: RX is green, TX is amber, and status messages are blue.
- Set the baud rate by hand (preset list or any custom rate), or detect it automatically on demand. The detected rate is remembered.
- Logs to a timestamped file on the SD card, one file per session.
- Line mode (edit, then send) or raw mode (every key is sent at once).
- About 40 presets for consoles, radios, projectors and modules, plus your own presets saved on the SD card.
- Hex mode for binary protocols (Icom CI-V, Modbus, ...), serial BREAK, RTS/CTS flow control.
- Talks to the Grove UART, or to a USB-serial device plugged into the USB-C port (USB host mode). For example, the CAT port of a (tr)uSDX or FX-4CR radio.
- All settings are restored at the next launch, stored in NVS.
- Works with [Launcher](https://bmorcelli.github.io/Launcher/catalog.html). It's a plain app image of about 535 KB with no data partition.

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
| `Fn+X` | Open the preset picker |
| `Fn+H` | Show the shortcut help in the terminal |
| `Fn+B` | Detect the baud rate now (`Fn+Esc` cancels). The result is saved |
| `Fn+[` / `Fn+]` | Previous / next standard baud rate |
| `Fn+F` | Cycle the data/parity/stop format (8N1, 8E1, 8O1, 8N2, 7E1, 7O1) |
| `Fn+U` | Switch the port between the Grove UART and USB host |
| `Fn+S` | Swap the Grove RX/TX pins |
| `Fn+D` / `Fn+T` | Turn the USB adapter's DTR / RTS line on or off |
| `Fn+K` | Send a serial BREAK (250 ms) |
| `Fn+V` | Turn hex mode on or off |
| `Fn+P` | Switch to the next USB serial port, when several are attached |
| `Fn+I` | USB diagnostics: host state and every device on the bus |
| `Fn+Z` | Re-enumerate the USB bus (power-cycles the Cardputer's USB port) |
| `Fn+R` | Switch between line mode and raw mode |
| `Fn+N` | Cycle what Enter sends: CR, LF, CRLF or nothing |
| `Fn+E` | Cycle local echo: hide echo, show all, off |
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

`;` / `.` select an item, `,` / `/` / Enter change it, and `` ` ``, Opt or Backspace close the menu. On the *Baud rate* item, type digits and press Enter to set a custom rate. *Preset* opens the preset picker. *Flow control* switches RTS/CTS on USB adapters that support it. The menu also has an item to reboot the device, which returns you to Launcher.

## Presets

`Fn+X` (or *Preset* in the settings menu) opens the preset picker: first a list of groups, then the presets in a group. `;` / `.` move, Enter applies, `,` or `` ` `` goes back, and typing a letter jumps to the next entry that starts with it. The bottom line shows a note for the selected preset (wiring, voltage levels, example commands); the note is also printed when the preset is applied.

A preset sets the baud rate, data format, what Enter sends, line/raw input, echo, DTR/RTS, hex mode and flow control. Presets for devices that only have a USB port (the FX-4CR) also switch the port to USB. Pins, logging and brightness are left alone.

The status bar shows the active preset's name instead of the details it set. **Changing any of those values drops the name**: the settings are then *Custom*, as shown in the menu. Applying a preset again restores it. A fresh install starts with the *Default* preset.

| Group | Presets |
|-------|---------|
| General | Default (115200 8N1, Enter sends CR) |
| Dev boards & Linux | U-Boot / Linux console, Raspberry Pi, ESP32 / Arduino, ESP8266 boot log (74880), Marlin 3D printer, GRBL CNC |
| Network gear | Cisco / HP / Aruba / Juniper (9600), MikroTik / Ubiquiti / OpenWrt (115200) |
| VoIP & embedded | Yealink / Polycom / Grandstream phones, Cisco IP phones |
| Ham CAT (text) | truSDX, uSDX, FX-4CR, Kenwood, Yaesu FT-991A family, Elecraft |
| Ham CAT (binary) | Icom CI-V (IC-705/7300/9700), Yaesu FT-817/857/897 |
| Two-way radios | Baofeng UV-5R / BF-888, Quansheng UV-K5, Kenwood TK, Motorola GM/GP/CDM |
| Projectors & TVs | Sanyo, Epson (ESC/VP21), BenQ, NEC, Samsung EX-Link |
| Modules & devices | GPS (NMEA), AT modems, HC-05, Meshtastic, Victron VE.Direct, Modbus RTU, APC UPS |
| My presets | your own, from the SD card |

The values are common factory defaults. Many devices let you change their rate (radios especially), so if a device stays silent, check its menu or try `Fn+B` / `Fn+[` `Fn+]`. Several of these devices (Cisco consoles, projectors, TVs, older radios, UPSs) use **RS-232 levels (±12 V)**, which would damage the 3.3 V Grove port: use a USB-RS232 cable, or a MAX3232 level shifter on the Grove port. Their preset notes say so.

The two-way radio presets are for watching the programming port. The programming protocols themselves are binary and vendor specific; the terminal does not clone or program radios.

### My presets

In the *My presets* group, the first entry, **+ Save current settings**, asks for a name and appends the current settings to `/uart_presets.txt` on the SD card. The file is plain text, so you can edit, copy or share it on a PC:

```ini
[Shack IC-705]
short=IC-705        ; status bar name, up to 11 characters
baud=115200
format=8N1          ; 8N1 8E1 8O1 8N2 7E1 7O1
enter=none          ; cr lf crlf none
input=line          ; line raw
echo=show           ; off hide show
dtr=0
rts=0
hex=1
flow=none           ; none rtscts
port=usb            ; keep grove usb
note=CI-V address A4
```

Missing keys take the Default preset's values. Up to 16 presets are read.

## Hex mode

`Fn+V` (or *Hex mode* in the menu) is for binary protocols:

- **Received data** is shown as hex bytes, 13 per line. A pause of more than 30 ms between bytes starts a new line, so each packet usually gets its own line.
- **In line mode, you type bytes as hex.** `FE FE A4 E0 03 FD`, `fefea4e003fd` and `0xFE,0xFE` all work. Nothing is added on Enter; type `0D` if the device needs a CR. The input prompt changes from `>` to `#`.
- In raw mode, each key is sent as its byte value and shown in hex.
- The SD log records hex too, with sent lines prefixed `> ` as usual.

## Serial BREAK and flow control

`Fn+K` holds the TX line low for 250 ms (a serial BREAK), which Cisco password recovery and some boot loaders need. It works on the Grove port and on all supported USB chips.

*Flow control* in the settings menu turns on RTS/CTS hardware flow control. It needs a USB adapter that supports it (CP210x, FTDI or PL2303): the Grove port has only RX and TX, and the CH340 and CDC devices don't support it. With flow control on, the adapter drives RTS itself.

## USB serial devices (USB host mode)

Set **Port** to *USB* (`Fn+U`, or in the settings menu) and the Cardputer acts as a USB host on its USB-C port. It then talks to a USB-serial device, such as the CAT/serial port of a (tr)uSDX transceiver, the same way it talks to the Grove UART. Baud rate, data format, baud detection, logging and echo all work the same.

Supported devices:

| Device | Examples |
|--------|----------|
| WCH CH340 / CH341 | Most Arduino Nano clones, cheap USB-TTL cables, and likely the (tr)uSDX (check which chip your build uses) |
| Silicon Labs CP210x | CP2102 / CP2104 modules, many ESP32 dev boards |
| FTDI FT232R / FT230X / FT2232H / FT232H | FTDI cables and boards |
| Prolific PL2303 (HX, HXN, older) | Many RS-232 and radio programming cables |
| Standard USB CDC-ACM | CH343 / CH9102, Arduino boards with native USB, RP2040, STM32 VCP |

The status bar shows `U:CH34x` (or the detected type) when an adapter is connected, and `U:--` in red when nothing is connected. Plugging and unplugging is detected automatically. Settings are re-applied each time an adapter connects.

### Several serial ports

A device can offer more than one serial port. For example, a radio might have a CH340 and a second serial chip behind its internal hub, or a dual-port chip such as an FT2232, CP2105 or CH342. The app lists every port it can drive. Sound cards, hubs and HID devices are ignored.

- The status bar shows which port is active, for example `U:CH34x 1/2`.
- `Fn+P`, or *USB port* in the settings menu, switches to the next port.
- `Fn+I` lists all ports and marks the active one with `*`.
- Each port is shown as chip, VID:PID, interface (when a chip has several ports) and hub port, for example `CH34x 1a86:7523 hub2`.

The port you choose is saved. At the next launch, or when the device is plugged in again, the app picks the same port, matching chip IDs, interface and hub port. If that port isn't there, it uses the first port it finds and switches back once your port appears. Two identical chips are told apart by the hub port they're plugged into.

**DTR / RTS** start off. Many boards with a USB-serial chip use DTR or RTS to reset the microcontroller. Some radios use them for PTT (transmit) or CW keying, so leave them off unless you know your device needs them. Some CDC devices only send data once DTR is on; use `Fn+D` for those.

### FX-4CR

The BG2FX FX-4CR's USB port has a COM port and a USB sound card behind one cable. According to its manual, the CAT protocol is Kenwood TS-590S at 115200 baud, and PTT is on **RTS**. Inside, the radio has a USB hub (Terminus FE1.1s, `1a40:0801`) with three devices behind it: a C-Media sound chip (`0d8c:013c`), the CH340 CAT port (`1a86:7523`) and a CSR chip that is itself a small hub (`0a12:4010`) with a second audio device behind it (`0a12:4007`). The ESP32-S3 has too few USB host channels to keep all of these open and still open the CH340. Once enumeration has settled, the app switches off the hub ports of the sound chip and the whole CSR branch, then opens the CH340.

When the radio is plugged in, the hub sometimes fails to reset one of its ports, and ESP-IDF doesn't retry that. If the hub appears but no serial port shows up, the app re-enumerates the bus automatically, up to 3 times. `Fn+Z` does the same by hand.

Nothing in the app is FX-4CR specific: hub support, freeing host channels and the CH340 driver work for any device. The *FX-4CR* preset (in *Ham CAT (text)*) sets USB, 115200 8N1, Enter sends nothing (CAT commands end with `;`), line mode, and **DTR and RTS off**, because RTS on keys the transmitter.

Then type commands, for example:

| Command | Meaning |
|---------|---------|
| `FA;` | Read VFO A frequency, e.g. reply `FA00014074000;` = 14.074 MHz |
| `FA00007074000;` | Set VFO A to 7.074 MHz |
| `IF;` | Status (frequency, mode, TX/RX) |
| `MD;` | Read mode |
| `ID;` | Radio ID |
| `AI0;` | Turn off auto-information, so the radio stops sending unrequested status |
| `TX1;` / `RX;` | Transmit / receive. **This keys the transmitter**; only use it with an antenna or dummy load connected |

Hamlib users report that this radio's CAT link only works without RTS/CTS handshaking, and that `AI1` can interleave unsolicited `TX`/`RX` messages with replies ([Hamlib #2195](https://github.com/Hamlib/Hamlib/issues/2195)).

The radio stays silent when it gets a command it doesn't know, and commands are case-sensitive: `fa;` gets no reply, `FA;` does.

### Hardware notes

- **Cable:** you need a USB-C OTG adapter or cable. The Cardputer's port is USB-C, so use C-to-A-female with the device's normal cable, or a direct C-to-whatever OTG cable.
- **Power:** a USB host normally supplies 5 V to the device. I couldn't confirm that the Cardputer ADV outputs 5 V on its USB-C port; M5Stack documents it only for charging and programming. If the adapter doesn't show up, it probably has no power. Use an **OTG Y-cable with a separate power input**, which feeds 5 V to the device while the data lines go to the Cardputer. A **self-powered USB hub** between the Cardputer and the device may also work, but some hubs stay off unless the host supplies 5 V.
- **Flashing:** while USB host mode is active, the USB-C port is no longer the Cardputer's own USB serial/JTAG. To flash over USB, switch the port back to Grove and reboot, or start the board in download mode (hold `G0` while plugging in or resetting). Installing through Launcher from the SD card is not affected.
- **Startup:** USB host mode starts only when the port is set to USB. In Grove mode, the USB-C port behaves as before.

## Baud rate detection

Detection only runs when you press `Fn+B`. It cycles through the common rates, from 115200 down to 1200 and up to 2 Mbaud, and picks the first one where the received data is readable text. The device has to be sending something while it scans. The rate it finds is saved, so the next launch starts with it immediately. In the status bar, `A:` before the baud rate means the rate came from detection.

## Local echo

`Fn+E` or the settings menu cycles through three modes:

| Mode | What you see |
|------|--------------|
| **On, hide echo** (default) | What you type appears in the TX color. Many devices (Linux consoles, U-Boot, most shells) echo your input back. Those bytes are recognized and hidden, so lines don't show up twice. Received bytes are only hidden once the *whole* sent text has come back; a reply that merely starts the same way (a CAT reply `FA00014074000;` to `FA;`) is shown in full. Devices that don't echo work the same way. |
| **On, show all** | What you type appears in the TX color, and everything received is shown in the RX color, including the echo. Use this for loopback tests (TX wired to RX). |
| **Off** | Nothing you type is shown locally. You only see what the device sends back. |

In *hide echo* mode, a TX-RX loopback looks like nothing was received, because the looped-back bytes are exactly the echo being hidden.

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

You need [PlatformIO Core](https://platformio.org/install/cli) and Python 3. The project uses Arduino-ESP32 3.3 (ESP-IDF 5.5) through the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform, which `pio run` downloads automatically. The first build downloads about 1-2 GB and can take 10-15 minutes. Later builds take under a minute.

### Windows

```powershell
# One-time install of PlatformIO Core into its own virtualenv
Invoke-WebRequest https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -OutFile get-platformio.py
python get-platformio.py
$env:Path += ";$env:USERPROFILE\.platformio\penv\Scripts"

# The Arduino 3.x packages contain paths longer than Windows' 260-character limit.
# Either enable long paths (Group Policy "Enable Win32 long paths", needs admin),
# or keep PlatformIO's packages in a short folder:
$env:PLATFORMIO_CORE_DIR = "C:\pio"

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

On Windows, run the build from PowerShell or cmd. Git Bash also works for normal builds, but some ESP-IDF tools refuse to run under MSYS.

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
3. Optional: check the image with `esptool --chip esp32s3 image-info dist/UART_Shell_v<ver>_CardputerADV.bin`. The checksum and validation hash should both say `valid`.
4. Attach the two files to a GitHub release. `dist/` isn't tracked by git because `*.bin` is ignored.

To flash the merged image over USB:

```bash
esptool --chip esp32s3 write-flash 0x0 dist/UART_Shell_v1.2_CardputerADV_merged.bin
```

The same files also stay in `.pio/build/cardputer-adv/` as `firmware.bin` and `uart_shell_merged.bin`.

## Using it with Launcher

Copy `dist/UART_Shell_v<ver>_CardputerADV.bin` to the SD card (any folder). On the Cardputer, open Launcher, choose **SD**, select the file and then **Install**. The app doesn't use SPIFFS or FAT partitions, so Launcher only installs the 535 KB app image. Settings are stored in NVS under the `uartshell` namespace, so they survive reinstalls and switching between apps. To get back to Launcher, reset the device or use *Reboot* in the settings menu.

## Project layout

```
src/main.cpp       terminal, RX/TX handling, baud detection, menu and shortcuts
src/term.*         scrollback ring buffer (2000 lines x 40 columns)
src/keyboard.*     TCA8418 keyboard driver for the Cardputer ADV
src/display.h      fixed ST7789 panel config (LovyanGFX)
src/sdlog.*        buffered SD session logger
src/usbserial.*    USB host driver for CDC-ACM, CH34x, CP210x, FTDI and PL2303 adapters
src/presets.*      built-in presets and the /uart_presets.txt reader/writer
src/settings.*     settings stored in NVS
partitions.csv     8 MB layout with two OTA app slots and no data partitions
tools/merge_bin.py post-build step that creates the merged image and dist/ package
```

## Limitations

- Supports the Cardputer **ADV** only (the TCA8418 keyboard). The original Cardputer uses a GPIO keyboard matrix and is not supported.
- ANSI escape sequences (colors, cursor movement) are removed, not interpreted. Full-screen programs like `vi` or `top` won't display correctly.
- USB host mode has one active serial port at a time (switch with `Fn+P`). Up to 6 devices and 8 serial ports are tracked.
- The ESP32-S3 USB controller has only 8 host channels. Every attached device uses one, a hub uses one more, and an open serial port uses 2-3. To leave room for the serial chip, devices without a serial port (sound chips, HID) are switched off at their hub port after enumeration. This only works for devices behind a hub, not for one plugged in directly. If you still see "no free USB host channels", unplug other devices from the hub.
- The USB-serial support is written from the chip vendors' and Linux drivers' documented behavior. The FX-4CR USB layout (a hub with a CH340 inside) is inferred from its manual, not checked against the radio.
- Baud detection needs the target to send text. It can't detect binary protocols, so set the rate by hand for those.
