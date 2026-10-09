from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'

def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

replace_once(ino, '"0.6.30"', '"0.6.31"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.30 initialise', '[MIRA] Firmware 0.6.31 initialise', 'startup log')

old = '''  memset(_buf1, 0, MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT * sizeof(lv_color_t));
  lv_disp_draw_buf_init(&_drawBuffer, _buf1, nullptr, MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT);
'''
new = '''  memset(_buf1, 0, MIRA_DISPLAY_WIDTH * MIRA_DISPLAY_HEIGHT * sizeof(lv_color_t));

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
'''
replace_once(ui, old, new, 'RGB framebuffer initial cache sync')

print('Mira Panel 0.6.31 applied: full RGB framebuffer cache sync after clear')
