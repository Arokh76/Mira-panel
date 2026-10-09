from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'
ws = root / 'Waveshare43CDisplay.h'

def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

replace_once(ino, '"0.6.33"', '"0.6.34"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.33 initialise', '[MIRA] Firmware 0.6.34 initialise', 'startup log')

# For Waveshare, stop adapting the WT32/LVGL path. This wrapper now delegates
# LCD + touch + LVGL transport to Waveshare's untouched 12_lvgl_transplant sources
# copied under src/WaveshareOfficial by the build workflow.
ws.write_text(r'''#pragma once

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
    // Keep this diagnostic build on the same electrical path as the official
    // demo: only gate the backlight. Do not program the expander PWM register.
    IO_EXTENSION_Output(IO_EXTENSION_IO_2, raw == 0 ? 0 : 1);
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
''', encoding='utf-8')

start_marker = '  lv_init();\\n'
end_marker = '  static lv_indev_drv_t indevDrv;\\n'
text = ui.read_text(encoding='utf-8')
if text.count(start_marker) != 1 or text.count(end_marker) != 1:
    raise SystemExit('Waveshare LVGL init markers not unique')
start = text.index(start_marker)
end = text.index(end_marker, start)
new_init = '''#if defined(MIRA_TARGET_WAVESHARE_43C)
  // Important: unlike 0.6.25..0.6.33, Mira does not create or register the
  // Waveshare display driver here. The official Waveshare port does all of it.
  if (!_display.miraLvglBegin()) {
    Serial.println("[MIRA][WAVESHARE] ERREUR demarrage port LVGL officiel");
    return;
  }
  if (!_display.miraLvglLock(-1)) {
    Serial.println("[MIRA][WAVESHARE] ERREUR verrou LVGL initial");
    return;
  }
#else
  lv_init();
  lv_disp_draw_buf_init(&_drawBuffer, _buf1, _buf2, MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = MIRA_DISPLAY_WIDTH;
  dispDrv.ver_res = MIRA_DISPLAY_HEIGHT;
  dispDrv.flush_cb = displayFlush;
  dispDrv.draw_buf = &_drawBuffer;
  lv_disp_drv_register(&dispDrv);
#endif

'''
ui.write_text(text[:start] + new_init + text[end:], encoding='utf-8')



replace_once(
    ui,
    '''  _lastActivity = millis();
  applyTheme();
  setNavActive(0);
}
''',
    '''  _lastActivity = millis();
  applyTheme();
  setNavActive(0);

#if defined(MIRA_TARGET_WAVESHARE_43C)
  // Force a complete first frame while still holding the official LVGL mutex.
  lv_obj_invalidate(_screen);
  lv_refr_now(nullptr);
  _display.miraLvglUnlock();
#endif
}
''',
    'unlock official LVGL after initial Mira frame',
)

replace_once(
    ui,
    '''void MiraPanelUI::loop() {
  if (_pendingNavPage >= 0) {
''',
    '''void MiraPanelUI::loop() {
#if defined(MIRA_TARGET_WAVESHARE_43C)
  if (!_display.miraLvglLock(20)) return;
#endif

  if (_pendingNavPage >= 0) {
''',
    'lock official LVGL during Mira UI loop',
)

replace_once(
    ui,
    '''  if (_sleeping && _clockInSleep) updateSleepClock();

  lv_timer_handler();
}
''',
    '''  if (_sleeping && _clockInSleep) updateSleepClock();

#if defined(MIRA_TARGET_WAVESHARE_43C)
  // The official Waveshare lvgl task owns lv_timer_handler().
  _display.miraLvglUnlock();
#else
  lv_timer_handler();
#endif
}
''',
    'use official Waveshare LVGL task',
)

print('Mira Panel 0.6.34 applied: untouched Waveshare LCD/touch/LVGL port owns the display path')
