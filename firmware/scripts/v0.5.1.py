from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
server = root / 'MiraPanelServer.cpp'
spiffs = root / 'SPIFFS_Process.h'
ui = root / 'MiraPanelUI.cpp'
hdr = root / 'MiraPanelUI.h'


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


def replace_function(path: Path, signature: str, new_function: str, label: str):
    text = path.read_text(encoding='utf-8')
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f'{label}: signature not found in {path}')
    next_start = text.find('\nvoid MiraPanelUI::', start + len(signature))
    if next_start < 0:
        raise SystemExit(f'{label}: next function not found in {path}')
    path.write_text(text[:start] + new_function.rstrip() + '\n' + text[next_start + 1:], encoding='utf-8')


# Version
replace_once(ino, '"0.5.0"', '"0.5.1"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.0 initialise', '[MIRA] Firmware 0.5.1 initialise', 'startup log')

# Persist one presentation style with each widget. Old configurations remain valid.
replace_once(
    spiffs,
    'String LVGL_Unit = "";\n',
    'String LVGL_Unit = "";\nString LVGL_Style = "standard";\n',
    'LVGL style storage',
)
replace_once(
    spiffs,
    '  lvgl_array["unit"] = LVGL_Unit;\n',
    '  lvgl_array["unit"] = LVGL_Unit;\n  lvgl_array["style"] = LVGL_Style.length() ? LVGL_Style : "standard";\n',
    'LVGL style json',
)

# /addlistlvgl and /screenadd accept an optional style query argument.
replace_all_exact(
    server,
    '                    LVGL_Unit = String(p->value().c_str());\n                    AddListLVGL();',
    '                    LVGL_Unit = String(p->value().c_str());\n                    LVGL_Style = request->hasParam("style") ? request->getParam("style")->value() : "standard";\n                    LVGL_Style.trim();\n                    LVGL_Style.toLowerCase();\n                    if (!LVGL_Style.length()) LVGL_Style = "standard";\n                    AddListLVGL();',
    2,
    'style query support',
)

# Binding remembers the presentation style separately from widget type/order.
replace_once(
    hdr,
    '    String unit;\n    String location;\n',
    '    String unit;\n    String location;\n    String style = "standard";\n',
    'binding style member',
)

replace_once(
    ui,
    '  String sValue = obj["value"] | "";\n\n  uint8_t idx = addBinding();',
    '  String sValue = obj["value"] | "";\n  String widgetStyle = obj["style"] | "standard";\n  widgetStyle.trim();\n  widgetStyle.toLowerCase();\n  if (!widgetStyle.length()) widgetStyle = "standard";\n\n  uint8_t idx = addBinding();',
    'read widget style',
)
replace_once(
    ui,
    '  b.location = location;\n  b.unit = unit;\n',
    '  b.location = location;\n  b.unit = unit;\n  b.style = widgetStyle;\n',
    'assign widget style',
)

replace_function(ui, 'void MiraPanelUI::createInfo(uint8_t idx) {', r'''void MiraPanelUI::createInfo(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  const bool valueDominant = b.style == "value";

  if (valueDominant) {
    b.card = makeCard(pageIndex(b.location), 72);
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

  b.card = makeCard(pageIndex(b.location), compact ? 30 : 36);
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
}''', 'style-aware info')

replace_function(ui, 'void MiraPanelUI::createButton(uint8_t idx) {', r'''void MiraPanelUI::createButton(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  b.card = makeCard(pageIndex(b.location), compact ? 48 : 62);
  b.control = lv_btn_create(b.card);
  lv_obj_set_size(b.control, compact ? 220 : 280, compact ? 34 : 42);
  lv_obj_center(b.control);
  lv_obj_set_style_bg_color(b.control, colorAccent2(), 0);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b.control, compact ? 10 : 9, 0);
  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onButton, LV_EVENT_CLICKED, ctx);
  lv_obj_t* lbl = lv_label_create(b.control);
  lv_label_set_text(lbl, b.name.c_str());
  lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
  lv_obj_set_style_text_font(lbl, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);
  lv_obj_center(lbl);
}''', 'style-aware button')

