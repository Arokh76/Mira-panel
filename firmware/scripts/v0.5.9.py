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


# Version
replace_once(ino, '"0.5.8"', '"0.5.9"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.8 initialise', '[MIRA] Firmware 0.5.9 initialise', 'startup log')

# Live controls: remove the 100 ms gate completely. LVGL still updates locally first,
# while duplicate values are filtered by lastLiveValue / lastLiveColor. This keeps
# the control and the external state return visually in phase instead of trailing.
replace_exact(
    ui,
    '''  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue &&
                        (b.lastLiveSendMs == 0 || (uint32_t)(now - b.lastLiveSendMs) >= 100);''',
    '''  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue;''',
    2,
    'linear + round range zero-latency live send',
)

replace_once(
    ui,
    '''  const bool liveSend = (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSING) &&
                        rgb24 != b.lastLiveColor &&
                        (b.lastLiveSendMs == 0 || (uint32_t)(now - b.lastLiveSendMs) >= 100);''',
    '''  const bool liveSend = (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSING) &&
                        rgb24 != b.lastLiveColor;''',
    'colorwheel zero-latency live send',
)

# Keep the 14 px ring width from 0.5.8. The visible black radial slits are not a
# width problem: they come from LVGL 8.3 colorwheel's coarse radial-line renderer.
# The renderer itself is patched in the build workflow (QF 3 -> 1) so the hue ring
# is drawn with roughly three times the angular resolution and overlapping strokes.

print('Mira Panel 0.5.9 zero-latency live controls applied')
