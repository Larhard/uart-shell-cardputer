#pragma once
#include <stddef.h>
#include <stdint.h>

// USB host driver for USB-serial adapters plugged into the Cardputer's USB-C port.
// Supports CDC-ACM, WCH CH34x, Silabs CP210x and FTDI FT232R/BM-style chips.
// Starting it switches the USB PHY to host mode (USB flashing then needs download mode).
namespace usbserial {

void begin();  // installs the USB host stack; safe to call more than once
bool connected();
const char *name();  // "CH34x", "CP210x", "FTDI", "CDC" or ""

// Line settings are kept and re-applied on every (re)connect.
// dataBits 7/8, parity 0 none / 1 odd / 2 even, stopBits 1/2.
void setLine(uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits);
void setBaud(uint32_t baud);
void setLines(bool dtr, bool rts);

size_t read(uint8_t *buf, size_t n);  // non-blocking
size_t write(const uint8_t *buf, size_t n);
void flushInput();

enum Event { EV_NONE, EV_CONNECTED, EV_DISCONNECTED, EV_UNSUPPORTED };

// Call from loop(): finishes connecting newly attached devices, cleans up removed ones.
Event poll();

}  // namespace usbserial