replace_function(ui, 'void MiraPanelUI::createToggle(uint8_t idx) {', r'''void MiraPanelUI::createToggle(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  b.card = makeCard(pageIndex(b.location), compact ? 48 : 64);
  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* name = lv_label_create(b.card);
  lv_label_set_text(name, b.name.c_str());
  lv_obj_set_style_text_color(name, COL_TEXT, 0);
  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);

  b.stateLabel = nullptr;
  b.control = lv_switch_create(b.card);
  lv_obj_set_size(b.control, compact ? 48 : 58, compact ? 24 : 30);
  lv_obj_set_style_bg_color(b.control, lv_color_hex(0x55616A), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xE6ECEF), LV_PART_KNOB);
  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onToggle, LV_EVENT_VALUE_CHANGED, ctx);
}''', 'style-aware toggle')

replace_function(ui, 'void MiraPanelUI::createRange(uint8_t idx) {', r'''void MiraPanelUI::createRange(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  b.card = makeCard(pageIndex(b.location), compact ? 68 : 90);

  lv_obj_t* top = makeTransparentRow(b.card, compact ? 22 : 28);
  lv_obj_t* name = lv_label_create(top);
  lv_label_set_text(name, b.name.c_str());
  lv_obj_set_style_text_color(name, COL_TEXT, 0);
  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);

  b.valueLabel = lv_label_create(top);
  lv_label_set_text_fmt(b.valueLabel, "%d%s%s", b.currentValue, b.unit.length() ? " " : "", b.unit.c_str());
  lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
  lv_obj_set_style_text_font(b.valueLabel, compact ? &lv_font_montserrat_14 : &lv_font_montserrat_16, 0);

  b.control = lv_slider_create(b.card);
  lv_obj_set_size(b.control, compact ? 250 : 280, compact ? 14 : 18);
  lv_slider_set_range(b.control, b.minValue, b.maxValue);
  lv_slider_set_value(b.control, b.currentValue, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(b.control, colorAccent2(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_KNOB);
  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onRange, LV_EVENT_ALL, ctx);
}''', 'style-aware range')

replace_function(ui, 'void MiraPanelUI::createRoundRange(uint8_t idx) {', r'''void MiraPanelUI::createRoundRange(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  const int cardH = compact ? 202 : 260;
  const int plate = compact ? 158 : 216;
  const int arc = compact ? 146 : 200;
  const int innerSize = compact ? 82 : 112;
  const int plateY = compact ? 18 : 19;
  const int arcY = compact ? 24 : 27;
  const int innerY = compact ? 57 : 71;

  b.card = makeCard(pageIndex(b.location), cardH);

  lv_obj_t* name = lv_label_create(b.card);
  lv_label_set_text(name, b.name.c_str());
  lv_obj_set_style_text_color(name, COL_MUTED, 0);
  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);
  lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t* dialPlate = lv_obj_create(b.card);
  lv_obj_set_size(dialPlate, plate, plate);
  lv_obj_set_style_radius(dialPlate, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dialPlate, lv_color_hex(0x171D24), 0);
  lv_obj_set_style_bg_opa(dialPlate, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(dialPlate, colorBorder(), 0);
  lv_obj_set_style_border_width(dialPlate, 1, 0);
  lv_obj_set_style_pad_all(dialPlate, 0, 0);
  lv_obj_clear_flag(dialPlate, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(dialPlate, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(dialPlate, LV_ALIGN_TOP_MID, 0, plateY);

  b.control = lv_arc_create(b.card);
  lv_obj_set_size(b.control, arc, arc);
  lv_arc_set_range(b.control, b.minValue, b.maxValue);
  lv_arc_set_value(b.control, b.currentValue);
  lv_arc_set_bg_angles(b.control, 135, 45);
  lv_obj_align(b.control, LV_ALIGN_TOP_MID, 0, arcY);
  lv_obj_set_style_arc_width(b.control, compact ? 12 : 15, LV_PART_MAIN);
  lv_obj_set_style_arc_width(b.control, compact ? 12 : 15, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(b.control, lv_color_hex(0x1B2229), LV_PART_MAIN);
  lv_obj_set_style_arc_color(b.control, colorAccent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_border_width(b.control, 0, LV_PART_KNOB);
  lv_obj_set_style_pad_all(b.control, 0, LV_PART_KNOB);

  lv_obj_t* inner = lv_obj_create(b.card);
  lv_obj_set_size(inner, innerSize, innerSize);
  lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(inner, lv_color_hex(0x19212A), 0);
  lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(inner, colorBorder(), 0);
  lv_obj_set_style_border_width(inner, 2, 0);
  lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(inner, LV_ALIGN_TOP_MID, 0, innerY);

  b.valueLabel = lv_label_create(inner);
  String roundValue = roundRangeDisplayValue(b.currentValue, b.unit);
  lv_label_set_text(b.valueLabel, roundValue.c_str());
  lv_obj_set_style_text_color(b.valueLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(b.valueLabel, compact ? &lv_font_montserrat_20 : &lv_font_montserrat_28, 0);
  lv_obj_center(b.valueLabel);

  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onRoundRange, LV_EVENT_ALL, ctx);
}''', 'style-aware roundrange')

