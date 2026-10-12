#pragma once
#include <stddef.h>
#include <stdint.h>

// USB host driver for USB-serial adapters plugged into the Cardputer's USB-C port,
// directly or through a hub. Supports CDC-ACM, WCH CH34x, Silabs CP210x and FTDI chips.
// Every serial port found (several chips, multi-port chips) is listed; one is active.
// Starting it switches the USB PHY to host mode (USB flashing then needs download mode).
namespace usbserial {

// Identifies a port across reconnects: chip IDs, interface number, hub port (0 = direct).
struct PortId {
  uint16_t vid, pid;
  uint8_t iface, hubPort;
};

void begin();  // installs the USB host stack; safe to call more than once
bool connected();
const char *name();  // chip of the active port: "CH34x", "CP210x", "FTDI", "CDC" or ""

// Ports currently attached. The active one is chosen automatically: the preferred port
// if present, otherwise the first one found. selectPort() switches and connects.
int portCount();
int activePort();  // -1 if none
void portLabel(int i, char *out, size_t n);  // e.g. "CH34x 1a86:7523 hub2"
PortId portId(int i);
bool selectPort(int i);
void setPreferred(const PortId &id);

// Line settings are kept and re-applied on every (re)connect.
// dataBits 7/8, parity 0 none / 1 odd / 2 even, stopBits 1/2.
void setLine(uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits);
void setBaud(uint32_t baud);
void setLines(bool dtr, bool rts);

size_t read(uint8_t *buf, size_t n);  // non-blocking
size_t write(const uint8_t *buf, size_t n);
void flushInput();

enum Event { EV_NONE, EV_CONNECTED, EV_DISCONNECTED };

// Call from loop(): handles attached/removed devices and (re)connects the active port.
// If a hub appears but no serial port shows up behind it (e.g. a hub port reset failed),
// the bus is re-enumerated automatically a few times.
Event poll();

// Power-cycle the USB port so every device enumerates again.
void reenumerate();

// Diagnostic messages (host start-up, attached devices, setup errors) for the terminal.
bool nextLog(char *out, size_t n);
// Queue a summary of the USB host state, devices and ports as log messages.
void report();

}  // namespace usbserial
