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
replace_once(ino, '"0.5.7"', '"0.5.8"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.7 initialise', '[MIRA] Firmware 0.5.8 initialise', 'startup log')

# Regular button: the zoom/shadow press effect can make the LVGL object vanish on
# this panel while pressed. Keep the warm confirmation, force full opacity, and
# remove geometry-changing effects.
replace_once(
    ui,
    '''  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xF2A23A), LV_STATE_PRESSED);
  lv_obj_set_style_transform_zoom(b.control, 244, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_color(b.control, colorAccent(), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_opa(b.control, LV_OPA_40, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(b.control, 10, LV_STATE_PRESSED);''',
    '''  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xF2A23A), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b.control, lv_color_hex(0xFFC56B), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b.control, 2, LV_STATE_PRESSED);''',
    'stable button pressed state',
)

# Slider: moving a transformed/shadowed knob leaves stale pixels on the WT32 panel.
# Keep the real-time command path, but remove the moving transform and shadow.
replace_once(
    ui,
    '''  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_KNOB);
  lv_obj_set_style_transform_zoom(b.control, 238, LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_shadow_color(b.control, colorAccent(), LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_shadow_opa(b.control, LV_OPA_40, LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(b.control, 9, LV_PART_KNOB | LV_STATE_PRESSED);''',
    '''  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_KNOB);
  lv_obj_set_style_border_color(b.control, lv_color_hex(0x7DEEFF), LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b.control, 2, LV_PART_KNOB | LV_STATE_PRESSED);''',
    'stable slider knob redraw',
)

# Same transform family is used by list/segmented buttons; remove it proactively so
# they cannot exhibit the same disappearing/ghosting behaviour. Their warm pressed
# colour remains intact.
replace_once(
    ui,
    '''      lv_obj_set_style_transform_zoom(btn, 244, LV_STATE_PRESSED);
      lv_obj_set_style_shadow_color(btn, colorAccent(), LV_STATE_PRESSED);
      lv_obj_set_style_shadow_opa(btn, LV_OPA_30, LV_STATE_PRESSED);
      lv_obj_set_style_shadow_width(btn, 8, LV_STATE_PRESSED);''',
    '''      lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
      lv_obj_set_style_border_color(btn, lv_color_hex(0xFFC56B), LV_STATE_PRESSED);
      lv_obj_set_style_border_width(btn, 1, LV_STATE_PRESSED);''',
    'stable segmented press redraw',
)
replace_once(
    ui,
    '''      lv_obj_set_style_bg_color(btn, lv_color_hex(0xF2A23A), LV_STATE_PRESSED);
      lv_obj_set_style_transform_zoom(btn, 244, LV_STATE_PRESSED);''',
    '''      lv_obj_set_style_bg_color(btn, lv_color_hex(0xF2A23A), LV_STATE_PRESSED);
      lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
      lv_obj_set_style_border_color(btn, lv_color_hex(0xFFC56B), LV_STATE_PRESSED);
      lv_obj_set_style_border_width(btn, 1, LV_STATE_PRESSED);''',
    'stable classic list press redraw',
)

# The 18 px colorwheel ring makes LVGL's colour segments visibly join on this
# display. 14 px is still clearly thicker than the original while hiding the seams.
replace_once(
    ui,
    '  lv_obj_set_style_arc_width(b.control, 18, LV_PART_MAIN);',
    '  lv_obj_set_style_arc_width(b.control, 14, LV_PART_MAIN);',
    'colorwheel seam reduction',
)

print('Mira Panel 0.5.8 stable button/slider redraw and colorwheel seam fix applied')
