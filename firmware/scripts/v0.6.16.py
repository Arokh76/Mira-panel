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


# 0.6.16 fixes the visible internal scrollbar on dropdown cards seen on real hardware.
# Keep the 0.6.15 visual redesign and all widget/event integration unchanged.
replace_once(ino, '"0.6.15"', '"0.6.16"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.15 initialise', '[MIRA] Firmware 0.6.16 initialise', 'startup log')

replace_once(
    ui,
    'b.card = makeCard(pageIndex(b.location), asButtons ? LV_SIZE_CONTENT : (compact ? 64 : 76));',
    '''b.card = makeCard(pageIndex(b.location), asButtons ? LV_SIZE_CONTENT : (compact ? 68 : 82));
  if (!asButtons) {
    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(b.card, LV_SCROLLBAR_MODE_OFF);
  }''',
    'dropdown card no-scroll fix',
)

print('Mira Panel 0.6.16 dropdown card scrollbar fix applied; integration unchanged')
