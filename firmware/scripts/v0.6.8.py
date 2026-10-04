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


# 0.6.8 changes presentation/layout only. Jeedom widget JSON, IDs, server commands,
# MQTT topics and all interaction callbacks remain exactly the same.
replace_once(ino, '"0.6.7"', '"0.6.8"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.7 initialise', '[MIRA] Firmware 0.6.8 initialise', 'startup log')

# Home becomes an adaptive dashboard: full-width widgets still occupy one row,
# while ordinary Info widgets can naturally pair as compact dashboard tiles.
# Other pages keep their existing vertical layout, so lighting/thermostat/color
# screens are not disturbed.
replace_once(
    ui,
    '''    lv_obj_set_style_pad_all(_pages[i], 6, 0);
    lv_obj_set_style_pad_row(_pages[i], 6, 0);
    lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);''',
    '''    lv_obj_set_style_pad_all(_pages[i], 6, 0);
    lv_obj_set_style_pad_row(_pages[i], 6, 0);
    lv_obj_set_style_pad_column(_pages[i], 6, 0);
    if (i == 0) {
      lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_ROW_WRAP);
      lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    } else {
      lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }''',
    'adaptive home dashboard flex',
)

replace_function(ui, 'void MiraPanelUI::createInfo(uint8_t idx) {', r'''void MiraPanelUI::createInfo(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  const bool valueDominant = b.style == "value";
  const uint8_t page = pageIndex(b.location);
  const bool dashboardTile = page == 0 && b.style == "standard";

  // Standard Info widgets on the home page become automatic two-column tiles.
  // Nothing changes in Jeedom: same widget type, same name/unit, same info ID.
  if (dashboardTile) {
    b.card = makeCard(page, 66);
    lv_obj_set_width(b.card, 149);
    lv_obj_fade_in(b.card, 140, 0);
    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(b.card, 4, 0);

    b.valueLabel = lv_label_create(b.card);
    String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
    lv_label_set_text(b.valueLabel, txt.c_str());
    lv_obj_set_width(b.valueLabel, 137);
    lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_20, 0);
    lv_obj_align(b.valueLabel, LV_ALIGN_TOP_MID, 0, 7);

    lv_obj_t* name = lv_label_create(b.card);
    lv_label_set_text(name, b.name.c_str());
    lv_obj_set_width(name, 137);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, COL_MUTED, 0);
    lv_obj_set_style_text_font(name, &mira_font_fr_12, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -6);
    return;
  }

  // Existing explicit "value" presentation stays available as the full-width
  // hero/value card and keeps exactly the same semantics.
  if (valueDominant) {
    b.card = makeCard(page, 72);
    lv_obj_fade_in(b.card, 150, 0);
    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(b.card, 4, 0);

    b.valueLabel = lv_label_create(b.card);
    String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
    lv_label_set_text(b.valueLabel, txt.c_str());
    lv_obj_set_width(b.valueLabel, 286);
    lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_28, 0);
    lv_obj_align(b.valueLabel, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_t* name = lv_label_create(b.card);
    lv_label_set_text(name, b.name.c_str());
    lv_obj_set_width(name, 286);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, COL_MUTED, 0);
    lv_obj_set_style_text_font(name, &mira_font_fr_12, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -3);
    return;
  }

  // Compact info and info widgets on non-home pages preserve the familiar row.
  b.card = makeCard(page, compact ? 30 : 36);
  lv_obj_fade_in(b.card, 140, 0);
  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_left(b.card, 4, 0);
  lv_obj_set_style_pad_right(b.card, 4, 0);
  lv_obj_set_style_pad_top(b.card, 0, 0);
  lv_obj_set_style_pad_bottom(b.card, 0, 0);
  lv_obj_set_style_pad_column(b.card, compact ? 4 : 6, 0);
  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  String displayName = b.name;
  if (displayName.length() && !displayName.endsWith(":")) displayName += ":";

  lv_obj_t* name = lv_label_create(b.card);
  lv_label_set_text(name, displayName.c_str());
  lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(name, COL_TEXT, 0);
  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);

  b.valueLabel = lv_label_create(b.card);
  String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
  lv_label_set_text(b.valueLabel, txt.c_str());
  lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
  lv_obj_set_style_text_font(b.valueLabel, compact ? &lv_font_montserrat_14 : &lv_font_montserrat_16, 0);
}''', 'adaptive info dashboard tiles')

print('Mira Panel 0.6.8 adaptive home dashboard applied; Jeedom widget integration unchanged')
