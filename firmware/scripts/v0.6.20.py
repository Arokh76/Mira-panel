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


replace_once(ino, '"0.6.19"', '"0.6.20"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.19 initialise', '[MIRA] Firmware 0.6.20 initialise', 'startup log')

text = ui.read_text(encoding='utf-8')

# Decorative fades force repeated alpha-blended redraws over SPI.
# They looked nice but made page changes and UI reconstruction feel slow.
# Remove only appearance fades; interactive control animations remain.
fade_lines = [
    '    lv_obj_fade_in(b.card, 140, 0);\n',
    '      lv_obj_fade_in(b.card, 140, 0);\n',
    '    lv_obj_fade_in(b.card, 150, 0);\n',
    '  lv_obj_fade_in(b.card, 140, 0);\n',
    '  lv_obj_fade_in(b.card, 150, 0);\n',
    '  lv_obj_fade_in(b.card, 160, 0);\n',
]
for line in fade_lines:
    text = text.replace(line, '')

# Page navigation must be immediate: show/hide only, no 160 ms fade.
old = '''    if (i == index) {
      lv_obj_clear_flag(_pages[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_fade_in(_pages[i], 160, 0);
    } else {'''
new = '''    if (i == index) {
      lv_obj_clear_flag(_pages[i], LV_OBJ_FLAG_HIDDEN);
    } else {'''
if old not in text:
    raise SystemExit('page fade block not found')
text = text.replace(old, new, 1)

ui.write_text(text, encoding='utf-8')

print('Mira Panel 0.6.20 fast-display pass: page/widget fades removed, controls unchanged')
