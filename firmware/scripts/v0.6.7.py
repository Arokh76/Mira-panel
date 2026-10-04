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


# Version only. Like 0.6.6, this pass is visual-only: no widget schema,
# Jeedom IDs, HTTP, MQTT or event/callback behavior is modified.
replace_once(ino, '"0.6.6"', '"0.6.7"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.6 initialise', '[MIRA] Firmware 0.6.7 initialise', 'startup log')

# Real-panel tuning from the first hardware photos: the cyan outlines were a bit
# too dominant and the action button was too luminous compared with the cards.
replace_once(
    ui,
    '  if (_theme == THEME_MIRA) return lv_color_hex(0x20445F);',
    '  if (_theme == THEME_MIRA) return lv_color_hex(0x17354B);',
    'softer Mira border',
)
replace_once(
    ui,
    '  if (_theme == THEME_MIRA) return lv_color_hex(0x0C6FA8);',
    '  if (_theme == THEME_MIRA) return lv_color_hex(0x0A5C88);',
    'softer Mira secondary accent',
)

# Cards: quieter outlines for ordinary widgets, while date/time gets a subtle
# hierarchy lift without changing its dimensions or widget placement.
replace_function(ui, 'void MiraPanelUI::styleMainCard(lv_obj_t* card, bool dateTime) {', r'''void MiraPanelUI::styleMainCard(lv_obj_t* card, bool dateTime) {
  if (!card) return;

  lv_obj_set_style_bg_color(card, colorPanel(), 0);
  lv_obj_set_style_bg_grad_color(card, colorPanel(), 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_opa(card, _theme == THEME_GLASS ? LV_OPA_80 : LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, colorBorder(), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_opa(card, _theme == THEME_MIRA ? LV_OPA_50 : LV_OPA_70, 0);
  lv_obj_set_style_radius(card, _theme == THEME_GLASS ? 16 : 14, 0);

  lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(card, _theme == THEME_MIRA ? LV_OPA_10 : LV_OPA_10, 0);
  lv_obj_set_style_shadow_width(card, _theme == THEME_MIRA ? 4 : 3, 0);
  lv_obj_set_style_shadow_ofs_y(card, 2, 0);

  if (dateTime) {
    lv_obj_set_style_border_color(card, colorAccent2(), 0);
    lv_obj_set_style_border_opa(card, _theme == THEME_MIRA ? LV_OPA_70 : LV_OPA_COVER, 0);
    if (_theme == THEME_MIRA) {
      lv_obj_set_style_bg_color(card, lv_color_hex(0x10283D), 0);
      lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x0C1F31), 0);
      lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    }
  }
}''', 'hardware-tuned card engine')

# Action button: on the physical display the solid cyan block was too bright.
# Use the same dark raised surface as the other controls, with a cyan border and
# cyan press feedback. Callback and command behavior remain untouched.
replace_once(
    ui,
    '''  lv_obj_set_style_bg_color(b.control, colorAccent2(), 0);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b.control, colorBorder(), 0);
  lv_obj_set_style_border_width(b.control, 1, 0);''',
    '''  lv_obj_set_style_bg_color(b.control, colorPanel2(), 0);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b.control, colorAccent2(), 0);
  lv_obj_set_style_border_opa(b.control, LV_OPA_70, 0);
  lv_obj_set_style_border_width(b.control, 1, 0);''',
    'calmer action button',
)

# Theme refresh must reproduce the same button/card treatment after a hot reload.
replace_once(
    ui,
    '''    else if (b.type == W_BUTTON && b.control) {
      lv_obj_set_style_bg_color(b.control, colorAccent2(), 0);
      lv_obj_set_style_border_color(b.control, colorBorder(), 0);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);
    }''',
    '''    else if (b.type == W_BUTTON && b.control) {
      lv_obj_set_style_bg_color(b.control, colorPanel2(), 0);
      lv_obj_set_style_border_color(b.control, colorAccent2(), 0);
      lv_obj_set_style_border_opa(b.control, LV_OPA_70, 0);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);
    }''',
    'theme action button refresh',
)

# Settings cards and the bottom separator should use the softened border too.
replace_once(
    ui,
    '      lv_obj_set_style_border_color(_settingsCards[i], colorBorder(), 0);\n      lv_obj_set_style_radius(_settingsCards[i], _theme == THEME_GLASS ? 16 : 14, 0);',
    '      lv_obj_set_style_border_color(_settingsCards[i], colorBorder(), 0);\n      lv_obj_set_style_border_opa(_settingsCards[i], _theme == THEME_MIRA ? LV_OPA_50 : LV_OPA_70, 0);\n      lv_obj_set_style_radius(_settingsCards[i], _theme == THEME_GLASS ? 16 : 14, 0);',
    'settings border softness',
)
replace_once(
    ui,
    '    lv_obj_set_style_border_color(_nav, colorBorder(), 0);',
    '    lv_obj_set_style_border_color(_nav, colorBorder(), 0);\n    lv_obj_set_style_border_opa(_nav, _theme == THEME_MIRA ? LV_OPA_50 : LV_OPA_70, 0);',
    'nav separator softness',
)

print('Mira Panel 0.6.7 hardware photo polish applied; widget integration unchanged')
