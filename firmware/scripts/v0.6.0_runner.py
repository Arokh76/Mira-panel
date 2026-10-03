from pathlib import Path
import sys

script_path = Path(__file__).with_name('v0.6.0.py')
source = script_path.read_text(encoding='utf-8')

# The current web PARAMS renderer no longer contains the legacy generic loop.
old = "else:\n    raise SystemExit('generic PARAMS loop anchor not found or already patched')"
new = "else:\n    # Current Mira PARAMS renders LVGL HTTP widgets only; DISPLAY_* are not listed there.\n    pass"
count = source.count(old)
if count != 1:
    raise SystemExit(f'v0.6.0 compatibility patch: expected 1 legacy PARAMS guard, got {count}')
source = source.replace(old, new, 1)

# A few old settings callbacks disappeared from the current 0.5.13 UI. They are
# optional for 0.6.0 because the same settings are now handled by the web/MQTT
# display engine. Keep strict replacement for every non-optional function.
old_missing = """    if start < 0:\n        raise SystemExit(f'{label}: signature not found in {path}')"""
new_missing = """    if start < 0:\n        if label in ('panel brightness sync', 'panel sleep toggle sync', 'panel sleep clock sync'):\n            return\n        raise SystemExit(f'{label}: signature not found in {path}')"""
if source.count(old_missing) != 1:
    raise SystemExit('v0.6.0 compatibility patch: replace_function missing-signature guard not found')
source = source.replace(old_missing, new_missing, 1)

# Current 0.5.13 loop has no dedicated `now` variable: insert the autonomous
# display refresh immediately before the existing idle calculation.
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ui_path = root / 'MiraPanelUI.cpp'
ui_text = ui_path.read_text(encoding='utf-8')
loop_start = ui_text.find('void MiraPanelUI::loop() {')
if loop_start < 0:
    raise SystemExit('v0.6.0 compatibility patch: MiraPanelUI::loop not found')
brace = ui_text.find('{', loop_start)
depth = 0
loop_end = -1
for i in range(brace, len(ui_text)):
    if ui_text[i] == '{':
        depth += 1
    elif ui_text[i] == '}':
        depth -= 1
        if depth == 0:
            loop_end = i + 1
            break
loop_text = ui_text[loop_start:loop_end]

if '  uint32_t now = millis();' not in loop_text:
    old_call = "replace_in_function(\n    ui,\n    'void MiraPanelUI::loop() {',\n    '  uint32_t now = millis();',\n    '''  refreshDisplayConfig(false);\n  updateDisplaySchedule(false);\n  uint32_t now = millis();''',\n    'display config polling in UI loop',\n)"
    new_call = "replace_in_function(\n    ui,\n    'void MiraPanelUI::loop() {',\n    '  uint32_t idle = millis() - _lastActivity;',\n    '''  refreshDisplayConfig(false);\n  updateDisplaySchedule(false);\n  uint32_t idle = millis() - _lastActivity;''',\n    'display config polling in UI loop',\n)"
    if source.count(old_call) != 1:
        raise SystemExit('v0.6.0 compatibility patch: source loop patch block not found')
    source = source.replace(old_call, new_call, 1)

code = compile(source, str(script_path), 'exec')
exec(code, {'__name__': '__main__', '__file__': str(script_path)})
