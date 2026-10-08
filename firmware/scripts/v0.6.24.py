from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
profile = root / 'MiraDisplayProfile.h'

def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

replace_once(ino, '"0.6.23"', '"0.6.24"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.23 initialise', '[MIRA] Firmware 0.6.24 initialise', 'startup log')

# 0.6.21 increased LVGL's two DMA draw buffers from 20 to 32 lines.
# Isolated hardware testing showed this exact change makes the WT32-SC01 Plus
# disappear from Wi-Fi, while /programui and waitDMA remain healthy.
# Keep the DMA synchronization and all later fixes, but restore the proven
# 20-line buffer size until larger buffers are explicitly allocated in PSRAM.
replace_once(
    profile,
    '#define MIRA_DRAW_BUFFER_LINES 32\n',
    '#define MIRA_DRAW_BUFFER_LINES 20\n',
    'LVGL draw buffer line count',
)

print('Mira Panel 0.6.24 applied: restore Wi-Fi-safe 20-line LVGL draw buffers')
