# Third-party notices

UART Shell's own source code is licensed under the [MIT License](LICENSE). The firmware
binary (`UART_Shell_*.bin`) also contains the third-party software listed below, each under
its own license. The list covers what is actually linked into the image, taken from the
linker map of the v1.3 build; optional parts of the frameworks that are not linked (Wi-Fi,
Bluetooth host, TinyUSB, camera, ...) are not included.

| Component | Version | License | Full text |
|-----------|---------|---------|-----------|
| [Arduino-ESP32 core](https://github.com/espressif/arduino-esp32) | 3.3.12 | LGPL-2.1-or-later; Espressif HAL files Apache-2.0 | [LGPL-2.1](licenses/LGPL-2.1.txt), [Apache-2.0](licenses/Apache-2.0.txt) |
| [ESP-IDF](https://github.com/espressif/esp-idf) (USB host, drivers, NVS, HAL, ...) | 5.5.5 | Apache-2.0 | [Apache-2.0](licenses/Apache-2.0.txt) |
| [esp_diagnostics](https://components.espressif.com/components/espressif/esp_diagnostics) | 1.3.4 | Apache-2.0 | [Apache-2.0](licenses/Apache-2.0.txt) |
| Espressif PHY / coexistence libraries (binary) | ESP-IDF 5.5.5 | Apache-2.0 | [Apache-2.0](licenses/Apache-2.0.txt) |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) (crypto part, via ESP-IDF) | ESP-IDF 5.5.5 | Apache-2.0 (used under Apache-2.0; also offered under GPL-2.0-or-later) | [Apache-2.0](licenses/Apache-2.0.txt) |
| [FreeRTOS kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) (ESP-IDF port) | ESP-IDF 5.5.5 | MIT | [below](#freertos-kernel) |
| [FatFs](http://elm-chan.org/fsw/ff/) R0.15 (via ESP-IDF) | R0.15 | FatFs license (BSD-style) | [below](#fatfs) |
| Xtensa HAL (Cadence) | ESP-IDF 5.5.5 | MIT | [below](#xtensa-hal) |
| [newlib](https://sourceware.org/newlib/) C library (toolchain) | xtensa-esp-elf 14.2.0 (2026-01-21) | several BSD-style licenses | [newlib](licenses/newlib.txt) |
| GCC runtime (`libstdc++`, `libgcc`) | 14.2.0 | GPL-3.0 with the GCC Runtime Library Exception | no conditions on the binary |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | 1.2.32 | FreeBSD (BSD-2-Clause); includes code and fonts from Adafruit GFX (BSD), Adafruit ILI9341 (MIT) and TFT_eSPI (FreeBSD) | [LovyanGFX](licenses/LovyanGFX.txt) |

The license texts in [`licenses/`](licenses/) are copied unmodified from the packages the
firmware was built with.

## Notes

**Arduino-ESP32 (LGPL).** The core is linked statically. As the LGPL requires, you can
rebuild the firmware against a modified core: the complete source of this app is in this
repository, and the core's source is available at
<https://github.com/espressif/arduino-esp32/tree/3.3.12>. See [Building](README.md#building).

**ESP-IDF and other Apache-2.0 software.** Copyright 2015-2026 Espressif Systems (Shanghai)
CO LTD, unless stated otherwise in the individual source files. Mbed TLS: Copyright The
Mbed TLS Contributors.

**GCC runtime.** `libstdc++` and `libgcc` are covered by the GCC Runtime Library Exception,
which places no conditions on programs compiled with GCC.

## FreeRTOS kernel

Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
Modifications for ESP-IDF: 2023-2025 Espressif Systems (Shanghai) CO LTD.

```
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## FatFs

```
FatFs - Generic FAT Filesystem module  R0.15

Copyright (C) 2022, ChaN, all right reserved.

FatFs module is an open source software. Redistribution and use of FatFs in
source and binary forms, with or without modification, are permitted provided
that the following condition is met:

1. Redistributions of source code must retain the above copyright notice,
   this condition and the following disclaimer.

This software is provided by the copyright holder and contributors "AS IS"
and any warranties related to this software are DISCLAIMED.
The copyright owner or contributors be NOT LIABLE for any damages caused
by use of this software.
```

## Xtensa HAL

```
Copyright (c) 1999-2015 Cadence Design Systems, Inc.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
