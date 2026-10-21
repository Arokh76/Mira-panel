#pragma once
// Mira Panel universal display adapter for ONX2432G028 (240x320).
// Based on the OpenNextion Arduino board reference.
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_CST8XX.h>
#include <Adafruit_PCF8574.h>
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Mira ONX2432G028 needs an ESP32-S3"
#endif
class MiraNextion28Display {
public:
  MiraNextion28Display()
    : _bus(3, 2, 5, 1, -1, &SPI, true),
      _lcd(&_bus, GFX_NOT_DEFINED, 0, false, 240, 320) {}

  bool begin() {
    Serial.println("[MIRA][NEXTION] Init ST7789 + CST826");
    Wire.begin(8, 7, 400000);
    if (!_expander.begin(0x38, &Wire)) {
      Serial.println("[MIRA][NEXTION] PCF8574 not found at 0x38");
      return false;
    }
    // LCD reset via PCF8574 P6; leave other outputs high.
    _expander.digitalWriteByte(0xFF);
    delay(20);
    _expander.digitalWriteByte(0xBF);
    delay(120);
    _expander.digitalWriteByte(0xFF);
    delay(120);
    if (!_lcd.begin(80000000)) {
      Serial.println("[MIRA][NEXTION] LCD ST7789 init failure");
      return false;
    }
    _lcd.setRotation(0);
    _lcd.fillScreen(0);
    ledcSetup(7, 5000, 8);
    ledcAttachPin(6, 7);
    ledcWrite(7, 0);
    _touchReady = _touch.begin(&Wire, 0x15);
    if (!_touchReady) Serial.println("[MIRA][NEXTION] CST826 init failed");
    return true;
  }
  void setRotation(uint8_t r) { _lcd.setRotation(r & 3); }
  void setBrightness(uint8_t raw) { ledcWrite(7, raw); }
  void miraFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *p) {
    if (!p || w <= 0 || h <= 0) return;
#if LV_COLOR_16_SWAP
    _lcd.draw16bitBeRGBBitmap(x, y, (uint16_t *)p, w, h);
#else
    _lcd.draw16bitRGBBitmap(x, y, (uint16_t *)p, w, h);
#endif
  }
  bool miraGetTouch(uint16_t *x, uint16_t *y) {
    if (!_touchReady || !x || !y || !_touch.touched()) return false;
    CST_TS_Point p = _touch.getPoint();
    *x = constrain((int)p.x, 0, 239);
    *y = constrain((int)p.y, 0, 319);
    return true;
  }
private:
  Arduino_HWSPI _bus;
  Arduino_ST7789 _lcd;
  Adafruit_CST8XX _touch;
  Adafruit_PCF8574 _expander;
  bool _touchReady = false;
};
