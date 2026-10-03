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


# Version
replace_once(ino, '"0.5.4"', '"0.5.5"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.4 initialise', '[MIRA] Firmware 0.5.5 initialise', 'startup log')

# Gentle widget entrance animation. The goal is to give the interface some life
# without delaying interaction or making the panel feel "busy".
for old, new, label in [
    (
        '    b.card = makeCard(pageIndex(b.location), 72);\n    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);',
        '    b.card = makeCard(pageIndex(b.location), 72);\n    lv_obj_fade_in(b.card, 150, 0);\n    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);',
        'value info fade',
    ),
    (
        '  b.card = makeCard(pageIndex(b.location), compact ? 30 : 36);\n  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);',
        '  b.card = makeCard(pageIndex(b.location), compact ? 30 : 36);\n  lv_obj_fade_in(b.card, 140, 0);\n  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);',
        'info fade',
    ),
    (
        '  b.card = makeCard(pageIndex(b.location), compact ? 48 : 62);\n  b.control = lv_btn_create(b.card);',
        '  b.card = makeCard(pageIndex(b.location), compact ? 48 : 62);\n  lv_obj_fade_in(b.card, 140, 0);\n  b.control = lv_btn_create(b.card);',
        'button fade',
    ),
    (
        '  b.card = makeCard(pageIndex(b.location), compact ? 48 : 64);\n  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);',
        '  b.card = makeCard(pageIndex(b.location), compact ? 48 : 64);\n  lv_obj_fade_in(b.card, 140, 0);\n  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);',
        'toggle fade',
    ),
    (
        '  b.card = makeCard(pageIndex(b.location), compact ? 68 : 90);\n  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);',
        '  b.card = makeCard(pageIndex(b.location), compact ? 68 : 90);\n  lv_obj_fade_in(b.card, 150, 0);\n  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);',
        'range fade',
    ),
    (
        '  b.card = makeCard(pageIndex(b.location), cardH);\n\n  lv_obj_t* name = lv_label_create(b.card);',
        '  b.card = makeCard(pageIndex(b.location), cardH);\n  lv_obj_fade_in(b.card, 160, 0);\n\n  lv_obj_t* name = lv_label_create(b.card);',
        'round range fade',
    ),
    (
        '  b.card = makeCard(pageIndex(b.location), asButtons ? LV_SIZE_CONTENT : (compact ? 68 : 82));\n  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_COLUMN);',
        '  b.card = makeCard(pageIndex(b.location), asButtons ? LV_SIZE_CONTENT : (compact ? 68 : 82));\n  lv_obj_fade_in(b.card, 150, 0);\n  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_COLUMN);',
        'list fade',
    ),
]:
    replace_once(ui, old, new, label)

# Push feedback on regular buttons: slight scale + glow while the finger is down.
replace_once(
    ui,
    '  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);\n  lv_obj_set_style_radius(b.control, compact ? 10 : 9, 0);',
    '  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);\n'
    '  lv_obj_set_style_transform_zoom(b.control, 244, LV_STATE_PRESSED);\n'
    '  lv_obj_set_style_shadow_color(b.control, colorAccent(), LV_STATE_PRESSED);\n'
    '  lv_obj_set_style_shadow_opa(b.control, LV_OPA_40, LV_STATE_PRESSED);\n'
    '  lv_obj_set_style_shadow_width(b.control, 10, LV_STATE_PRESSED);\n'
    '  lv_obj_set_style_radius(b.control, compact ? 10 : 9, 0);',
    'button press feedback',
)

# Switch movement is short and smooth instead of snapping from one side to the other.
replace_once(
    ui,
    '  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xE6ECEF), LV_PART_KNOB);\n  EventContext* ctx = makeContext(idx);',
    '  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xE6ECEF), LV_PART_KNOB);\n'
    '  lv_obj_set_style_anim_time(b.control, 160, 0);\n'
    '  lv_obj_set_style_shadow_color(b.control, colorAccent(), LV_PART_KNOB | LV_STATE_CHECKED);\n'
    '  lv_obj_set_style_shadow_opa(b.control, LV_OPA_30, LV_PART_KNOB | LV_STATE_CHECKED);\n'
    '  lv_obj_set_style_shadow_width(b.control, 7, LV_PART_KNOB | LV_STATE_CHECKED);\n'
    '  EventContext* ctx = makeContext(idx);',
    'toggle animation',
)

