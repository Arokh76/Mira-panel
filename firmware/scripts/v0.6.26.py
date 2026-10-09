from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
lvconf = root / 'lv_conf.h'

def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

replace_once(ino, '"0.6.25"', '"0.6.26"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.25 initialise', '[MIRA] Firmware 0.6.26 initialise', 'startup log')

# LVGL RGB565 byte order is a hardware-transport concern:
# - WT32-SC01 Plus / LovyanGFX SPI path expects swapped RGB565 bytes.
# - Waveshare 4.3C RGB parallel panel expects native RGB565 (official config = 0).
replace_once(
    lvconf,
    '#define LV_COLOR_16_SWAP 1\n',
    '''#if defined(MIRA_TARGET_WAVESHARE_43C)
#define LV_COLOR_16_SWAP 0
#else
#define LV_COLOR_16_SWAP 1
#endif
''',
    'per-profile RGB565 byte order',
)

print('Mira Panel 0.6.26 applied: per-profile RGB565 byte order (WT32=swap, Waveshare=native)')
