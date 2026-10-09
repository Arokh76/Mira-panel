from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'
hdr = root / 'MiraPanelUI.h'
profile = root / 'MiraDisplayProfile.h'
sc01 = root / 'SC01PlusDisplay.h'

def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

replace_once(ino, '"0.6.24"', '"0.6.25"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.24 initialise', '[MIRA] Firmware 0.6.25 initialise', 'startup log')

profile.write_text(r'''#pragma once

// Mira hardware profile. UI/business logic must never depend on one panel.
#if defined(MIRA_TARGET_WAVESHARE_43C)
  #define MIRA_DISPLAY_PROFILE_ID "waveshare-esp32s3-touch-lcd-4.3c"
  #define MIRA_HARDWARE_NAME "WAVESHARE-ESP32-S3-TOUCH-LCD-4.3C"
  #define MIRA_DISPLAY_WIDTH 800
  #define MIRA_DISPLAY_HEIGHT 480
  #define MIRA_NAV_HEIGHT 50
  #define MIRA_CONTENT_HEIGHT (MIRA_DISPLAY_HEIGHT - MIRA_NAV_HEIGHT)
  #define MIRA_DISPLAY_ROTATION 0
  // First conservative RGB-panel bring-up: 2 x 800 x 10 x RGB565 = 32 KB.
  #define MIRA_DRAW_BUFFER_LINES 10
#else
  #define MIRA_DISPLAY_PROFILE_ID "wt32-sc01-plus"
  #define MIRA_HARDWARE_NAME "WT32-SC01-PLUS"
  #define MIRA_DISPLAY_WIDTH 320
  #define MIRA_DISPLAY_HEIGHT 480
  #define MIRA_NAV_HEIGHT 46
  #define MIRA_CONTENT_HEIGHT (MIRA_DISPLAY_HEIGHT - MIRA_NAV_HEIGHT)
  #define MIRA_DISPLAY_ROTATION 0
  // Hardware validated on WT32: 32 lines causes Wi-Fi instability; keep 20.
  #define MIRA_DRAW_BUFFER_LINES 20
#endif

#define MIRA_LAYOUT_LOGICAL_WIDTH 1000
#define MIRA_LAYOUT_LOGICAL_HEIGHT 1000
''', encoding='utf-8')

(root / 'MiraHardware.h').write_text(r'''#pragma once
#include "MiraDisplayProfile.h"

#if defined(MIRA_TARGET_WAVESHARE_43C)
  #include "Waveshare43CDisplay.h"
  using MiraDisplay = Waveshare43CDisplay;
#else
  #include "SC01PlusDisplay.h"
  using MiraDisplay = SC01PlusDisplay;
#endif
''', encoding='utf-8')

insert = r'''
  void miraFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
    if (getStartCount() == 0) startWrite();
    pushImageDMA(x, y, w, h, (lgfx::swap565_t*)pixels);
    waitDMA();
  }

  bool miraGetTouch(uint16_t* x, uint16_t* y) {
    return getTouch(x, y);
  }
'''
text = sc01.read_text(encoding='utf-8')
anchor = '    setPanel(&_panel);\n  }\n};\n'
if text.count(anchor) != 1:
    raise SystemExit('SC01 adapter anchor not found')
text = text.replace(anchor, '    setPanel(&_panel);\n  }\n' + insert + '};\n', 1)
sc01.write_text(text, encoding='utf-8')

