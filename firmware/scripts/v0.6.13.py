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


# 0.6.13 fixes the actual reason the dashboard still looked too low:
# with ROW_WRAP, the third flex alignment parameter controls vertical track placement.
# It was CENTER, so the whole dashboard block was vertically centered in the 434px page.
replace_once(ino, '"0.6.12"', '"0.6.13"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.12 initialise', '[MIRA] Firmware 0.6.13 initialise', 'startup log')

replace_once(
    ui,
    'lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);',
    'lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);',
    'home dashboard top track alignment',
)

print('Mira Panel 0.6.13 dashboard tracks top-aligned; integration unchanged')
