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


# Version only. This pass deliberately leaves Jeedom JSON parsing, widget IDs,
# HTTP, MQTT and event callbacks untouched: 0.6.6 is a visual-only refresh.
replace_once(ino, '"0.6.5"', '"0.6.6"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.5 initialise', '[MIRA] Firmware 0.6.6 initialise', 'startup log')

# Fresh installs start on the branded Mira theme. Existing saved theme choices are
# preserved because the stored "theme" key still has priority.
replace_once(
    ui,
    '  if (savedTheme == 0xFF) savedTheme = _prefs.getBool("dark", true) ? THEME_DARK : THEME_MIRA;',
    '  if (savedTheme == 0xFF) savedTheme = _prefs.getBool("dark", false) ? THEME_DARK : THEME_MIRA;',
    'fresh install Mira theme default',
)

old_palette = '''lv_color_t MiraPanelUI::colorBg() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x263840);
  if (_theme == THEME_GLASS) return lv_color_hex(0x071A28);
  return lv_color_hex(0x0C1115);
}

lv_color_t MiraPanelUI::colorPanel() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x344B55);
  if (_theme == THEME_GLASS) return lv_color_hex(0x12344A);
  return lv_color_hex(0x151C21);
}

lv_color_t MiraPanelUI::colorPanel2() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x415D68);
  if (_theme == THEME_GLASS) return lv_color_hex(0x17465F);
  return lv_color_hex(0x1F282E);
}

lv_color_t MiraPanelUI::colorBorder() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x607781);
  if (_theme == THEME_GLASS) return lv_color_hex(0x2D6A82);
  return lv_color_hex(0x2E3B43);
}

lv_color_t MiraPanelUI::colorAccent() const {
  if (_theme == THEME_GLASS) return lv_color_hex(0x56DBFF);
  return lv_color_hex(0x22BEEA);
}

lv_color_t MiraPanelUI::colorAccent2() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x18637A);
  if (_theme == THEME_GLASS) return lv_color_hex(0x1B6B89);
  return lv_color_hex(0x124E65);
}

lv_color_t MiraPanelUI::colorNav() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x12191D);
  if (_theme == THEME_GLASS) return lv_color_hex(0x06131F);
  return lv_color_hex(0x080C0F);
}
'''
new_palette = '''lv_color_t MiraPanelUI::colorBg() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x071421);
  if (_theme == THEME_GLASS) return lv_color_hex(0x071A28);
  return lv_color_hex(0x090D12);
}

lv_color_t MiraPanelUI::colorPanel() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x0E2032);
  if (_theme == THEME_GLASS) return lv_color_hex(0x103047);
  return lv_color_hex(0x141B22);
}

lv_color_t MiraPanelUI::colorPanel2() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x173149);
  if (_theme == THEME_GLASS) return lv_color_hex(0x17445D);
  return lv_color_hex(0x202A33);
}

lv_color_t MiraPanelUI::colorBorder() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x20445F);
  if (_theme == THEME_GLASS) return lv_color_hex(0x2B6882);
  return lv_color_hex(0x33414C);
}

lv_color_t MiraPanelUI::colorAccent() const {
  if (_theme == THEME_GLASS) return lv_color_hex(0x5BDEFF);
  if (_theme == THEME_MIRA) return lv_color_hex(0x2ABEFF);
  return lv_color_hex(0x31BDF2);
}

lv_color_t MiraPanelUI::colorAccent2() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x0C6FA8);
  if (_theme == THEME_GLASS) return lv_color_hex(0x176885);
  return lv_color_hex(0x155A75);
}

lv_color_t MiraPanelUI::colorNav() const {
  if (_theme == THEME_MIRA) return lv_color_hex(0x06101B);
  if (_theme == THEME_GLASS) return lv_color_hex(0x06131F);
  return lv_color_hex(0x070B0F);
}
'''
replace_once(ui, old_palette, new_palette, 'premium Mira palette')

