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


def replace_all_exact(path: Path, old: str, new: str, expected: int, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != expected:
        raise SystemExit(f'{label}: expected {expected} matches in {path}, got {count}')
    path.write_text(text.replace(old, new), encoding='utf-8')


# Version
replace_once(ino, '"0.5.5"', '"0.5.6"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.5 initialise', '[MIRA] Firmware 0.5.6 initialise', 'startup log')

# Interactive widgets should read more like a finished touch interface: labels are
# one step larger while info/value-dominant widgets keep their existing hierarchy.
replace_once(
    ui,
    '  lv_obj_set_style_text_font(lbl, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);\n  lv_obj_center(lbl);',
    '  lv_obj_set_style_text_font(lbl, compact ? &mira_font_fr_14 : &mira_font_fr_16, 0);\n  lv_obj_center(lbl);',
    'button label readability',
)

# Toggle rows: more breathing room from the panel edges and a stronger label.
replace_once(
    ui,
    '  lv_obj_fade_in(b.card, 140, 0);\n  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);',
    '  lv_obj_fade_in(b.card, 140, 0);\n'
    '  lv_obj_set_style_pad_left(b.card, compact ? 16 : 20, 0);\n'
    '  lv_obj_set_style_pad_right(b.card, compact ? 16 : 20, 0);\n'
    '  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);',
    'toggle horizontal breathing room',
)
replace_once(
    ui,
    '  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);\n\n  b.stateLabel = nullptr;',
    '  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_14 : &mira_font_fr_16, 0);\n\n  b.stateLabel = nullptr;',
    'toggle label readability',
)

# Range title is the action name; make it easier to catch at a glance.
replace_once(
    ui,
    '  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);\n\n  b.valueLabel = lv_label_create(top);',
    '  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_14 : &mira_font_fr_16, 0);\n\n  b.valueLabel = lv_label_create(top);',
    'range label readability',
)

# Round thermostat title: same visual weight as the other interactive controls.
replace_once(
    ui,
    '  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);\n  lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 0);',
    '  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_14 : &mira_font_fr_16, 0);\n  lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 0);',
    'round range label readability',
)

# List heading and choice labels gain the same readability bump. Segmented remains
# compact enough for three choices on a row, but the text is much easier to scan.
replace_once(
    ui,
    '  lv_obj_set_style_text_font(listName, (compact || segmented) ? &mira_font_fr_12 : &mira_font_fr_14, 0);',
    '  lv_obj_set_style_text_font(listName, (compact || segmented) ? &mira_font_fr_14 : &mira_font_fr_16, 0);',
    'list title readability',
)
replace_once(
    ui,
    '    lv_obj_set_style_text_font(lbl, (compact || segmented) ? &mira_font_fr_12 : &mira_font_fr_14, 0);',
    '    lv_obj_set_style_text_font(lbl, (compact || segmented) ? &mira_font_fr_14 : &mira_font_fr_16, 0);',
    'list choice readability',
)

# A warm, short press colour makes commands visibly acknowledge the finger, as on
# the reference panel, while the normal/selected colours remain Mira cyan.
replace_once(
    ui,
    '  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);\n  lv_obj_set_style_transform_zoom(b.control, 244, LV_STATE_PRESSED);',
    '  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xF2A23A), LV_STATE_PRESSED);\n  lv_obj_set_style_transform_zoom(b.control, 244, LV_STATE_PRESSED);',
    'button warm press confirmation',
)
replace_all_exact(
    ui,
    '      lv_obj_set_style_bg_color(btn, colorAccent(), LV_STATE_PRESSED);',
    '      lv_obj_set_style_bg_color(btn, lv_color_hex(0xF2A23A), LV_STATE_PRESSED);',
    2,
    'list warm press confirmation',
)

# Bottom navigation keeps the existing three-destination concept, but gives a
# small tactile press animation. The current page remains intentionally absent.
replace_once(
    ui,
    '    lv_obj_set_style_shadow_width(_navButtons[i], 0, 0);\n    lv_obj_add_event_cb(_navButtons[i], onNav, LV_EVENT_CLICKED, (void*)(uintptr_t)i);',
    '    lv_obj_set_style_shadow_width(_navButtons[i], 0, 0);\n'
    '    lv_obj_set_style_transform_zoom(_navButtons[i], 246, LV_STATE_PRESSED);\n'
    '    lv_obj_set_style_anim_time(_navButtons[i], 100, LV_STATE_DEFAULT);\n'
    '    lv_obj_add_event_cb(_navButtons[i], onNav, LV_EVENT_CLICKED, (void*)(uintptr_t)i);',
    'navigation touch feedback',
)

# Page changes were abrupt. Fade in the new page very briefly while preserving
# the current hidden-page architecture and fixed bottom navigation.
replace_once(
    ui,
    '''void MiraPanelUI::showPage(uint8_t index) {
  if (index >= PAGE_COUNT) return;
  for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
    if (i == index) lv_obj_clear_flag(_pages[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(_pages[i], LV_OBJ_FLAG_HIDDEN);
  }
  _currentPage = index;
  setNavActive(index);
  noteActivity();
}''',
    '''void MiraPanelUI::showPage(uint8_t index) {
  if (index >= PAGE_COUNT) return;
  for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
    if (i == index) {
      lv_obj_clear_flag(_pages[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_fade_in(_pages[i], 160, 0);
    } else {
      lv_obj_add_flag(_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  _currentPage = index;
  setNavActive(index);
  noteActivity();
}''',
    'page fade transition',
)

print('Mira Panel 0.5.6 readability, spacing, navigation and touch confirmation applied')
