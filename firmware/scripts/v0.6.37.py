from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'
ws = root / 'Waveshare43CDisplay.h'

def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')

replace_once(ino, '"0.6.36"', '"0.6.37"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.36 initialise', '[MIRA] Firmware 0.6.37 initialise', 'startup log')

# Brightness should only touch the I2C expander when the slider value changes.
replace_once(
    ui,
    '  lv_obj_add_event_cb(_brightnessSlider, onBrightness, LV_EVENT_ALL, this);\n',
    '  lv_obj_add_event_cb(_brightnessSlider, onBrightness, LV_EVENT_VALUE_CHANGED, this);\n',
    'brightness slider event scope',
)

old_brightness = '''  void setBrightness(uint8_t raw) {
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
new_brightness = '''  void setBrightness(uint8_t raw) {
    if (raw == 0) {
      // Real OFF is reserved for Mira sleep/off.
      IO_EXTENSION_Output(IO_EXTENSION_IO_2, 0);
      return;
    }

    // Restore the backlight gate first in case we are waking from OFF.
    IO_EXTENSION_Output(IO_EXTENSION_IO_2, 1);

    // Mira internally uses raw 30..255 for 1..100%.
    // Waveshare PWM is inverted (0 = max brightness). Values near 95 are
    // visually almost black on this panel, so keep the interactive minimum
    // safely visible by limiting the hardware duty to 80.
    uint8_t pct;
    if (raw <= 30) {
      pct = 1;
    } else {
      pct = (uint8_t)(1 + ((uint16_t)(raw - 30) * 99U + 112U) / 225U);
      if (pct > 100) pct = 100;
    }

    // 1..100% Mira -> 80..0 Waveshare PWM.
    uint8_t wsDuty = (uint8_t)(((uint16_t)(100U - pct) * 80U + 49U) / 99U);
    IO_EXTENSION_Pwm_Output(wsDuty);
  }
'''
replace_once(ws, old_brightness, new_brightness, 'safe Waveshare brightness curve')

print('Mira Panel 0.6.37 applied: safe brightness events/curve; triple-buffer full-refresh selected by CI')
