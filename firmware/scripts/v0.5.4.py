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


# Version
replace_once(ino, '"0.5.3"', '"0.5.4"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.3 initialise', '[MIRA] Firmware 0.5.4 initialise', 'startup log')

# Lists keep the existing Standard / Compact modes. For the existing
# "Liste vers boutons" widget, a third "segmented" presentation provides a
# compact touch-friendly segmented control. It does not change command order,
# IDs or MQTT/Jeedom behavior: only LVGL presentation changes.
replace_function(ui, 'void MiraPanelUI::createList(uint8_t idx, bool asButtons) {', r'''void MiraPanelUI::createList(uint8_t idx, bool asButtons) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  const bool segmented = asButtons && b.style == "segmented";

  if (b.listCount == 0) {
    createUnsupported(idx, "liste vide");
    return;
  }

  b.card = makeCard(pageIndex(b.location), asButtons ? LV_SIZE_CONTENT : (compact ? 68 : 82));
  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(b.card, (compact || segmented) ? 3 : 5, 0);

  lv_obj_t* listName = lv_label_create(b.card);
  lv_label_set_text(listName, b.name.c_str());
  lv_obj_set_width(listName, 280);
  lv_label_set_long_mode(listName, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(listName, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(listName, segmented ? COL_MUTED : COL_TEXT, 0);
  lv_obj_set_style_text_font(listName, (compact || segmented) ? &mira_font_fr_12 : &mira_font_fr_14, 0);

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
    return;
  }

  lv_obj_t* wrap = lv_obj_create(b.card);
  lv_obj_set_width(wrap, 280);
  lv_obj_set_height(wrap, LV_SIZE_CONTENT);
  lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(wrap, segmented ? 1 : 0, 0);
  lv_obj_set_style_border_color(wrap, colorBorder(), 0);
  lv_obj_set_style_radius(wrap, segmented ? 12 : 0, 0);
  lv_obj_set_style_bg_color(wrap, colorPanel2(), 0);
  lv_obj_set_style_bg_opa(wrap, segmented ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(wrap, segmented ? 3 : 0, 0);
  lv_obj_set_style_pad_row(wrap, segmented ? 3 : (compact ? 4 : 5), 0);
  lv_obj_set_style_pad_column(wrap, segmented ? 3 : (compact ? 4 : 5), 0);
  lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  for (uint8_t i = 0; i < b.listCount; ++i) {
    lv_obj_t* btn = lv_btn_create(wrap);
    b.listButtons[i] = btn;

    if (segmented) {
      const int btnWidth = b.listCount <= 2 ? 134 : 88;
      lv_obj_set_size(btn, btnWidth, 32);
      lv_obj_set_style_radius(btn, 9, 0);
      lv_obj_set_style_border_width(btn, 0, 0);
      lv_obj_set_style_shadow_width(btn, 0, 0);
      lv_obj_set_style_bg_color(btn, colorPanel2(), 0);
      lv_obj_set_style_bg_color(btn, colorAccent(), LV_STATE_PRESSED);
    } else {
      lv_obj_set_size(btn, compact ? 88 : 132, compact ? 34 : 38);
      lv_obj_set_style_bg_color(btn, colorAccent(), LV_STATE_PRESSED);
    }

    EventContext* ctx = makeContext(idx, i);
    lv_obj_add_event_cb(btn, onListButton, LV_EVENT_CLICKED, ctx);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, b.listLabels[i].c_str());
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl, (compact || segmented) ? &mira_font_fr_12 : &mira_font_fr_14, 0);
    lv_obj_center(lbl);
  }

  updateListVisual(b);
}''', 'segmented list style')

print('Mira Panel 0.5.4 segmented list style applied')
