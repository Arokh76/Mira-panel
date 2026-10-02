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


# Version
replace_once(ino, '"0.5.2"', '"0.5.3"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.2 initialise', '[MIRA] Firmware 0.5.3 initialise', 'startup log')

# Slider polish: shorten the slider object while keeping it centered in the card.
# The LVGL knob is centered on the min/max endpoints, so leaving extra room on
# both sides prevents the circular knob from being clipped by the card edges.
replace_once(
    ui,
    '  lv_obj_set_size(b.control, compact ? 250 : 280, compact ? 14 : 18);',
    '  lv_obj_set_size(b.control, compact ? 220 : 250, compact ? 14 : 18);',
    'shorter range track for full knob visibility',
)

print('Mira Panel 0.5.3 slider endpoint polish applied')
