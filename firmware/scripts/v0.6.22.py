from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'
hdr = root / 'MiraPanelUI.h'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


replace_once(ino, '"0.6.21"', '"0.6.22"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.21 initialise', '[MIRA] Firmware 0.6.22 initialise', 'startup log')

# Bottom navigation is a permanent hardware touch zone. Route it directly from
# the touch driver instead of depending on LVGL hit-testing through scrollable
# pages/free-layout children. The visible LVGL buttons remain for styling.
header_anchor = '  uint8_t _currentPage = 0;\n'
header_new = '''  uint8_t _currentPage = 0;
  int8_t _pendingNavPage = -1;
  bool _navTouchLatched = false;
'''
replace_once(hdr, header_anchor, header_new, 'navigation state')

old_touch = '''    // Le premier toucher sert uniquement a reveiller l'ecran.
    if (wasSleeping) return;

    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  }
}
'''
new_touch = '''    // Le premier toucher sert uniquement a reveiller l'ecran.
    if (wasSleeping) return;

    // La barre basse est reservee a la navigation Mira. On la traite
    // directement afin qu'aucun widget/page scrollable ne puisse intercepter
    // le toucher. Un seul changement est genere par appui.
    if (_uiForLvgl && y >= MIRA_CONTENT_HEIGHT) {
      if (!_uiForLvgl->_navTouchLatched) {
        _uiForLvgl->_navTouchLatched = true;
        static const uint8_t navOrder[PAGE_COUNT][3] = {
          {1, 3, 2},
          {2, 0, 3},
          {1, 0, 3},
          {1, 0, 2}
        };
        uint8_t current = _uiForLvgl->_currentPage < PAGE_COUNT ? _uiForLvgl->_currentPage : 0;
        uint8_t slot = ((uint32_t)x * 3U) / MIRA_DISPLAY_WIDTH;
        if (slot > 2) slot = 2;
        _uiForLvgl->_pendingNavPage = (int8_t)navOrder[current][slot];
      }
      return;
    }

    if (_uiForLvgl) _uiForLvgl->_navTouchLatched = false;
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
    return;
  }

  if (_uiForLvgl) _uiForLvgl->_navTouchLatched = false;
}
'''
replace_once(ui, old_touch, new_touch, 'direct bottom navigation touch')

loop_anchor = '''void MiraPanelUI::loop() {
  startNtpIfPossible();
'''
loop_new = '''void MiraPanelUI::loop() {
  if (_pendingNavPage >= 0) {
    uint8_t page = (uint8_t)_pendingNavPage;
    _pendingNavPage = -1;
    showPage(page);
  }

  startNtpIfPossible();
'''
replace_once(ui, loop_anchor, loop_new, 'pending navigation dispatch')

# Keep the LVGL buttons as a secondary path, but make their hit area explicit.
replace_once(
    ui,
    '    lv_obj_set_size(_navButtons[i], 106, 44);\n',
    '''    lv_obj_set_size(_navButtons[i], 106, 44);
    lv_obj_add_flag(_navButtons[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(_navButtons[i], 2);
''',
    'nav button hit area',
)

print('Mira Panel 0.6.22 direct hardware bottom navigation applied')
