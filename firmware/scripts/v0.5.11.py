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


def replace_exact(path: Path, old: str, new: str, expected: int, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != expected:
        raise SystemExit(f'{label}: expected exactly {expected} matches in {path}, got {count}')
    path.write_text(text.replace(old, new), encoding='utf-8')


replace_once(ino, '"0.5.10"', '"0.5.11"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.10 initialise', '[MIRA] Firmware 0.5.11 initialise', 'startup log')

# Restore true real-time MQTT while keeping the 0.5.10 anti-echo holdoff.
# The UI remains local-first and delayed feedback is still ignored during/after touch.
replace_exact(
    ui,
    '''  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue &&\n                        (b.lastLiveSendMs == 0 || (uint32_t)(now - b.lastLiveSendMs) >= 60);''',
    '''  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue;''',
    2,
    'real-time linear + round range MQTT',
)

replace_once(
    ui,
    '''  const bool liveSend = (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSING) &&\n                        rgb24 != b.lastLiveColor &&\n                        (b.lastLiveSendMs == 0 || (uint32_t)(now - b.lastLiveSendMs) >= 60);''',
    '''  const bool liveSend = (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSING) &&\n                        rgb24 != b.lastLiveColor;''',
    'real-time colorwheel MQTT',
)

print('Mira Panel 0.5.11 real-time MQTT restored with local anti-echo holdoff retained')
