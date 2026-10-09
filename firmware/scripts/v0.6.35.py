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

replace_once(ino, '"0.6.34"', '"0.6.35"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.34 initialise', '[MIRA] Firmware 0.6.35 initialise', 'startup log')

old = '''#if defined(MIRA_TARGET_WAVESHARE_43C)
  // Force a complete first frame while still holding the official LVGL mutex.
  lv_obj_invalidate(_screen);
  lv_refr_now(nullptr);
  _display.miraLvglUnlock();
#endif
'''
new = '''#if defined(MIRA_TARGET_WAVESHARE_43C)
  // Do NOT call lv_refr_now() from the Arduino task here.
  // Waveshare's official RGB flush waits for a VSYNC notification addressed to
  // the dedicated LVGL task. Refreshing from this task would therefore deadlock
  // after the first visible frame. Unlock and let the official LVGL task render.
  lv_obj_invalidate(_screen);
  _display.miraLvglUnlock();
#endif
'''
replace_once(ui, old, new, 'remove cross-task forced first refresh deadlock')

print('Mira Panel 0.6.35 applied: first frame is rendered by Waveshare LVGL task, no cross-task VSYNC wait')
