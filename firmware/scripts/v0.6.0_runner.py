from pathlib import Path
import sys

script_path = Path(__file__).with_name('v0.6.0.py')
source = script_path.read_text(encoding='utf-8')
old = "else:\n    raise SystemExit('generic PARAMS loop anchor not found or already patched')"
new = "else:\n    # Current Mira PARAMS renders LVGL HTTP widgets only; DISPLAY_* are not listed there.\n    pass"
count = source.count(old)
if count != 1:
    raise SystemExit(f'v0.6.0 compatibility patch: expected 1 legacy PARAMS guard, got {count}')
source = source.replace(old, new, 1)

# 0.5.13 loop evolved from the first 0.6.0 draft. Detect its current clock line
# and adapt the source patch without changing the generated firmware semantics.
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
    if '  const uint32_t now = millis();' in loop_text:
        source = source.replace("    '  uint32_t now = millis();',", "    '  const uint32_t now = millis();',", 1)
        source = source.replace("  updateDisplaySchedule(false);\n  uint32_t now = millis();", "  updateDisplaySchedule(false);\n  const uint32_t now = millis();", 1)
    else:
        print('===== CURRENT MiraPanelUI::loop() =====')
        print(loop_text)
        raise SystemExit('v0.6.0 compatibility patch: unknown UI loop clock line')

code = compile(source, str(script_path), 'exec')
exec(code, {'__name__': '__main__', '__file__': str(script_path)})
