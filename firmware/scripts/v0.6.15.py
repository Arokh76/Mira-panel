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


# 0.6.15 continues the visual redesign on the Lights and Temperature pages only.
# Widget types, IDs, Jeedom actions, HTTP/MQTT and event callbacks are untouched.
replace_once(ino, '"0.6.14"', '"0.6.15"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.14 initialise', '[MIRA] Firmware 0.6.15 initialise', 'startup log')

# Keep the same 8 px breathing margin used by Home on the two user widget pages.
replace_once(
    ui,
    '''    } else {
      lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }''',
    '''    } else {
      if (i == 1 || i == 2) lv_obj_set_style_pad_top(_pages[i], 8, 0);
      lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }''',
    'secondary page breathing margin',
)

# Lights: make switch rows denser and cleaner while preserving the exact switch callback.
replace_function(ui, 'void MiraPanelUI::createToggle(uint8_t idx) {', r'''void MiraPanelUI::createToggle(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  b.card = makeCard(pageIndex(b.location), compact ? 44 : 54);
  lv_obj_fade_in(b.card, 140, 0);
  lv_obj_set_style_pad_left(b.card, compact ? 14 : 16, 0);
  lv_obj_set_style_pad_right(b.card, compact ? 14 : 16, 0);
  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* name = lv_label_create(b.card);
  lv_label_set_text(name, b.name.c_str());
  lv_obj_set_style_text_color(name, COL_TEXT, 0);
  lv_obj_set_style_text_font(name, compact ? &mira_font_fr_14 : &mira_font_fr_16, 0);

  b.stateLabel = nullptr;
  b.control = lv_switch_create(b.card);
  lv_obj_set_size(b.control, compact ? 46 : 54, compact ? 23 : 28);
  lv_obj_set_ext_click_area(b.control, 6);
  lv_obj_set_style_bg_color(b.control, colorPanel2(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xF0F8FC), LV_PART_KNOB);
  lv_obj_set_style_border_color(b.control, colorBorder(), LV_PART_MAIN);
  lv_obj_set_style_border_width(b.control, 1, LV_PART_MAIN);
  lv_obj_set_style_anim_time(b.control, 160, 0);
  lv_obj_set_style_shadow_color(b.control, colorAccent(), LV_PART_KNOB | LV_STATE_CHECKED);
  lv_obj_set_style_shadow_opa(b.control, LV_OPA_20, LV_PART_KNOB | LV_STATE_CHECKED);
  lv_obj_set_style_shadow_width(b.control, 5, LV_PART_KNOB | LV_STATE_CHECKED);
  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onToggle, LV_EVENT_VALUE_CHANGED, ctx);
}''', 'cleaner toggle rows')

# Color wheel: keep the proven hue interaction, only tighten its visual footprint.
replace_once(ui, 'b.card = makeCard(pageIndex(b.location), 210);',
             'b.card = makeCard(pageIndex(b.location), 196);', 'color card height')
replace_once(ui, 'lv_obj_set_size(b.control, 160, 160);',
             'lv_obj_set_size(b.control, 150, 150);', 'color wheel size')
replace_once(ui, 'lv_obj_align(b.control, LV_ALIGN_CENTER, 0, 12);',
             'lv_obj_align(b.control, LV_ALIGN_CENTER, 0, 10);', 'color wheel alignment')

# Temperature: preserve the same thermostat control and 0.5 degree mechanics,
# but reduce the oversized hero card so the current temperature and mode breathe.
replace_once(ui, 'const int cardH = compact ? 202 : 260;',
             'const int cardH = compact ? 194 : 238;', 'thermostat card height')
replace_once(ui, 'const int plate = compact ? 158 : 216;',
             'const int plate = compact ? 154 : 196;', 'thermostat plate size')
replace_once(ui, 'const int arc = compact ? 146 : 200;',
             'const int arc = compact ? 142 : 182;', 'thermostat arc size')
replace_once(ui, 'const int innerSize = compact ? 82 : 112;',
             'const int innerSize = compact ? 80 : 104;', 'thermostat inner size')
replace_once(ui, 'const int plateY = compact ? 18 : 19;',
             'const int plateY = compact ? 18 : 18;', 'thermostat plate position')
replace_once(ui, 'const int arcY = compact ? 24 : 27;',
             'const int arcY = compact ? 24 : 25;', 'thermostat arc position')
replace_once(ui, 'const int innerY = compact ? 57 : 71;',
             'const int innerY = compact ? 55 : 64;', 'thermostat inner position')

# Dropdown lists become a little more compact; list-to-buttons is deliberately untouched.
replace_once(ui,
             'b.card = makeCard(pageIndex(b.location), asButtons ? LV_SIZE_CONTENT : (compact ? 68 : 82));',
             'b.card = makeCard(pageIndex(b.location), asButtons ? LV_SIZE_CONTENT : (compact ? 64 : 76));',
             'dropdown card height')
replace_once(ui, 'lv_obj_set_width(b.control, compact ? 230 : 260);',
             'lv_obj_set_width(b.control, compact ? 236 : 268);', 'dropdown width')

print('Mira Panel 0.6.15 lighting + temperature page visual polish applied; integration unchanged')
