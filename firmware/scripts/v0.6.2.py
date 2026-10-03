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


replace_once(ino, '"0.6.1"', '"0.6.2"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.1 initialise', '[MIRA] Firmware 0.6.2 initialise', 'startup log')

# The schedule checker introduced in 0.6.0 called getLocalTime(..., 10) from the
# main UI loop whenever automatic night mode was enabled. That can block the UI
# for up to 10 ms on every loop pass, which is enough to make LVGL sliders and
# HTTP page rendering visibly lag. Gate the schedule check before touching the
# time subsystem and make the call non-blocking.
replace_once(
    ui_h,
    '  int _lastScheduleMinute = -1;\n',
    '  int _lastScheduleMinute = -1;\n  uint32_t _lastScheduleCheckMs = 0;\n',
    'schedule throttle field',
)

replace_once(
    ui,
    '''  struct tm t;\n  if (!getLocalTime(&t, 10)) return;\n  int minute = t.tm_hour * 60 + t.tm_min;''',
    '''  const uint32_t now = millis();\n  if (!force && (uint32_t)(now - _lastScheduleCheckMs) < 15000UL) return;\n  _lastScheduleCheckMs = now;\n\n  struct tm t;\n  if (!getLocalTime(&t, 0)) return;\n  int minute = t.tm_hour * 60 + t.tm_min;''',
    'non-blocking schedule check',
)

# Never reread the web configuration while the user is actively touching the
# panel. The server-backed DISPLAY_* getters can allocate/parse configuration
# data; deferring them until the touch path has been quiet keeps sliders fluid.
replace_once(
    ui,
    '''  if (!force && millis() - _lastDisplayConfigCheck < 5000UL) return;\n  _lastDisplayConfigCheck = millis();''',
    '''  const uint32_t now = millis();\n  if (!force && (uint32_t)(now - _lastActivity) < 1200UL) return;\n  if (!force && (uint32_t)(now - _lastDisplayConfigCheck) < 5000UL) return;\n  _lastDisplayConfigCheck = now;''',
    'defer display config reads during touch',
)

# Display telemetry is not control traffic. Five seconds is enough for retained
# status while avoiding periodic bursts of String construction / MQTT writes in
# the same loop that drives LVGL and the local web server. Forced publications
# after explicit MQTT commands and broker connection remain immediate.
replace_once(
    mqtt,
    '  if (!force && (uint32_t)(now - _lastDisplayStateCheckMs) < 1000UL) return;',
    '  if (!force && (uint32_t)(now - _lastDisplayStateCheckMs) < 5000UL) return;',
    'display mqtt telemetry interval',
)

print('Mira Panel 0.6.2 non-blocking display scheduler + UI touch priority applied')
