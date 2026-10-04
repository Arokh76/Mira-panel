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


# 0.6.14 keeps the dashboard top-aligned but restores a small breathing margin.
# No widget protocol, Jeedom, MQTT/HTTP or callback behavior is changed.
replace_once(ino, '"0.6.13"', '"0.6.14"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.13 initialise', '[MIRA] Firmware 0.6.14 initialise', 'startup log')

replace_once(
    ui,
    'lv_obj_set_style_pad_top(_pages[i], 0, 0);',
    'lv_obj_set_style_pad_top(_pages[i], 8, 0);',
    'home dashboard breathing margin',
)

print('Mira Panel 0.6.14 top-aligned dashboard + 8px breathing margin applied; integration unchanged')
