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
code = compile(source, str(script_path), 'exec')
exec(code, {'__name__': '__main__', '__file__': str(script_path)})
