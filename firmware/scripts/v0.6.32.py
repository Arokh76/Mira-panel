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

replace_once(ino, '"0.6.31"', '"0.6.32"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.31 initialise', '[MIRA] Firmware 0.6.32 initialise', 'startup log')

# Match the official Waveshare 12_lvgl_transplant RGB transport:
# two PSRAM framebuffers + 800x10 bounce buffer + frame-finish synchronization.
replace_once(ws, '#include "esp_lcd_panel_rgb.h"\n',
'''#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
''', 'FreeRTOS RGB sync includes')

replace_once(ws, '    cfg.num_fbs = 1;\n', '    cfg.num_fbs = 2;\n', 'official double framebuffer')
replace_once(ws, '    cfg.bounce_buffer_size_px = 0;\n', '    cfg.bounce_buffer_size_px = 800 * 10;\n', 'official bounce buffer')

old_panel_init = '''    err = esp_lcd_panel_reset(_panel);
    if (err == ESP_OK) err = esp_lcd_panel_init(_panel);
    if (err != ESP_OK) {
      Serial.printf("[MIRA][WAVESHARE] initialisation LCD erreur=%d\\n", (int)err);
      return false;
    }

    setIo(2, true);
'''
new_panel_init = '''    err = esp_lcd_panel_reset(_panel);
    if (err == ESP_OK) err = esp_lcd_panel_init(_panel);
    if (err != ESP_OK) {
      Serial.printf("[MIRA][WAVESHARE] initialisation LCD erreur=%d\\n", (int)err);
      return false;
    }

    _frameDone = xSemaphoreCreateBinary();
    if (!_frameDone) {
      Serial.println("[MIRA][WAVESHARE] semaphore VSYNC impossible");
      return false;
    }

    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_bounce_frame_finish = rgbFrameDone;
    err = esp_lcd_rgb_panel_register_event_callbacks(_panel, &cbs, this);
    if (err != ESP_OK) {
      Serial.printf("[MIRA][WAVESHARE] callback RGB erreur=%d\\n", (int)err);
      return false;
    }

    setIo(2, true);
'''
replace_once(ws, old_panel_init, new_panel_init, 'register official-style RGB frame callback')

old_flush = '''  void miraFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
    if (!_panel || !pixels || w <= 0 || h <= 0) return;
    esp_lcd_panel_draw_bitmap(_panel, x, y, x + w, y + h, pixels);
  }

  void* miraGetFrameBuffer() {
    if (!_panel) return nullptr;
    void* fb = nullptr;
    if (esp_lcd_rgb_panel_get_frame_buffer(_panel, 1, &fb) != ESP_OK) return nullptr;
    return fb;
  }
'''
new_flush = '''  void miraFlush(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* pixels) {
    if (!_panel || !pixels || w <= 0 || h <= 0) return;

    // Same principle as Waveshare lvgl_port: switch/copy the RGB framebuffer,
    // then wait until the RGB engine reports the frame boundary before LVGL
    // is allowed to reuse the other buffer.
    if (_frameDone) {
      while (xSemaphoreTake(_frameDone, 0) == pdTRUE) {}
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(_panel, x, y, x + w, y + h, pixels);
    if (err != ESP_OK) {
      Serial.printf("[MIRA][WAVESHARE] draw_bitmap erreur=%d\\n", (int)err);
      return;
    }

    if (_frameDone && xSemaphoreTake(_frameDone, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println("[MIRA][WAVESHARE] timeout synchro frame RGB");
    }
  }

  bool miraGetFrameBuffers(void** fb1, void** fb2) {
    if (!_panel || !fb1 || !fb2) return false;
    *fb1 = nullptr;
    *fb2 = nullptr;
    return esp_lcd_rgb_panel_get_frame_buffer(_panel, 2, fb1, fb2) == ESP_OK
        && *fb1 != nullptr && *fb2 != nullptr;
  }
'''
replace_once(ws, old_flush, new_flush, 'official double framebuffer access + synchronized flush')

private_anchor = '''private:
  static constexpr uint8_t IO_ADDR = 0x24;
'''
private_new = '''private:
  static bool IRAM_ATTR rgbFrameDone(
      esp_lcd_panel_handle_t panel,
      const esp_lcd_rgb_panel_event_data_t* event,
      void* user_ctx) {
    (void)panel;
    (void)event;
    Waveshare43CDisplay* self = static_cast<Waveshare43CDisplay*>(user_ctx);
    if (!self || !self->_frameDone) return false;

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(self->_frameDone, &higherPriorityTaskWoken);
    return higherPriorityTaskWoken == pdTRUE;
  }

  static constexpr uint8_t IO_ADDR = 0x24;
'''
replace_once(ws, private_anchor, private_new, 'RGB frame callback')

