from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ws = root / 'Waveshare43CDisplay.h'

def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

replace_once(ino, '"0.6.28"', '"0.6.29"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.28 initialise', '[MIRA] Firmware 0.6.29 initialise', 'startup log')

# Waveshare LVGL reference (12_lvgl_transplant) timings.
# Keep the two RGB framebuffers introduced in 0.6.28.
replace_once(ws, '    cfg.timings.hsync_pulse_width = 4;\n', '    cfg.timings.hsync_pulse_width = 8;\n', 'HSYNC pulse')
replace_once(ws, '    cfg.timings.hsync_front_porch = 8;\n', '    cfg.timings.hsync_front_porch = 4;\n', 'HSYNC front porch')
replace_once(ws, '    cfg.timings.vsync_pulse_width = 4;\n', '    cfg.timings.vsync_pulse_width = 8;\n', 'VSYNC pulse')
replace_once(ws, '    cfg.timings.vsync_front_porch = 8;\n', '    cfg.timings.vsync_front_porch = 4;\n', 'VSYNC front porch')

print('Mira Panel 0.6.29 applied: Waveshare LVGL reference RGB timings 8/8/4 + 2 framebuffers')
