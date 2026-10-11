#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <driver/i2c.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Ce projet est prévu pour un ESP32-S3 (WT32-SC01 Plus / ZX3D50CE08S)."
#endif

// Driver minimal du Smart Panlee / Wireless-Tag SC01 Plus (ZX3D50CE08S).
// Brochage et séquence d'initialisation repris du support constructeur PanelLan.
class SC01PlusPanel : public lgfx::Panel_ST7796 {
protected:
  const uint8_t* getInitCommands(uint8_t listno) const override {
    static constexpr uint8_t list0[] = {
      0x11, 0 + CMD_INIT_DELAY, 120,
      0x36, 1, 0x48,
      0x3A, 1, 0x55,
      0xF0, 1, 0xC3,
      0xF0, 1, 0x96,
      0xB4, 1, 0x01,
      0xB5, 1, 0x1E,
      0xB6, 3, 0x80, 0x22, 0x3B,
      0xB7, 1, 0xC6,
      0xB9, 2, 0x02, 0xE0,
      0xC0, 2, 0x80, 0x16,
      0xC1, 1, 0x19,
      0xC2, 1, 0xA7,
      0xC5, 1, 0x16,
      0xE8, 8, 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33,
      0xE0, 14, 0xF0, 0x07, 0x0D, 0x04, 0x05, 0x14, 0x36, 0x54, 0x4C, 0x38, 0x13, 0x14, 0x2E, 0x34,
      0xE1, 14, 0xF0, 0x10, 0x14, 0x0E, 0x0C, 0x08, 0x35, 0x44, 0x4C, 0x26, 0x10, 0x12, 0x2C, 0x32,
      0xF0, 1, 0x3C,
      0xF0, 1 + CMD_INIT_DELAY, 0x69, 120,
      0x29, 0,
      0x21, 0,
      0xFF, 0xFF
    };

    return (listno == 0) ? list0 : nullptr;
  }
};

class SC01PlusDisplay : public lgfx::LGFX_Device {
private:
  lgfx::Bus_Parallel8 _bus;
  SC01PlusPanel _panel;
  lgfx::Touch_FT5x06 _touch;
  lgfx::Light_PWM _light;

public:
  SC01PlusDisplay() {
    {
      auto cfg = _bus.config();
      cfg.port = 0;
      cfg.freq_write = 40 * 1000 * 1000;
      cfg.pin_wr = 47;
      cfg.pin_rd = -1;
      cfg.pin_rs = 0;
      cfg.pin_d0 = 9;
      cfg.pin_d1 = 46;
      cfg.pin_d2 = 3;
      cfg.pin_d3 = 8;
      cfg.pin_d4 = 18;
      cfg.pin_d5 = 17;
      cfg.pin_d6 = 16;
      cfg.pin_d7 = 15;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    {
      auto cfg = _panel.config();
      cfg.pin_cs = -1;
      cfg.pin_rst = 4;
      cfg.pin_busy = -1;
      cfg.memory_width = 320;
      cfg.memory_height = 480;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 2;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }

    {
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = 320;
      cfg.y_min = 0;
      cfg.y_max = 480;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = I2C_NUM_1;
      cfg.pin_int = 7;
      cfg.pin_sda = 6;
      cfg.pin_scl = 5;
      cfg.pin_rst = -1;
      cfg.freq = 400000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }

    {
      auto cfg = _light.config();
      cfg.pin_bl = 45;
      cfg.invert = false;
      cfg.freq = 21111;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }

    setPanel(&_panel);
  }

  void miraFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
    if (getStartCount() == 0) startWrite();
    pushImageDMA(x, y, w, h, (lgfx::swap565_t*)pixels);
    waitDMA();
  }

  bool miraGetTouch(uint16_t* x, uint16_t* y) {
    return getTouch(x, y);
  }
};