replace_once(
    ws,
    '  esp_lcd_panel_handle_t _panel = nullptr;\n',
    '  esp_lcd_panel_handle_t _panel = nullptr;\n  SemaphoreHandle_t _frameDone = nullptr;\n',
    'RGB frame semaphore member',
)

old_ui_init = '''  _buf1 = reinterpret_cast<lv_color_t*>(_display.miraGetFrameBuffer());
  _buf2 = nullptr;
  if (!_buf1) {
    Serial.println("[MIRA][WAVESHARE] ERREUR framebuffer RGB indisponible");
    return;
  }
  memset(_buf1, 0, MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT * sizeof(lv_color_t));

  // The RGB DMA scans PSRAM directly. A CPU memset can still be dirty in cache,
  // leaving untouched parts of the screen filled with stale PSRAM contents.
  // Route one full-frame draw through esp_lcd so the driver performs the required
  // cache write-back before continuous scanout begins.
  _display.miraFlush(
    0, 0,
    MIRA_DISPLAY_WIDTH, MIRA_DISPLAY_HEIGHT,
    reinterpret_cast<const uint16_t*>(_buf1)
  );

  lv_disp_draw_buf_init(&_drawBuffer, _buf1, nullptr, MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT);
  Serial.printf("[MIRA][WAVESHARE] LVGL framebuffer direct PSRAM=%p (%u px)\\n",
                _buf1, (unsigned)(MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT));
'''
new_ui_init = '''  void* fb1 = nullptr;
  void* fb2 = nullptr;
  if (!_display.miraGetFrameBuffers(&fb1, &fb2)) {
    Serial.println("[MIRA][WAVESHARE] ERREUR double framebuffer RGB indisponible");
    return;
  }

  _buf1 = reinterpret_cast<lv_color_t*>(fb1);
  _buf2 = reinterpret_cast<lv_color_t*>(fb2);
  const size_t frameBytes =
      MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT * sizeof(lv_color_t);

  memset(_buf1, 0, frameBytes);
  memset(_buf2, 0, frameBytes);

  // Force both panel-owned framebuffers through esp_lcd once before LVGL starts.
  // This mirrors the ownership model of Waveshare's official direct-mode port.
  _display.miraFlush(
    0, 0, MIRA_DISPLAY_WIDTH, MIRA_DISPLAY_HEIGHT,
    reinterpret_cast<const uint16_t*>(_buf1)
  );
  _display.miraFlush(
    0, 0, MIRA_DISPLAY_WIDTH, MIRA_DISPLAY_HEIGHT,
    reinterpret_cast<const uint16_t*>(_buf2)
  );

  lv_disp_draw_buf_init(
    &_drawBuffer,
    _buf1,
    _buf2,
    MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT
  );

  Serial.printf("[MIRA][WAVESHARE] LVGL direct double FB: %p / %p (%u px chacun)\\n",
                _buf1, _buf2,
                (unsigned)(MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT));
'''
replace_once(ui, old_ui_init, new_ui_init, 'official LVGL two-framebuffer direct init')

old_display_flush = '''  _displayForLvgl->miraFlush(
    area->x1,
    area->y1,
    area->x2 - area->x1 + 1,
    area->y2 - area->y1 + 1,
    (const uint16_t*)&color_p->full
  );

  lv_disp_flush_ready(disp);
'''
new_display_flush = '''#if MIRA_RGB_DIRECT_FRAMEBUFFER
  // Waveshare direct-mode behavior: only the last invalid area switches the
  // panel framebuffer. color_p already belongs to one of the two panel FBs.
  if (lv_disp_flush_is_last(disp)) {
    _displayForLvgl->miraFlush(
      area->x1,
      area->y1,
      area->x2 - area->x1 + 1,
      area->y2 - area->y1 + 1,
      (const uint16_t*)&color_p->full
    );
  }
#else
  _displayForLvgl->miraFlush(
    area->x1,
    area->y1,
    area->x2 - area->x1 + 1,
    area->y2 - area->y1 + 1,
    (const uint16_t*)&color_p->full
  );
#endif

  lv_disp_flush_ready(disp);
'''
replace_once(ui, old_display_flush, new_display_flush, 'official direct-mode last-flush behavior')

print('Mira Panel 0.6.32 applied: official Waveshare double framebuffer + RGB frame synchronization')
