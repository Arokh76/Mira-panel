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


# 0.6.11 is visual-only. Keep widget IDs, Jeedom commands, MQTT/HTTP and callbacks unchanged.
replace_once(ino, '"0.6.10"', '"0.6.11"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.10 initialise', '[MIRA] Firmware 0.6.11 initialise', 'startup log')

# The hardware photo showed a visible card-inside-card effect around ordinary buttons.
# Flatten the outer card and keep a single raised Mira action surface.
replace_function(ui, 'void MiraPanelUI::createButton(uint8_t idx) {', r'''void MiraPanelUI::createButton(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  b.card = makeCard(pageIndex(b.location), compact ? 44 : 54);
  lv_obj_fade_in(b.card, 140, 0);
  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(b.card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(b.card, 0, 0);
  lv_obj_set_style_shadow_width(b.card, 0, 0);
  lv_obj_set_style_pad_all(b.card, 0, 0);

  b.control = lv_btn_create(b.card);
  lv_obj_set_size(b.control, compact ? 230 : 288, compact ? 34 : 44);
  lv_obj_center(b.control);
  lv_obj_set_style_bg_color(b.control, colorPanel2(), 0);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b.control, colorAccent2(), 0);
  lv_obj_set_style_border_opa(b.control, LV_OPA_70, 0);
  lv_obj_set_style_border_width(b.control, 1, 0);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b.control, lv_color_hex(0x7DEEFF), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b.control, 1, LV_STATE_PRESSED);
  lv_obj_set_style_radius(b.control, 13, 0);
  lv_obj_set_style_shadow_color(b.control, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(b.control, LV_OPA_20, 0);
  lv_obj_set_style_shadow_width(b.control, 4, 0);
  lv_obj_set_style_shadow_ofs_y(b.control, 2, 0);
  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onButton, LV_EVENT_CLICKED, ctx);
  lv_obj_t* lbl = lv_label_create(b.control);
  lv_label_set_text(lbl, b.name.c_str());
  lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
  lv_obj_set_style_text_font(lbl, compact ? &mira_font_fr_14 : &mira_font_fr_16, 0);
  lv_obj_center(lbl);
}''', 'single-surface action button')

# applyTheme() restyles every card first; re-assert the transparent outer button card
# so a theme refresh never brings the double frame back.
replace_once(
    ui,
    '''    else if (b.type == W_BUTTON && b.control) {
      lv_obj_set_style_bg_color(b.control, colorPanel2(), 0);
      lv_obj_set_style_border_color(b.control, colorAccent2(), 0);
      lv_obj_set_style_border_opa(b.control, LV_OPA_70, 0);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);
    }''',
    '''    else if (b.type == W_BUTTON && b.control) {
      if (b.card) {
        lv_obj_set_style_bg_opa(b.card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b.card, 0, 0);
        lv_obj_set_style_shadow_width(b.card, 0, 0);
      }
      lv_obj_set_style_bg_color(b.control, colorPanel2(), 0);
      lv_obj_set_style_border_color(b.control, colorAccent2(), 0);
      lv_obj_set_style_border_opa(b.control, LV_OPA_70, 0);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);
    }''',
    'button theme refresh flattening',
)

# The slider still occupied more vertical space than the information cards in the
# real-panel photo. Tighten it without changing range/value/event behavior, and
# preserve a generous invisible touch area for finger use.
replace_once(ui, 'b.card = makeCard(pageIndex(b.location), compact ? 68 : 90);',
             'b.card = makeCard(pageIndex(b.location), compact ? 64 : 80);', 'range card height')
replace_once(ui, 'lv_obj_set_style_pad_row(b.card, compact ? 5 : 7, 0);',
             'lv_obj_set_style_pad_row(b.card, compact ? 4 : 5, 0);', 'range row spacing')
replace_once(ui, 'lv_obj_t* top = makeTransparentRow(b.card, compact ? 22 : 28);',
             'lv_obj_t* top = makeTransparentRow(b.card, compact ? 22 : 24);', 'range header height')
replace_once(ui, 'lv_obj_set_size(b.control, compact ? 220 : 250, compact ? 14 : 18);',
             'lv_obj_set_size(b.control, compact ? 230 : 258, compact ? 12 : 14);\n  lv_obj_set_ext_click_area(b.control, 8);', 'range track size')

print('Mira Panel 0.6.11 button flattening + compact slider polish applied; integration unchanged')
