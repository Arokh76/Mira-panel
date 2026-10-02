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
replace_once(ino, '"0.5.1"', '"0.5.2"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.1 initialise', '[MIRA] Firmware 0.5.2 initialise', 'startup log')

# Keep vertical swipe on pages, but hide the scrollbar. On a 320 px wide panel it
# wastes useful space and looks like a desktop/web scrollbar.
replace_once(
    ui,
    '    lv_obj_set_scrollbar_mode(_pages[i], LV_SCROLLBAR_MODE_AUTO);',
    '    lv_obj_set_scrollbar_mode(_pages[i], LV_SCROLLBAR_MODE_OFF);',
    'hide page scrollbar while keeping vertical scroll',
)

# 0.5.1 created the Range title/value row and the slider in the same card, but
# the card had no layout. LVGL therefore placed both children at the same origin,
# making the slider cover the title/value. Stack them vertically instead.
replace_once(
    ui,
    '''  b.card = makeCard(pageIndex(b.location), compact ? 68 : 90);\n\n  lv_obj_t* top = makeTransparentRow(b.card, compact ? 22 : 28);''',
    '''  b.card = makeCard(pageIndex(b.location), compact ? 68 : 90);\n  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);\n  lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_COLUMN);\n  lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);\n  lv_obj_set_style_pad_row(b.card, compact ? 5 : 7, 0);\n\n  lv_obj_t* top = makeTransparentRow(b.card, compact ? 22 : 28);''',
    'range vertical layout',
)

# Make the slider track explicit and consistent in all three themes.
replace_once(
    ui,
    '''  lv_obj_set_size(b.control, compact ? 250 : 280, compact ? 14 : 18);\n  lv_slider_set_range(b.control, b.minValue, b.maxValue);''',
    '''  lv_obj_set_size(b.control, compact ? 250 : 280, compact ? 14 : 18);\n  lv_obj_set_style_bg_color(b.control, colorPanel2(), LV_PART_MAIN);\n  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, LV_PART_MAIN);\n  lv_obj_set_style_radius(b.control, LV_RADIUS_CIRCLE, LV_PART_MAIN);\n  lv_obj_set_style_radius(b.control, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);\n  lv_slider_set_range(b.control, b.minValue, b.maxValue);''',
    'range track styling',
)

print('Mira Panel 0.5.2 slider layout + invisible page scrollbar applied')
