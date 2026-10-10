from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
text = ino.read_text(encoding='utf-8')

for old, new, label in [
    ('"0.6.37"', '"0.6.38"', 'firmware version'),
    ('[MIRA] Firmware 0.6.37 initialise', '[MIRA] Firmware 0.6.38 initialise', 'startup log'),
]:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one match in {ino}, got {count}')
    text = text.replace(old, new, 1)

ino.write_text(text, encoding='utf-8')
print('Mira Panel 0.6.38 RGB diagnostic: firmware bumped, LCD clock reduced to 12 MHz by CI')
