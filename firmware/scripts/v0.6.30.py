from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'
hdr = root / 'MiraPanelUI.h'
profile = root / 'MiraDisplayProfile.h'
ws = root / 'Waveshare43CDisplay.h'

def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

replace_once(ino, '"0.6.29"', '"0.6.30"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.29 initialise', '[MIRA] Firmware 0.6.30 initialise', 'startup log')

replace_once(
    profile,
    '  #define MIRA_DRAW_BUFFER_LINES 10\n',
    '  #define MIRA_DRAW_BUFFER_LINES 10\n  #define MIRA_RGB_DIRECT_FRAMEBUFFER 1\n',
    'Waveshare direct framebuffer profile flag',
)
replace_once(
    profile,
    '  #define MIRA_DRAW_BUFFER_LINES 20\n',
    '  #define MIRA_DRAW_BUFFER_LINES 20\n  #define MIRA_RGB_DIRECT_FRAMEBUFFER 0\n',
    'WT32 direct framebuffer profile flag',
)

# Stop using the CPU-heavy RGB bounce-buffer path for Mira. LVGL will render
# directly in the single RGB framebuffer allocated in PSRAM. esp_lcd draw_bitmap
# then only performs the required cache sync instead of copying each dirty strip.
replace_once(ws, '    cfg.num_fbs = 2;\n', '    cfg.num_fbs = 1;\n', 'single RGB framebuffer')
replace_once(ws, '    cfg.bounce_buffer_size_px = 800 * 10;\n', '    cfg.bounce_buffer_size_px = 0;\n', 'disable RGB bounce buffers')

# GT911 point layout: byte0=track ID, byte1/2=X, byte3/4=Y.
replace_once(
    ws,
    '    uint16_t tx = (uint16_t)point[0] | ((uint16_t)point[1] << 8);\n'
    '    uint16_t ty = (uint16_t)point[2] | ((uint16_t)point[3] << 8);\n',
    '    uint16_t tx = (uint16_t)point[1] | ((uint16_t)point[2] << 8);\n'
    '    uint16_t ty = (uint16_t)point[3] | ((uint16_t)point[4] << 8);\n',
    'GT911 coordinate byte offsets',
)

# Expose the panel-owned framebuffer to LVGL.
anchor = '''  bool miraGetTouch(uint16_t* x, uint16_t* y) {
'''
insert = '''  void* miraGetFrameBuffer() {
    if (!_panel) return nullptr;
    void* fb = nullptr;
    if (esp_lcd_rgb_panel_get_frame_buffer(_panel, 1, &fb) != ESP_OK) return nullptr;
    return fb;
  }

  bool miraGetTouch(uint16_t* x, uint16_t* y) {
'''
replace_once(ws, anchor, insert, 'Waveshare framebuffer accessor')

old_hdr = '''  static lv_disp_draw_buf_t _drawBuffer;
  static lv_color_t _buf1[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
  static lv_color_t _buf2[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
'''
new_hdr = '''  static lv_disp_draw_buf_t _drawBuffer;
#if MIRA_RGB_DIRECT_FRAMEBUFFER
  static lv_color_t* _buf1;
  static lv_color_t* _buf2;
#else
  static lv_color_t _buf1[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
  static lv_color_t _buf2[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
#endif
'''
replace_once(hdr, old_hdr, new_hdr, 'conditional LVGL buffer declarations')

old_cpp = '''lv_disp_draw_buf_t MiraPanelUI::_drawBuffer;
lv_color_t MiraPanelUI::_buf1[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
lv_color_t MiraPanelUI::_buf2[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
'''
new_cpp = '''lv_disp_draw_buf_t MiraPanelUI::_drawBuffer;
#if MIRA_RGB_DIRECT_FRAMEBUFFER
lv_color_t* MiraPanelUI::_buf1 = nullptr;
lv_color_t* MiraPanelUI::_buf2 = nullptr;
#else
lv_color_t MiraPanelUI::_buf1[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
lv_color_t MiraPanelUI::_buf2[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];
#endif
'''
replace_once(ui, old_cpp, new_cpp, 'conditional LVGL buffer definitions')

old_init = '''  lv_init();
  lv_disp_draw_buf_init(&_drawBuffer, _buf1, _buf2, MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES);

  static lv_disp_drv_t dispDrv;
'''
new_init = '''  lv_init();
#if MIRA_RGB_DIRECT_FRAMEBUFFER
  _buf1 = reinterpret_cast<lv_color_t*>(_display.miraGetFrameBuffer());
  _buf2 = nullptr;
  if (!_buf1) {
    Serial.println("[MIRA][WAVESHARE] ERREUR framebuffer RGB indisponible");
    return;
  }
  memset(_buf1, 0, MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT * sizeof(lv_color_t));
  lv_disp_draw_buf_init(&_drawBuffer, _buf1, nullptr, MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT);
  Serial.printf("[MIRA][WAVESHARE] LVGL framebuffer direct PSRAM=%p (%u px)\\n",
                _buf1, (unsigned)(MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT));
#else
  lv_disp_draw_buf_init(&_drawBuffer, _buf1, _buf2, MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES);
#endif

  static lv_disp_drv_t dispDrv;
'''
replace_once(ui, old_init, new_init, 'LVGL direct framebuffer init')

old_drv = '''  dispDrv.flush_cb = displayFlush;
  dispDrv.draw_buf = &_drawBuffer;
  lv_disp_drv_register(&dispDrv);
'''
new_drv = '''  dispDrv.flush_cb = displayFlush;
  dispDrv.draw_buf = &_drawBuffer;
#if MIRA_RGB_DIRECT_FRAMEBUFFER
  dispDrv.direct_mode = 1;
#endif
  lv_disp_drv_register(&dispDrv);
'''
replace_once(ui, old_drv, new_drv, 'LVGL direct mode flag')

print('Mira Panel 0.6.30 applied: direct RGB framebuffer + GT911 coordinate fix')
