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


def replace_function(path: Path, signature: str, new_function: str, label: str):
    text = path.read_text(encoding='utf-8')
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f'{label}: signature not found in {path}')
    next_start = text.find('\nvoid MiraPanelUI::', start + len(signature))
    if next_start < 0:
        raise SystemExit(f'{label}: next function not found in {path}')
    path.write_text(text[:start] + new_function.rstrip() + '\n' + text[next_start + 1:], encoding='utf-8')


# 0.6.12: reclaim the top of the dashboard. The clock/date becomes a compact
# header on the page background instead of a large framed card. Widget protocol,
# Jeedom IDs, MQTT/HTTP and callbacks remain unchanged.
replace_once(ino, '"0.6.11"', '"0.6.12"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.11 initialise', '[MIRA] Firmware 0.6.12 initialise', 'startup log')

# Home dashboard may use the very top of the content area; other pages preserve
# their existing padding.
replace_once(
    ui,
    '''    if (i == 0) {
      lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    } else {''',
    '''    if (i == 0) {
      lv_obj_set_style_pad_top(_pages[i], 0, 0);
      lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    } else {''',
    'home top padding',
)

replace_function(ui, 'void MiraPanelUI::createDateTime(uint8_t idx) {', r'''void MiraPanelUI::createDateTime(uint8_t idx) {
  Binding& b = _bindings[idx];

  // Dashboard clock = header, not another card. This keeps the visual hierarchy
  // while recovering roughly one widget row of useful screen height.
  b.card = makeCard(pageIndex(b.location), 82);
  lv_obj_set_width(b.card, 304);
  lv_obj_fade_in(b.card, 150, 0);
  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(b.card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(b.card, 0, 0);
  lv_obj_set_style_shadow_width(b.card, 0, 0);
  lv_obj_set_style_pad_all(b.card, 0, 0);

  b.valueLabel = lv_label_create(b.card);
  lv_label_set_text(b.valueLabel, "--:--");
  lv_obj_set_width(b.valueLabel, 304);
  lv_obj_set_style_text_color(b.valueLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_40, 0);
  lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(b.valueLabel, LV_ALIGN_TOP_MID, 0, -2);

  _dateSubLabel = lv_label_create(b.card);
  lv_label_set_text(_dateSubLabel, "-- -- ----");
  lv_obj_set_width(_dateSubLabel, 300);
  lv_obj_set_style_text_color(_dateSubLabel, COL_MUTED, 0);
  lv_obj_set_style_text_font(_dateSubLabel, &mira_font_fr_14, 0);
  lv_obj_set_style_text_align(_dateSubLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(_dateSubLabel, LV_ALIGN_TOP_MID, 0, 47);

  if (!_dateTimeLabel) _dateTimeLabel = b.valueLabel;
}''', 'compact frameless clock header')

# applyTheme() normally repaints every widget card. Reassert the frameless clock
# header after a live theme change so it never regains the old rounded panel.
replace_once(
    ui,
    '    if (b.card) styleMainCard(b.card, b.type == W_DATETIME);',
    '''    if (b.card) {
      styleMainCard(b.card, b.type == W_DATETIME);
      if (b.type == W_DATETIME) {
        lv_obj_set_style_bg_opa(b.card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b.card, 0, 0);
        lv_obj_set_style_shadow_width(b.card, 0, 0);
      }
    }''',
    'datetime theme refresh flattening',
)

print('Mira Panel 0.6.12 compact frameless dashboard clock header applied; integration unchanged')
