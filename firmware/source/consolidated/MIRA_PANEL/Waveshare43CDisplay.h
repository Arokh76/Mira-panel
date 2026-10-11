#pragma once

#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Mira Waveshare 4.3C requires ESP32-S3"
#endif

#include "src/WaveshareOfficial/i2c/i2c.h"
#include "src/WaveshareOfficial/io_extension/io_extension.h"
#include "src/WaveshareOfficial/gt911/gt911.h"
#include "src/WaveshareOfficial/rgb_lcd_port/rgb_lcd_port.h"
#include "src/WaveshareOfficial/lvgl_port/lvgl_port.h"

class Waveshare43CDisplay {
public:
  bool begin() {
    Serial.println("[MIRA][WAVESHARE] Initialisation materielle via port officiel Waveshare");

    DEV_I2C_Init();
    IO_EXTENSION_Init();

    // Exact same order as examples/arduino/12_lvgl_transplant.
    _touch = touch_gt911_init(DEV_I2C_Get_Bus_Device());
    _panel = waveshare_esp32_s3_rgb_lcd_init();
    waveshare_rgb_lcd_bl_on();

    if (!_panel || !_touch) {
      Serial.println("[MIRA][WAVESHARE] ERREUR initialisation officielle LCD/tactile");
      return false;
    }

    Serial.println("[MIRA][WAVESHARE] LCD + GT911 initialises par les sources Waveshare");
    return true;
  }

  bool miraLvglBegin() {
    if (!_panel) return false;

    // The official port owns: lv_init(), esp_timer tick, panel framebuffers,
    // direct_mode, VSYNC notification and the pinned LVGL task.
    esp_err_t err = lvgl_port_init(_panel, nullptr);
    if (err != ESP_OK) {
      Serial.printf("[MIRA][WAVESHARE] lvgl_port_init erreur=%d\n", (int)err);
      return false;
    }

    Serial.println("[MIRA][WAVESHARE] Port LVGL Waveshare officiel actif");
    return true;
  }

  bool miraLvglLock(int timeoutMs = -1) {
    return lvgl_port_lock(timeoutMs);
  }

  void miraLvglUnlock() {
    lvgl_port_unlock();
  }

  void setRotation(uint8_t rotation) {
    (void)rotation; // Official port is fixed to native landscape rotation 0.
  }

  void setBrightness(uint8_t raw) {
    if (raw == 0) {
      // Real OFF is reserved for Mira sleep/off.
      IO_EXTENSION_Output(IO_EXTENSION_IO_2, 0);
      return;
    }

    // Restore the backlight gate first in case we are waking from OFF.
    IO_EXTENSION_Output(IO_EXTENSION_IO_2, 1);

    // Mira internally uses raw 30..255 for 1..100%.
    // Waveshare PWM is inverted (0 = max brightness). Values near 95 are
    // visually almost black on this panel, so keep the interactive minimum
    // safely visible by limiting the hardware duty to 80.
    uint8_t pct;
    if (raw <= 30) {
      pct = 1;
    } else {
      pct = (uint8_t)(1 + ((uint16_t)(raw - 30) * 99U + 112U) / 225U);
      if (pct > 100) pct = 100;
    }

    // 1..100% Mira -> 80..0 Waveshare PWM.
    uint8_t wsDuty = (uint8_t)(((uint16_t)(100U - pct) * 80U + 49U) / 99U);
    IO_EXTENSION_Pwm_Output(wsDuty);
  }

  void miraFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
    // Retained only so the generic Mira display adapter still compiles.
    // The Waveshare LVGL display driver never calls this method in 0.6.34.
    if (!_panel || !pixels || w <= 0 || h <= 0) return;
    esp_lcd_panel_draw_bitmap(_panel, x, y, x + w, y + h, pixels);
  }

  bool miraGetFrameBuffers(void** fb1, void** fb2) {
    if (!_panel || !fb1 || !fb2) return false;
    *fb1 = nullptr;
    *fb2 = nullptr;
    return esp_lcd_rgb_panel_get_frame_buffer(_panel, 2, fb1, fb2) == ESP_OK
        && *fb1 != nullptr && *fb2 != nullptr;
  }

  bool miraGetTouch(uint16_t* x, uint16_t* y) {
    if (!_touch || !x || !y) return false;

    esp_err_t err = esp_lcd_touch_read_data(_touch);
    if (err != ESP_OK) return false;

    uint16_t tx = 0;
    uint16_t ty = 0;
    uint8_t count = 0;
    bool pressed = esp_lcd_touch_get_coordinates(
      _touch, &tx, &ty, nullptr, &count, 1
    );

    if (!pressed || count == 0 || tx >= 800 || ty >= 480) return false;
    *x = tx;
    *y = ty;
    return true;
  }

private:
  esp_lcd_panel_handle_t _panel = nullptr;
  esp_lcd_touch_handle_t _touch = nullptr;
};
