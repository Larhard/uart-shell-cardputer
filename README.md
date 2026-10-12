# UART Shell for M5Stack Cardputer ADV

A small, fast-booting serial terminal for the **M5Stack Cardputer ADV**. Plug a device into the Grove port and start typing.

- Opens straight into the terminal. There is no splash screen or menu at boot.
- 2000-line scrollback.
- RX and TX text in different colors: RX is green, TX is amber, and status messages are blue.
- Set the baud rate by hand (preset list or any custom rate), or detect it automatically on demand. The detected rate is remembered.
- Logs to a timestamped file on the SD card, one file per session.
- Line mode (edit, then send) or raw mode (every key is sent at once).
- Talks to the Grove UART, or to a USB-serial device plugged into the USB-C port (USB host mode). For example, the CAT port of a (tr)uSDX or FX-4CR radio.
- All settings are restored at the next launch, stored in NVS.
- Works with [Launcher](https://bmorcelli.github.io/Launcher/catalog.html). It's a plain app image of about 520 KB with no data partition.

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
| `Fn+U` | Switch the port between the Grove UART and USB host |
| `Fn+S` | Swap the Grove RX/TX pins |
| `Fn+D` / `Fn+T` | Turn the USB adapter's DTR / RTS line on or off |
| `Fn+P` | Switch to the next USB serial port, when several are attached |
| `Fn+X` | Apply the FX-4CR CAT preset (see below) |
| `Fn+I` | USB diagnostics: host state and every device on the bus |
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

`;` / `.` select an item, `,` / `/` / Enter change it, and `` ` ``, Opt or Backspace close the menu. On the *Baud rate* item, type digits and press Enter to set a custom rate. The menu also has an item to reboot the device, which returns you to Launcher.

## USB serial devices (USB host mode)

Set **Port** to *USB* (`Fn+U`, or in the settings menu) and the Cardputer acts as a USB host on its USB-C port. It then talks to a USB-serial device, such as the CAT/serial port of a (tr)uSDX transceiver, the same way it talks to the Grove UART. Baud rate, data format, baud detection, logging and echo all work the same.

Supported devices:

| Device | Examples |
|--------|----------|
| WCH CH340 / CH341 | Most Arduino Nano clones, cheap USB-TTL cables, and likely the (tr)uSDX (check which chip your build uses) |
| Silicon Labs CP210x | CP2102 / CP2104 modules, many ESP32 dev boards |
| FTDI FT232R / FT230X / FT2232H / FT232H | FTDI cables and boards |
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

The BG2FX FX-4CR's USB port has a COM port and a USB sound card behind one cable. According to its manual, the CAT protocol is Kenwood TS-590S at 115200 baud, and PTT is on **RTS**. The device names in the manual suggest a small USB hub inside the radio, with a CH340 and a C-Media sound chip behind it. The app supports hubs and ignores the sound chip.

Press `Fn+X`, or choose *FX-4CR CAT preset* in the settings menu. This sets:

- Port: USB, 115200 baud, 8N1
- *Enter sends*: none, because CAT commands end with `;`
- Line mode
- **DTR and RTS off**, because RTS on keys the transmitter

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

**For (tr)uSDX CAT control:** set the baud rate to match the radio's CAT setting (115200 is common). Set *Enter sends* to `None` (`Fn+N`), because Kenwood-style CAT commands end with `;`, for example `FA;` or `IF;`. Line mode works well for this.

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
| **On, hide echo** (default) | What you type appears in the TX color. Many devices (Linux consoles, U-Boot, most shells) echo your input back. Those bytes are recognized and hidden, so lines don't show up twice. Devices that don't echo work the same way. |
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

Copy `dist/UART_Shell_v<ver>_CardputerADV.bin` to the SD card (any folder). On the Cardputer, open Launcher, choose **SD**, select the file and then **Install**. The app doesn't use SPIFFS or FAT partitions, so Launcher only installs the 520 KB app image. Settings are stored in NVS under the `uartshell` namespace, so they survive reinstalls and switching between apps. To get back to Launcher, reset the device or use *Reboot* in the settings menu.

## Project layout

```
src/main.cpp       terminal, RX/TX handling, baud detection, menu and shortcuts
src/term.*         scrollback ring buffer (2000 lines x 40 columns)
src/keyboard.*     TCA8418 keyboard driver for the Cardputer ADV
src/display.h      fixed ST7789 panel config (LovyanGFX)
src/sdlog.*        buffered SD session logger
src/usbserial.*    USB host driver for CDC-ACM, CH34x, CP210x and FTDI adapters
src/settings.*     settings stored in NVS
partitions.csv     8 MB layout with two OTA app slots and no data partitions
tools/merge_bin.py post-build step that creates the merged image and dist/ package
```

## Limitations

- Supports the Cardputer **ADV** only (the TCA8418 keyboard). The original Cardputer uses a GPIO keyboard matrix and is not supported.
- ANSI escape sequences (colors, cursor movement) are removed, not interpreted. Full-screen programs like `vi` or `top` won't display correctly.
- USB host mode has one active serial port at a time (switch with `Fn+P`). Up to 6 devices and 8 serial ports are tracked.
- The ESP32-S3 USB controller has only 8 host channels. Every attached device uses one, a hub uses one more, and an open serial port uses 2-3. To leave room for the serial chip, USB sound chips, HID and storage devices are released during enumeration. If you still see "no free USB host channels", unplug other devices from the hub.
- The USB-serial support is written from the chip vendors' and Linux drivers' documented behavior. The FX-4CR USB layout (a hub with a CH340 inside) is inferred from its manual, not checked against the radio.
- Baud detection needs the target to send text. It can't detect binary protocols, so set the rate by hand for those.
