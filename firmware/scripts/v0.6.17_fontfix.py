from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ui = root / 'MiraPanelUI.cpp'

text = ui.read_text(encoding='utf-8')
old = 'forcedHero ? &lv_font_montserrat_32 : &lv_font_montserrat_28'
new = '&lv_font_montserrat_28'
count = text.count(old)
if count != 1:
    raise SystemExit(f'0.6.17 hero font fix: expected exactly 1 match, got {count}')
ui.write_text(text.replace(old, new, 1), encoding='utf-8')
print('Mira Panel 0.6.17 hero layout uses available Montserrat 28 font')
