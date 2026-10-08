#pragma once
#include <stddef.h>
#include <stdint.h>

// Session log on the SD card: /uart_logs/uart_NNNN.txt
// RX text is written line by line, TX lines are prefixed with "> ".
namespace sdlog {

bool start(const char *header);  // mounts the card if needed
void stop();
bool active();
const char *fileName();

void rx(uint8_t c);  // printable chars, '\t' and '\n'; others are escaped
void tx(const char *s, size_t n);  // one complete TX line (no line ending)
void note(const char *s);  // "# ..." comment line
void poll();  // periodic flush

}  // namespace sdlog