# Slider knob gives a tiny tactile pop/glow while dragging.
replace_once(
    ui,
    '  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_KNOB);\n  EventContext* ctx = makeContext(idx);',
    '  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_KNOB);\n'
    '  lv_obj_set_style_transform_zoom(b.control, 238, LV_PART_KNOB | LV_STATE_PRESSED);\n'
    '  lv_obj_set_style_shadow_color(b.control, colorAccent(), LV_PART_KNOB | LV_STATE_PRESSED);\n'
    '  lv_obj_set_style_shadow_opa(b.control, LV_OPA_40, LV_PART_KNOB | LV_STATE_PRESSED);\n'
    '  lv_obj_set_style_shadow_width(b.control, 9, LV_PART_KNOB | LV_STATE_PRESSED);\n'
    '  EventContext* ctx = makeContext(idx);',
    'slider drag feedback',
)

# Round thermostat: the active arc thickens very slightly while touched.
replace_once(
    ui,
    '  lv_obj_set_style_arc_width(b.control, compact ? 12 : 15, LV_PART_INDICATOR);\n  lv_obj_set_style_arc_color(b.control, lv_color_hex(0x1B2229), LV_PART_MAIN);',
    '  lv_obj_set_style_arc_width(b.control, compact ? 12 : 15, LV_PART_INDICATOR);\n'
    '  lv_obj_set_style_arc_width(b.control, compact ? 15 : 18, LV_PART_INDICATOR | LV_STATE_PRESSED);\n'
    '  lv_obj_set_style_arc_color(b.control, lv_color_hex(0x1B2229), LV_PART_MAIN);',
    'round range touch feedback',
)

# Both classic list buttons and segmented buttons get the same small press motion.
# The selected item also gets a thin accent outline and a quick fade when selection changes.
replace_once(
    ui,
    '      lv_obj_set_style_shadow_width(btn, 0, 0);\n      lv_obj_set_style_bg_color(btn, colorPanel2(), 0);',
    '      lv_obj_set_style_shadow_width(btn, 0, 0);\n'
    '      lv_obj_set_style_transform_zoom(btn, 244, LV_STATE_PRESSED);\n'
    '      lv_obj_set_style_shadow_color(btn, colorAccent(), LV_STATE_PRESSED);\n'
    '      lv_obj_set_style_shadow_opa(btn, LV_OPA_30, LV_STATE_PRESSED);\n'
    '      lv_obj_set_style_shadow_width(btn, 8, LV_STATE_PRESSED);\n'
    '      lv_obj_set_style_bg_color(btn, colorPanel2(), 0);',
    'segmented press feedback',
)
replace_once(
    ui,
    '      lv_obj_set_size(btn, compact ? 88 : 132, compact ? 34 : 38);\n      lv_obj_set_style_bg_color(btn, colorAccent(), LV_STATE_PRESSED);',
    '      lv_obj_set_size(btn, compact ? 88 : 132, compact ? 34 : 38);\n'
    '      lv_obj_set_style_bg_color(btn, colorAccent(), LV_STATE_PRESSED);\n'
    '      lv_obj_set_style_transform_zoom(btn, 244, LV_STATE_PRESSED);',
    'classic list press feedback',
)
replace_once(
    ui,
    'lv_obj_set_style_bg_color(b.listButtons[i], i == (uint8_t)b.selectedIndex ? colorAccent2() : colorPanel2(), 0);',
    'const bool miraSelected = i == (uint8_t)b.selectedIndex;\n'
    '    lv_obj_set_style_bg_color(b.listButtons[i], miraSelected ? colorAccent2() : colorPanel2(), 0);\n'
    '    lv_obj_set_style_border_color(b.listButtons[i], colorAccent(), 0);\n'
    '    lv_obj_set_style_border_width(b.listButtons[i], miraSelected ? 1 : 0, 0);\n'
    '    if (miraSelected) lv_obj_fade_in(b.listButtons[i], 90, 0);',
    'list selection feedback',
)

print('Mira Panel 0.5.5 micro animations and touch feedback applied')