replace_function(ui, 'void MiraPanelUI::styleMainCard(lv_obj_t* card, bool dateTime) {', r'''void MiraPanelUI::styleMainCard(lv_obj_t* card, bool dateTime) {
  if (!card) return;

  lv_obj_set_style_bg_color(card, colorPanel(), 0);
  lv_obj_set_style_bg_opa(card, _theme == THEME_GLASS ? LV_OPA_80 : LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, colorBorder(), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, _theme == THEME_GLASS ? 16 : 14, 0);

  // Keep shadows deliberately light: this is an ESP32-S3 touch UI, so the
  // premium look must not trade away redraw speed.
  lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(card, _theme == THEME_MIRA ? LV_OPA_20 : LV_OPA_10, 0);
  lv_obj_set_style_shadow_width(card, _theme == THEME_MIRA ? 5 : 3, 0);
  lv_obj_set_style_shadow_ofs_y(card, 2, 0);

  if (dateTime) {
    lv_obj_set_style_border_color(card, colorAccent2(), 0);
  }
}''', 'premium card engine')

# Pages keep exactly the same flex/scroll architecture and 320x434 widget area.
# Only spacing is tightened to fit the now-visible cards cleanly.
replace_once(
    ui,
    '    lv_obj_set_style_pad_all(_pages[i], 7, 0);\n    lv_obj_set_style_pad_row(_pages[i], 5, 0);',
    '    lv_obj_set_style_pad_all(_pages[i], 6, 0);\n    lv_obj_set_style_pad_row(_pages[i], 6, 0);',
    'page card spacing',
)

# Bottom navigation: same four destinations and same current-page-hidden logic,
# just a cleaner floating-touch treatment.
replace_once(
    ui,
    '''  lv_obj_set_style_border_width(_nav, 0, 0);
  lv_obj_set_style_radius(_nav, 0, 0);
  lv_obj_set_style_pad_all(_nav, 0, 0);''',
    '''  lv_obj_set_style_border_width(_nav, 1, 0);
  lv_obj_set_style_border_side(_nav, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(_nav, colorBorder(), 0);
  lv_obj_set_style_radius(_nav, 0, 0);
  lv_obj_set_style_pad_all(_nav, 0, 0);''',
    'navigation top separator',
)
replace_once(
    ui,
    '''    lv_obj_set_style_bg_opa(_navButtons[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(_navButtons[i], LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(_navButtons[i], colorAccent(), LV_STATE_PRESSED);
    lv_obj_set_style_radius(_navButtons[i], 0, 0);''',
    '''    lv_obj_set_style_bg_opa(_navButtons[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(_navButtons[i], LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(_navButtons[i], colorAccent(), LV_STATE_PRESSED);
    lv_obj_set_style_radius(_navButtons[i], 10, 0);''',
    'navigation touch tiles',
)

# Buttons: no change to callbacks/IDs; only the LVGL surface changes.
replace_once(
    ui,
    '''  lv_obj_set_style_bg_color(b.control, colorAccent2(), 0);
  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xF2A23A), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b.control, lv_color_hex(0xFFC56B), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b.control, 2, LV_STATE_PRESSED);
  lv_obj_set_style_radius(b.control, compact ? 10 : 9, 0);''',
    '''  lv_obj_set_style_bg_color(b.control, colorAccent2(), 0);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b.control, colorBorder(), 0);
  lv_obj_set_style_border_width(b.control, 1, 0);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b.control, lv_color_hex(0x7DEEFF), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b.control, 1, LV_STATE_PRESSED);
  lv_obj_set_style_radius(b.control, 12, 0);
  lv_obj_set_style_shadow_color(b.control, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(b.control, LV_OPA_20, 0);
  lv_obj_set_style_shadow_width(b.control, 4, 0);
  lv_obj_set_style_shadow_ofs_y(b.control, 2, 0);''',
    'premium action button',
)

# Toggle track/knob: retain the same switch and event callback.
replace_once(
    ui,
    '''  lv_obj_set_style_bg_color(b.control, lv_color_hex(0x55616A), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xE6ECEF), LV_PART_KNOB);''',
    '''  lv_obj_set_style_bg_color(b.control, colorPanel2(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xF0F8FC), LV_PART_KNOB);
  lv_obj_set_style_border_color(b.control, colorBorder(), LV_PART_MAIN);
  lv_obj_set_style_border_width(b.control, 1, LV_PART_MAIN);''',
    'premium toggle',
)

