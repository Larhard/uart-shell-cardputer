#pragma once
// Fixed ST7789 config for the Cardputer / Cardputer ADV (values from M5GFX autodetect).
// Skipping autodetect keeps boot fast and the binary small.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host = SPI3_HOST;
      c.spi_mode = 0;
      c.freq_write = 40000000;
      c.freq_read = 16000000;
      c.spi_3wire = true;
      c.use_lock = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 36;
      c.pin_mosi = 35;
      c.pin_miso = -1;
      c.pin_dc = 34;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs = 37;
      c.pin_rst = 33;
      c.pin_busy = -1;
      c.panel_width = 135;
      c.panel_height = 240;
      c.offset_x = 52;
      c.offset_y = 40;
      c.offset_rotation = 0;
      c.readable = false;
      c.invert = true;
      c.bus_shared = false;
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl = 38;
      c.invert = false;
      c.freq = 256;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
