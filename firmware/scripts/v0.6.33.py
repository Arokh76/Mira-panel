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

replace_once(ino, '"0.6.32"', '"0.6.33"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.32 initialise', '[MIRA] Firmware 0.6.33 initialise', 'startup log')

anchor = '#include <time.h>\n'
guard = '''#include <time.h>

#if defined(MIRA_TARGET_WAVESHARE_43C)
  #if LVGL_VERSION_MAJOR != 8 || LVGL_VERSION_MINOR != 4
    #error "Mira Waveshare requires the official Waveshare LVGL 8.4 library"
  #endif
#endif
'''
replace_once(ui, anchor, guard, 'Waveshare LVGL 8.4 compile guard')

old = '''void MiraPanelUI::begin() {
  _displayForLvgl = &_display;
'''
new = '''void MiraPanelUI::begin() {
#if defined(MIRA_TARGET_WAVESHARE_43C)
  Serial.printf("[MIRA][WAVESHARE] LVGL officiel %d.%d.%d\\n",
                LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
#endif

  _displayForLvgl = &_display;
'''
replace_once(ui, old, new, 'runtime LVGL version log')

print('Mira Panel 0.6.33 applied: Waveshare requires official LVGL 8.4')
