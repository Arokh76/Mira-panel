from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
spiffs = root / 'SPIFFS_Process.h'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


# 0.6.9 fixes truncated widget JSON when the screen contains many widgets.
# The original 4 KiB ArduinoJson document became too small after adding style
# metadata and richer configurations. When full, ArduinoJson silently drops the
# last properties of the last widget; list/listbutton then lose their `value`
# field and the UI sees an empty list.
replace_once(ino, '"0.6.8"', '"0.6.9"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.8 initialise', '[MIRA] Firmware 0.6.9 initialise', 'startup log')
replace_once(
    spiffs,
    'DynamicJsonDocument mira_Json_LVGL(4096);',
    'DynamicJsonDocument mira_Json_LVGL(16384);',
    'LVGL JSON capacity',
)

# Keep a diagnostic in debug builds if the larger document ever becomes full.
replace_once(
    spiffs,
    '  if (_Debug == true) serializeJsonPretty(mira_Json_LVGL, Serial);\n  paramEvent = MIRA_ADD_LIST_LVGL;',
    '  if (_Debug == true) {\n'
    '    serializeJsonPretty(mira_Json_LVGL, Serial);\n'
    '    if (mira_Json_LVGL.overflowed()) Serial.println("[MIRA][FS] ERREUR: document LVGL sature");\n'
    '  }\n'
    '  paramEvent = MIRA_ADD_LIST_LVGL;',
    'LVGL overflow diagnostic',
)

print('Mira Panel 0.6.9 LVGL JSON capacity fix applied (4096 -> 16384)')
