from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui_h = root / 'MiraPanelUI.h'
ui = root / 'MiraPanelUI.cpp'
mqtt = root / 'MiraMqtt.cpp'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


def replace_in_function(path: Path, signature: str, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f'{label}: function not found in {path}')
    brace = text.find('{', start)
    depth = 0
    end = -1
    for i in range(brace, len(text)):
        if text[i] == '{': depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    func = text[start:end]
    if func.count(old) != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in function, got {func.count(old)}')
    func = func.replace(old, new, 1)
    path.write_text(text[:start] + func + text[end:], encoding='utf-8')


replace_once(ino, '"0.6.2"', '"0.6.3"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.2 initialise', '[MIRA] Firmware 0.6.3 initialise', 'startup log')

# Programmatic LVGL updates must never be treated as user changes. In 0.6.0 the
# display refresh could set the local brightness slider, fire its callback, and
# synchronously write DISPLAY_* back through the server config. That blocks the
# same task that renders LVGL and matches the multi-second page construction and
# delayed sliders visible on the device.
replace_once(
    ui_h,
    '  bool _manualBrightnessOverride = false;\n',
    '  bool _manualBrightnessOverride = false;\n  bool _syncingDisplayControls = false;\n',
    'display control sync guard field',
)

replace_in_function(
    ui,
    'void MiraPanelUI::refreshDisplayConfig(bool force) {',
    '''  if (_sleepSwitch) {\n    if (_sleepEnabled) lv_obj_add_state(_sleepSwitch, LV_STATE_CHECKED);\n    else lv_obj_clear_state(_sleepSwitch, LV_STATE_CHECKED);\n  }\n  if (_clockSleepSwitch) {\n    if (_clockInSleep) lv_obj_add_state(_clockSleepSwitch, LV_STATE_CHECKED);\n    else lv_obj_clear_state(_clockSleepSwitch, LV_STATE_CHECKED);\n  }''',
    '''  _syncingDisplayControls = true;\n  if (_sleepSwitch) {\n    if (_sleepEnabled) lv_obj_add_state(_sleepSwitch, LV_STATE_CHECKED);\n    else lv_obj_clear_state(_sleepSwitch, LV_STATE_CHECKED);\n  }\n  if (_clockSleepSwitch) {\n    if (_clockInSleep) lv_obj_add_state(_clockSleepSwitch, LV_STATE_CHECKED);\n    else lv_obj_clear_state(_clockSleepSwitch, LV_STATE_CHECKED);\n  }\n  _syncingDisplayControls = false;''',
    'guard programmatic display switches',
)

replace_in_function(
    ui,
    'void MiraPanelUI::applyAwakeBrightness() {',
    '''  if (_brightnessSlider) lv_slider_set_value(_brightnessSlider, _brightness, LV_ANIM_OFF);\n  if (_brightnessValue) lv_label_set_text_fmt(_brightnessValue, "%u%%", pct);''',
    '''  if (_brightnessSlider && lv_slider_get_value(_brightnessSlider) != _brightness) {\n    _syncingDisplayControls = true;\n    lv_slider_set_value(_brightnessSlider, _brightness, LV_ANIM_OFF);\n    _syncingDisplayControls = false;\n  }\n  if (_brightnessValue) lv_label_set_text_fmt(_brightnessValue, "%u%%", pct);''',
    'guard programmatic brightness slider',
)

replace_in_function(
    ui,
    'void MiraPanelUI::onBrightness(lv_event_t* e) {',
    '  if (!ui) return;',
    '  if (!ui) return;\n  if (ui->_syncingDisplayControls) return;',
    'brightness callback guard',
)

# A fast drag on the local brightness slider must not serialize/write the web
# configuration for every LVGL value. Treat it like MQTT brightness/set: an
# immediate runtime override. The scheduled day/night values remain untouched.
replace_in_function(
    ui,
    'void MiraPanelUI::onBrightness(lv_event_t* e) {',
    '''  ui->_manualBrightnessOverride = false;\n  if (ui->_nightScheduleEnabled && ui->_nightActive) {\n    ui->_nightBrightnessPct = pct;\n    ui->_server.UpdateParamText("DISPLAY_Nuit", String(pct));\n  } else {\n    ui->_dayBrightnessPct = pct;\n    ui->_server.UpdateParamText("DISPLAY_Jour", String(pct));\n  }''',
    '''  ui->_manualBrightnessPct = pct;\n  ui->_manualBrightnessOverride = true;''',
    'nonblocking local brightness override',
)

# The web configuration is background work. Do it only after five seconds of
# completely quiet local UI and at most once every fifteen seconds. This keeps
# navigation / rendering / fast sliders ahead of config JSON access.
replace_once(
    ui,
    '  if (!force && (uint32_t)(now - _lastActivity) < 1200UL) return;\n  if (!force && (uint32_t)(now - _lastDisplayConfigCheck) < 5000UL) return;',
    '  if (!force && (uint32_t)(now - _lastActivity) < 5000UL) return;\n  if (!force && (uint32_t)(now - _lastDisplayConfigCheck) < 15000UL) return;',
    'display config deep idle polling',
)

# Retained display telemetry is also background work. State changes remain
# published immediately when forced; the passive signature scan becomes rare.
replace_once(
    mqtt,
    '  if (!force && (uint32_t)(now - _lastDisplayStateCheckMs) < 5000UL) return;',
    '  if (!force && (uint32_t)(now - _lastDisplayStateCheckMs) < 15000UL) return;',
    'display mqtt background interval',
)

print('Mira Panel 0.6.3 programmatic-control guard + nonblocking brightness override applied')