(root / 'Waveshare43CDisplay.h').write_text(r'''#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Mira Waveshare 4.3C requires ESP32-S3"
#endif

class Waveshare43CDisplay {
public:
  bool begin() {
    Serial.println("[MIRA][WAVESHARE] Initialisation I2C / IO expander");
    Wire.begin(8, 9, 400000);
    initIoExpander();

    // Board IO1 resets GT911, IO3 resets LCD. Values/pins follow Waveshare 4.3C.
    setIo(3, false);
    delay(20);
    setIo(3, true);
    delay(30);

    initTouch();

    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = 16 * 1000 * 1000;
    cfg.timings.h_res = 800;
    cfg.timings.v_res = 480;
    cfg.timings.hsync_pulse_width = 8;
    cfg.timings.hsync_back_porch = 8;
    cfg.timings.hsync_front_porch = 4;
    cfg.timings.vsync_pulse_width = 8;
    cfg.timings.vsync_back_porch = 8;
    cfg.timings.vsync_front_porch = 4;
    cfg.timings.flags.pclk_active_neg = 1;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 1;
    cfg.bounce_buffer_size_px = 800 * 10;
    cfg.sram_trans_align = 4;
    cfg.psram_trans_align = 64;
    cfg.hsync_gpio_num = GPIO_NUM_46;
    cfg.vsync_gpio_num = GPIO_NUM_3;
    cfg.de_gpio_num = GPIO_NUM_5;
    cfg.pclk_gpio_num = GPIO_NUM_7;
    cfg.disp_gpio_num = GPIO_NUM_NC;

    const int pins[16] = {
      GPIO_NUM_14, GPIO_NUM_38, GPIO_NUM_18, GPIO_NUM_17, GPIO_NUM_10,
      GPIO_NUM_39, GPIO_NUM_0, GPIO_NUM_45, GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_21,
      GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_40
    };
    for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = pins[i];
    cfg.flags.fb_in_psram = 1;

    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &_panel);
    if (err != ESP_OK) {
      Serial.printf("[MIRA][WAVESHARE] esp_lcd_new_rgb_panel erreur=%d\n", (int)err);
      return false;
    }
    err = esp_lcd_panel_reset(_panel);
    if (err == ESP_OK) err = esp_lcd_panel_init(_panel);
    if (err != ESP_OK) {
      Serial.printf("[MIRA][WAVESHARE] initialisation LCD erreur=%d\n", (int)err);
      return false;
    }

    setIo(2, true);
    setBrightness(220);
    Serial.println("[MIRA][WAVESHARE] LCD RGB 800x480 + GT911 prets");
    return true;
  }

  void setRotation(uint8_t rotation) {
    _rotation = rotation;
  }

  void setBrightness(uint8_t raw) {
    uint8_t duty = raw;
    if (duty > 242) duty = 242;
    writeExpander(0x05, duty);
    setIo(2, raw != 0);
  }

  void miraFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
    if (!_panel || !pixels || w <= 0 || h <= 0) return;
    esp_lcd_panel_draw_bitmap(_panel, x, y, x + w, y + h, pixels);
  }

  bool miraGetTouch(uint16_t* x, uint16_t* y) {
    if (!x || !y) return false;

    uint8_t status = 0;
    if (!gtRead(0x814E, &status, 1)) return false;
    if ((status & 0x80) == 0) return false;

    uint8_t count = status & 0x0F;
    if (count == 0 || count > 5) {
      gtClearStatus();
      return false;
    }

    uint8_t point[8] = {0};
    if (!gtRead(0x814F, point, sizeof(point))) {
      gtClearStatus();
      return false;
    }
    gtClearStatus();

    uint16_t tx = (uint16_t)point[0] | ((uint16_t)point[1] << 8);
    uint16_t ty = (uint16_t)point[2] | ((uint16_t)point[3] << 8);
    if (tx >= 800 || ty >= 480) return false;

    *x = tx;
    *y = ty;
    return true;
  }

private:
  static constexpr uint8_t IO_ADDR = 0x24;
  static constexpr uint8_t GT911_ADDR = 0x5D;
  esp_lcd_panel_handle_t _panel = nullptr;
  uint8_t _ioState = 0xFF;
  uint8_t _rotation = 0;

  bool writeExpander(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(IO_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
  }

  void initIoExpander() {
    _ioState = 0xFF;
    writeExpander(0x02, 0xFF);
    writeExpander(0x03, _ioState);
  }

  void setIo(uint8_t pin, bool high) {
    if (pin > 7) return;
    if (high) _ioState |= (uint8_t)(1U << pin);
    else _ioState &= (uint8_t)~(1U << pin);
    writeExpander(0x03, _ioState);
  }

  void initTouch() {
    pinMode(4, OUTPUT);
    setIo(1, false);
    delay(100);
    digitalWrite(4, LOW);
    delay(100);
    setIo(1, true);
    delay(200);
    pinMode(4, INPUT);

    uint8_t product[3] = {0};
    if (gtRead(0x8140, product, sizeof(product))) {
      Serial.printf("[MIRA][WAVESHARE] GT911 ID=%c%c%c\n", product[0], product[1], product[2]);
    } else {
      Serial.println("[MIRA][WAVESHARE] GT911 non lu sur 0x5D");
    }
  }

  bool gtRead(uint16_t reg, uint8_t* dst, size_t len) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    size_t got = Wire.requestFrom((int)GT911_ADDR, (int)len);
    if (got != len) {
      while (Wire.available()) Wire.read();
      return false;
    }
    for (size_t i = 0; i < len; ++i) dst[i] = (uint8_t)Wire.read();
    return true;
  }

  bool gtWrite(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
  }

  void gtClearStatus() {
    gtWrite(0x814E, 0x00);
  }
};
''', encoding='utf-8')