# Linear slider: cyan fill + light knob with cyan focus ring. Touch area and
# slider range remain unchanged.
replace_once(
    ui,
    '''  lv_obj_set_style_bg_color(b.control, colorAccent2(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_KNOB);
  lv_obj_set_style_border_color(b.control, lv_color_hex(0x7DEEFF), LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b.control, 2, LV_PART_KNOB | LV_STATE_PRESSED);''',
    '''  lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(b.control, lv_color_hex(0xEAF7FF), LV_PART_KNOB);
  lv_obj_set_style_border_color(b.control, colorAccent(), LV_PART_KNOB);
  lv_obj_set_style_border_width(b.control, 2, LV_PART_KNOB);
  lv_obj_set_style_shadow_color(b.control, colorAccent(), LV_PART_KNOB);
  lv_obj_set_style_shadow_opa(b.control, LV_OPA_20, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(b.control, 5, LV_PART_KNOB);
  lv_obj_set_style_border_color(b.control, lv_color_hex(0x7DEEFF), LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b.control, 3, LV_PART_KNOB | LV_STATE_PRESSED);''',
    'premium slider knob',
)

# Thermostat dial uses the existing arc/value and exact same command path.
replace_once(ui, '  lv_obj_set_style_bg_color(dialPlate, lv_color_hex(0x171D24), 0);', '  lv_obj_set_style_bg_color(dialPlate, colorPanel2(), 0);', 'thermostat plate')
replace_once(ui, '  lv_obj_set_style_arc_color(b.control, lv_color_hex(0x1B2229), LV_PART_MAIN);', '  lv_obj_set_style_arc_color(b.control, colorPanel2(), LV_PART_MAIN);', 'thermostat inactive arc')
replace_once(ui, '  lv_obj_set_style_bg_color(inner, lv_color_hex(0x19212A), 0);', '  lv_obj_set_style_bg_color(inner, colorBg(), 0);', 'thermostat inner disc')

# Standard list/dropdown no longer floats invisibly: give it the same touch-card
# vocabulary as the rest of Mira. Command order and selected index are untouched.
replace_once(
    ui,
    '''    lv_obj_set_style_bg_opa(b.control, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b.control, 0, 0);
    lv_obj_set_style_radius(b.control, 0, 0);''',
    '''    lv_obj_set_style_bg_color(b.control, colorPanel2(), 0);
    lv_obj_set_style_bg_opa(b.control, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b.control, colorBorder(), 0);
    lv_obj_set_style_border_width(b.control, 1, 0);
    lv_obj_set_style_radius(b.control, 10, 0);
    lv_obj_set_style_pad_left(b.control, 10, 0);
    lv_obj_set_style_pad_right(b.control, 10, 0);''',
    'premium dropdown',
)

# Remove the old warm/orange press feedback from segmented/list buttons. The new
# visual language uses Mira cyan consistently.
text = ui.read_text(encoding='utf-8')
old_press = 'lv_obj_set_style_bg_color(btn, lv_color_hex(0xF2A23A), LV_STATE_PRESSED);'
count = text.count(old_press)
if count != 2:
    raise SystemExit(f'list cyan press feedback: expected 2 matches in {ui}, got {count}')
ui.write_text(text.replace(old_press, 'lv_obj_set_style_bg_color(btn, colorAccent(), LV_STATE_PRESSED);'), encoding='utf-8')

