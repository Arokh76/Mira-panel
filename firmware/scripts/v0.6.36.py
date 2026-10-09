from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'

def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

replace_once(ino, '"0.6.35"', '"0.6.36"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.35 initialise', '[MIRA] Firmware 0.6.36 initialise', 'startup log')

print('Mira Panel 0.6.36 applied: Waveshare full-refresh framebuffer fix is selected by CI')
