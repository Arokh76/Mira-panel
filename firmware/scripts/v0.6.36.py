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

ws = root / 'Waveshare43CDisplay.h'
old_brightness = '''  void setBrightness(uint8_t raw) {
    // Keep this diagnostic build on the same electrical path as the official
    // demo: only gate the backlight. Do not program the expander PWM register.
    IO_EXTENSION_Output(IO_EXTENSION_IO_2, raw == 0 ? 0 : 1);
  }
'''
new_brightness = '''  void setBrightness(uint8_t raw) {
    if (raw == 0) {
      // Real OFF: gate the backlight output.
      IO_EXTENSION_Output(IO_EXTENSION_IO_2, 0);
      return;
    }

    // Mira internally uses raw 30..255 for 1..100%.
    // Waveshare's expander PWM is inverted: 0 = max brightness,
    // ~95 = minimum brightness. IO_EXTENSION_Pwm_Output() also clamps
    // the extreme end to avoid accidentally blanking the panel.
    uint8_t pct;
    if (raw <= 30) {
      pct = 1;
    } else {
      pct = (uint8_t)(1 + ((uint16_t)(raw - 30) * 99U + 112U) / 225U);
      if (pct > 100) pct = 100;
    }

    uint8_t wsDuty = (uint8_t)(100U - pct);
    IO_EXTENSION_Pwm_Output(wsDuty);
    IO_EXTENSION_Output(IO_EXTENSION_IO_2, 1);
  }
'''
replace_once(ws, old_brightness, new_brightness, 'enable Waveshare backlight PWM')

print('Mira Panel 0.6.36 applied: Waveshare full-refresh + hardware backlight PWM')