# Theme reapplication must restore every premium state after a hot widget reload
# or theme change. No data or event logic is changed here.
replace_function(ui, 'void MiraPanelUI::applyTheme() {', r'''void MiraPanelUI::applyTheme() {
  if (!_screen) return;

  const lv_color_t grad = (_theme == THEME_MIRA) ? lv_color_hex(0x0A2134) :
                          (_theme == THEME_GLASS) ? lv_color_hex(0x0B293D) : colorBg();

  lv_obj_set_style_bg_color(_screen, colorBg(), 0);
  lv_obj_set_style_bg_grad_color(_screen, grad, 0);
  lv_obj_set_style_bg_grad_dir(_screen, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

  if (_nav) {
    lv_obj_set_style_bg_color(_nav, colorNav(), 0);
    lv_obj_set_style_bg_opa(_nav, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_nav, colorBorder(), 0);
  }

  for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
    if (_pages[i]) {
      lv_obj_set_style_bg_color(_pages[i], colorBg(), 0);
      lv_obj_set_style_bg_grad_color(_pages[i], grad, 0);
      lv_obj_set_style_bg_grad_dir(_pages[i], LV_GRAD_DIR_VER, 0);
    }
    if (_navButtons[i]) {
      lv_obj_set_style_bg_color(_navButtons[i], colorAccent(), LV_STATE_PRESSED);
    }
  }

  for (uint8_t i = 0; i < _bindingCount; ++i) {
    Binding& b = _bindings[i];
    if (b.card) styleMainCard(b.card, b.type == W_DATETIME);

    if (b.type == W_INFO && b.valueLabel) {
      lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    }
    else if (b.type == W_BUTTON && b.control) {
      lv_obj_set_style_bg_color(b.control, colorAccent2(), 0);
      lv_obj_set_style_border_color(b.control, colorBorder(), 0);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);
    }
    else if (b.type == W_TOGGLE && b.control) {
      lv_obj_set_style_bg_color(b.control, colorPanel2(), LV_PART_MAIN);
      lv_obj_set_style_border_color(b.control, colorBorder(), LV_PART_MAIN);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
      lv_obj_set_style_bg_color(b.control, lv_color_hex(0xF0F8FC), LV_PART_KNOB);
    }
    else if (b.type == W_RANGE && b.control) {
      lv_obj_set_style_bg_color(b.control, colorPanel2(), LV_PART_MAIN);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(b.control, lv_color_hex(0xEAF7FF), LV_PART_KNOB);
      lv_obj_set_style_border_color(b.control, colorAccent(), LV_PART_KNOB);
      if (b.valueLabel) lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    }
    else if (b.type == W_ROUNDRANGE && b.control) {
      lv_obj_set_style_arc_color(b.control, colorPanel2(), LV_PART_MAIN);
      lv_obj_set_style_arc_color(b.control, colorAccent(), LV_PART_INDICATOR);
    }
    else if (b.type == W_LIST && b.control) {
      lv_obj_set_style_bg_color(b.control, colorPanel2(), 0);
      lv_obj_set_style_border_color(b.control, colorBorder(), 0);
    }
    else if (b.type == W_LISTBUTTON) {
      updateListVisual(b);
    }
  }

  for (uint8_t i = 0; i < 4; ++i) {
    if (_settingsCards[i]) {
      lv_obj_set_style_bg_color(_settingsCards[i], colorPanel(), 0);
      lv_obj_set_style_bg_opa(_settingsCards[i], _theme == THEME_GLASS ? LV_OPA_80 : LV_OPA_COVER, 0);
      lv_obj_set_style_border_color(_settingsCards[i], colorBorder(), 0);
      lv_obj_set_style_radius(_settingsCards[i], _theme == THEME_GLASS ? 16 : 14, 0);
    }
  }

  if (_brightnessSlider) {
    lv_obj_set_style_bg_color(_brightnessSlider, colorPanel2(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_brightnessSlider, colorAccent(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_brightnessSlider, lv_color_hex(0xEAF7FF), LV_PART_KNOB);
    lv_obj_set_style_border_color(_brightnessSlider, colorAccent(), LV_PART_KNOB);
  }
  if (_brightnessValue) lv_obj_set_style_text_color(_brightnessValue, colorAccent(), 0);
  if (_themeDropdown) {
    lv_obj_set_style_bg_color(_themeDropdown, colorPanel2(), 0);
    lv_obj_set_style_border_color(_themeDropdown, colorBorder(), 0);
  }

  setNavActive(_currentPage);
}''', 'premium applyTheme')

print('Mira Panel 0.6.6 premium touch theme applied; widget integration unchanged')