replace_function(ui, 'void MiraPanelUI::createList(uint8_t idx, bool asButtons) {', r'''void MiraPanelUI::createList(uint8_t idx, bool asButtons) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";

  if (b.listCount == 0) {
    createUnsupported(idx, "liste vide");
    return;
  }

  b.card = makeCard(pageIndex(b.location), asButtons ? LV_SIZE_CONTENT : (compact ? 68 : 82));
  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(b.card, compact ? 3 : 5, 0);

  lv_obj_t* listName = lv_label_create(b.card);
  lv_label_set_text(listName, b.name.c_str());
  lv_obj_set_width(listName, 280);
  lv_label_set_long_mode(listName, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(listName, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(listName, COL_TEXT, 0);
  lv_obj_set_style_text_font(listName, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);

  if (!asButtons) {
    b.control = lv_dropdown_create(b.card);
    lv_obj_set_width(b.control, compact ? 230 : 260);
    lv_obj_set_style_text_font(b.control, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);
    lv_obj_set_style_text_color(b.control, COL_TEXT, 0);
    lv_obj_set_style_bg_opa(b.control, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b.control, 0, 0);
    lv_obj_set_style_radius(b.control, 0, 0);

    String opts;
    for (uint8_t i = 0; i < b.listCount; ++i) {
      if (i) opts += "\n";
      opts += b.listLabels[i];
    }
    lv_dropdown_set_options(b.control, opts.c_str());
    lv_dropdown_set_selected(b.control, 0);
    EventContext* ctx = makeContext(idx);
    lv_obj_add_event_cb(b.control, onList, LV_EVENT_VALUE_CHANGED, ctx);
  } else {
    lv_obj_t* wrap = lv_obj_create(b.card);
    lv_obj_set_width(wrap, 280);
    lv_obj_set_height(wrap, LV_SIZE_CONTENT);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_set_style_pad_row(wrap, compact ? 4 : 5, 0);
    lv_obj_set_style_pad_column(wrap, compact ? 4 : 5, 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW_WRAP);

    for (uint8_t i = 0; i < b.listCount; ++i) {
      lv_obj_t* btn = lv_btn_create(wrap);
      b.listButtons[i] = btn;
      lv_obj_set_size(btn, compact ? 88 : 132, compact ? 34 : 38);
      EventContext* ctx = makeContext(idx, i);
      lv_obj_add_event_cb(btn, onListButton, LV_EVENT_CLICKED, ctx);
      lv_obj_t* lbl = lv_label_create(btn);
      lv_label_set_text(lbl, b.listLabels[i].c_str());
      lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
      lv_obj_set_style_text_font(lbl, compact ? &mira_font_fr_12 : &mira_font_fr_14, 0);
      lv_obj_center(lbl);
    }
    updateListVisual(b);
  }
}''', 'style-aware list')

print('Mira Panel 0.5.1 widget style engine applied')
