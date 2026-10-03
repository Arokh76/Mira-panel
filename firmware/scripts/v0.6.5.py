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


def replace_in_function(path: Path, signature: str, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f'{label}: function not found in {path}')
    brace = text.find('{', start)
    depth = 0
    end = -1
    for i in range(brace, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end < 0:
        raise SystemExit(f'{label}: function end not found')
    func = text[start:end]
    if func.count(old) != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in function, got {func.count(old)}')
    func = func.replace(old, new, 1)
    path.write_text(text[:start] + func + text[end:], encoding='utf-8')


replace_once(ino, '"0.6.4"', '"0.6.5"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.4 initialise', '[MIRA] Firmware 0.6.5 initialise', 'startup log')

# Display web saves must become effective quickly, but never while the user is
# dragging a control. 0.6.3 proved that the UI lag was caused by callbacks and
# synchronous writes, not by an occasional read. Poll one second after a short
# touch quiet period so a browser save is reflected almost immediately.
replace_in_function(
    ui,
    'void MiraPanelUI::refreshDisplayConfig(bool force) {',
    '''  if (!force && (uint32_t)(now - _lastActivity) < 5000UL) return;\n  if (!force && (uint32_t)(now - _lastDisplayConfigCheck) < 15000UL) return;''',
    '''  if (!force && (uint32_t)(now - _lastActivity) < 800UL) return;\n  if (!force && (uint32_t)(now - _lastDisplayConfigCheck) < 1000UL) return;''',
    'fast idle display config pickup',
)

# The automatic scheduler must not depend on getLocalTime(timeout=0). Read the
# already-maintained system epoch directly; time() and localtime_r() are fully
# non-blocking once SNTP has set the clock. Check once per second so a test
# schedule changes within the current minute without adding LVGL latency.
replace_in_function(
    ui,
    'void MiraPanelUI::updateDisplaySchedule(bool force) {',
    '''  const uint32_t now = millis();\n  if (!force && (uint32_t)(now - _lastScheduleCheckMs) < 15000UL) return;\n  _lastScheduleCheckMs = now;\n\n  struct tm t;\n  if (!getLocalTime(&t, 0)) return;\n  int minute = t.tm_hour * 60 + t.tm_min;''',
    '''  const uint32_t now = millis();\n  if (!force && (uint32_t)(now - _lastScheduleCheckMs) < 1000UL) return;\n  _lastScheduleCheckMs = now;\n\n  time_t epoch = time(nullptr);\n  if (epoch < 1700000000) return;\n  struct tm t;\n  localtime_r(&epoch, &t);\n  int minute = t.tm_hour * 60 + t.tm_min;''',
    'nonblocking reliable schedule clock',
)

replace_in_function(
    ui,
    'void MiraPanelUI::updateDisplaySchedule(bool force) {',
    '''    if (changed) Serial.printf("[MIRA][DISPLAY] Mode %s\\n", night ? "nuit" : "jour");''',
    '''    if (changed) Serial.printf("[MIRA][DISPLAY] %02d:%02d -> mode %s (%s-%s)\\n",\n                               t.tm_hour, t.tm_min, night ? "nuit" : "jour",\n                               _nightStart.c_str(), _nightEnd.c_str());''',
    'schedule transition diagnostic',
)

print('Mira Panel 0.6.5 reliable nonblocking day/night scheduler applied')
