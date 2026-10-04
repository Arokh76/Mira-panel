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


def replace_function(path: Path, signature: str, new_function: str, label: str):
    text = path.read_text(encoding='utf-8')
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f'{label}: signature not found in {path}')
    next_start = text.find('\nvoid MiraPanelUI::', start + len(signature))
    if next_start < 0:
        raise SystemExit(f'{label}: next function not found in {path}')
    path.write_text(text[:start] + new_function.rstrip() + '\n' + text[next_start + 1:], encoding='utf-8')


# 0.6.10 continues the visual/dashboard pass only. Widget IDs, Jeedom commands,
# HTTP/MQTT behavior and touch callbacks remain unchanged.
replace_once(ino, '"0.6.9"', '"0.6.10"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.9 initialise', '[MIRA] Firmware 0.6.10 initialise', 'startup log')

# Keep enough state to balance the automatic home Info grid. If the number of
# standard Info tiles is odd, the final tile becomes a full-width horizontal card
# instead of sitting alone in the middle of the screen.
replace_once(
    hdr,
    '  Binding _bindings[MAX_BINDINGS];\n  uint8_t _bindingCount = 0;\n  EventContext _eventCtx[MAX_EVENT_CONTEXTS];',
    '  Binding _bindings[MAX_BINDINGS];\n  uint8_t _bindingCount = 0;\n  uint8_t _homeStandardInfoTotal = 0;\n  uint8_t _homeStandardInfoCreated = 0;\n  EventContext _eventCtx[MAX_EVENT_CONTEXTS];',
    'dashboard counters',
)

replace_once(
    ui,
    '  _bindingCount = 0;\n  _eventCtxCount = 0;',
    '  _bindingCount = 0;\n  _homeStandardInfoTotal = 0;\n  _homeStandardInfoCreated = 0;\n  _eventCtxCount = 0;',
    'reset dashboard counters',
)

replace_once(
    ui,
    '  for (JsonObject obj : arr) registerAndCreate(obj);',
    '''  _homeStandardInfoTotal = 0;
  _homeStandardInfoCreated = 0;
  for (JsonObject obj : arr) {
    String type = obj["type"] | "";
    String location = obj["emplacement"] | "accueil";
    String style = obj["style"] | "standard";
    type.trim();
    type.toLowerCase();
    style.trim();
    style.toLowerCase();
    if (!style.length()) style = "standard";
    if (type == "info" && pageIndex(location) == 0 && style == "standard") {
      ++_homeStandardInfoTotal;
    }
  }

  for (JsonObject obj : arr) registerAndCreate(obj);''',
    'pre-count home info tiles',
)

replace_function(ui, 'void MiraPanelUI::createInfo(uint8_t idx) {', r'''void MiraPanelUI::createInfo(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  const bool valueDominant = b.style == "value";
  const uint8_t page = pageIndex(b.location);
  const bool dashboardTile = page == 0 && b.style == "standard";

  if (dashboardTile) {
    ++_homeStandardInfoCreated;
    const bool lastOddTile = (_homeStandardInfoTotal & 1U) &&
                             (_homeStandardInfoCreated == _homeStandardInfoTotal);

    if (lastOddTile) {
      // A single final tile reads much better as a compact full-width status row.
      b.card = makeCard(page, 56);
      lv_obj_set_width(b.card, 304);
      lv_obj_fade_in(b.card, 140, 0);
      lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_pad_left(b.card, 16, 0);
      lv_obj_set_style_pad_right(b.card, 16, 0);
      lv_obj_set_style_pad_top(b.card, 0, 0);
      lv_obj_set_style_pad_bottom(b.card, 0, 0);
      lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

      lv_obj_t* name = lv_label_create(b.card);
      lv_label_set_text(name, b.name.c_str());
      lv_obj_set_width(name, 150);
      lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
      lv_obj_set_style_text_color(name, COL_MUTED, 0);
      lv_obj_set_style_text_font(name, &mira_font_fr_14, 0);

      b.valueLabel = lv_label_create(b.card);
      String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
      lv_label_set_text(b.valueLabel, txt.c_str());
      lv_obj_set_width(b.valueLabel, 116);
      lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
      lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_20, 0);
      return;
    }

    // Paired standard Info widgets remain the compact two-column dashboard tiles.
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
}''', 'balanced adaptive info tiles')

replace_function(ui, 'void MiraPanelUI::createDateTime(uint8_t idx) {', r'''void MiraPanelUI::createDateTime(uint8_t idx) {
  Binding& b = _bindings[idx];
  b.card = makeCard(pageIndex(b.location), 100);
  lv_obj_fade_in(b.card, 150, 0);
  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
  styleMainCard(b.card, true);
  lv_obj_set_style_pad_all(b.card, 0, 0);

  b.valueLabel = lv_label_create(b.card);
  lv_label_set_text(b.valueLabel, "--:--");
  lv_obj_set_style_text_color(b.valueLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_40, 0);
  lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(b.valueLabel, LV_ALIGN_TOP_MID, 0, 1);

  _dateSubLabel = lv_label_create(b.card);
  lv_label_set_text(_dateSubLabel, "-- -- ----");
  lv_obj_set_width(_dateSubLabel, 286);
  lv_obj_set_style_text_color(_dateSubLabel, COL_MUTED, 0);
  lv_obj_set_style_text_font(_dateSubLabel, &mira_font_fr_14, 0);
  lv_obj_set_style_text_align(_dateSubLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(_dateSubLabel, LV_ALIGN_TOP_MID, 0, 54);

  if (!_dateTimeLabel) _dateTimeLabel = b.valueLabel;
}''', 'slimmer dashboard clock card')

print('Mira Panel 0.6.10 balanced dashboard grid + slimmer clock applied; integration unchanged')