replace_once(ino, '#include "SC01PlusDisplay.h"\n', '#include "MiraHardware.h"\n', 'main hardware include')
replace_once(ino, 'SC01PlusDisplay Ecran;\n', 'MiraDisplay Ecran;\n', 'main display type')
replace_once(ino, '    "WT32-SC01-PLUS",\n', '    MIRA_HARDWARE_NAME,\n', 'module hardware name')

replace_once(hdr, '#include "SC01PlusDisplay.h"\n', '#include "MiraHardware.h"\n', 'UI hardware include')
replace_once(hdr, '  MiraPanelUI(MiraPanelServer& server, SC01PlusDisplay& display);\n', '  MiraPanelUI(MiraPanelServer& server, MiraDisplay& display);\n', 'UI constructor declaration')
replace_once(hdr, '  SC01PlusDisplay& _display;\n', '  MiraDisplay& _display;\n', 'UI display member')
replace_once(hdr, '  static SC01PlusDisplay* _displayForLvgl;\n', '  static MiraDisplay* _displayForLvgl;\n', 'UI static display pointer')

replace_once(ui, 'SC01PlusDisplay* MiraPanelUI::_displayForLvgl = nullptr;\n', 'MiraDisplay* MiraPanelUI::_displayForLvgl = nullptr;\n', 'UI static display type')
replace_once(ui, 'MiraPanelUI::MiraPanelUI(MiraPanelServer& server, SC01PlusDisplay& display)\n', 'MiraPanelUI::MiraPanelUI(MiraPanelServer& server, MiraDisplay& display)\n', 'UI constructor definition')

old_flush = '''  if (_displayForLvgl->getStartCount() == 0) _displayForLvgl->startWrite();

  _displayForLvgl->pushImageDMA(
    area->x1,
    area->y1,
    area->x2 - area->x1 + 1,
    area->y2 - area->y1 + 1,
    (lgfx::swap565_t*)&color_p->full
  );
  _displayForLvgl->waitDMA();
'''
new_flush = '''  _displayForLvgl->miraFlush(
    area->x1,
    area->y1,
    area->x2 - area->x1 + 1,
    area->y2 - area->y1 + 1,
    (const uint16_t*)&color_p->full
  );
'''
replace_once(ui, old_flush, new_flush, 'generic display flush')
replace_once(ui, '_displayForLvgl->getTouch(&x, &y)', '_displayForLvgl->miraGetTouch(&x, &y)', 'generic touch read')

replace_once(
    ui,
    '    lv_obj_set_size(_navButtons[i], 106, 44);\n',
    '    lv_obj_set_size(_navButtons[i], MIRA_DISPLAY_WIDTH / 3, MIRA_NAV_HEIGHT - 2);\n',
    'profile nav button size',
)
replace_once(
    ui,
    '    lv_obj_set_pos(btn, 1 + slot * 106, 1);\n',
    '    lv_obj_set_pos(btn, 1 + slot * (MIRA_DISPLAY_WIDTH / 3), 1);\n',
    'profile nav button position',
)

print('Mira Panel 0.6.25 applied: universal display adapter + Waveshare 4.3C profile')
