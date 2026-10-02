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

# Version 0.5.0
replace_once(ino, '"0.4.11"', '"0.5.0"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.4.11 initialise', '[MIRA] Firmware 0.5.0 initialise', 'startup log')

# Header: three themes instead of a boolean dark mode.
replace_once(
    hdr,
    '  static constexpr uint8_t PAGE_COUNT = 4;\n\n  enum WidgetType : uint8_t {',
    '  static constexpr uint8_t PAGE_COUNT = 4;\n\n  enum ThemeMode : uint8_t {\n    THEME_MIRA = 0,\n    THEME_DARK = 1,\n    THEME_GLASS = 2\n  };\n\n  enum WidgetType : uint8_t {',
    'theme enum',
)
replace_once(hdr, '  lv_obj_t* _darkSwitch = nullptr;', '  lv_obj_t* _themeDropdown = nullptr;', 'theme dropdown member')
replace_once(hdr, '  bool _darkMode = true;', '  uint8_t _theme = THEME_DARK;', 'theme state member')
replace_once(hdr, '  static void onDarkMode(lv_event_t* e);', '  static void onTheme(lv_event_t* e);', 'theme callback declaration')
replace_once(
    hdr,
    '  lv_color_t colorBg() const;\n  lv_color_t colorPanel() const;\n  lv_color_t colorPanel2() const;\n  lv_color_t colorBorder() const;',
    '  lv_color_t colorBg() const;\n  lv_color_t colorPanel() const;\n  lv_color_t colorPanel2() const;\n  lv_color_t colorBorder() const;\n  lv_color_t colorAccent() const;\n  lv_color_t colorAccent2() const;\n  lv_color_t colorNav() const;\n  void styleMainCard(lv_obj_t* card, bool dateTime = false);',
    'theme helpers declaration',
)

# Theme palettes. Classic stays close to the current Mira look, Dark is deeper,
# Glass uses a navy/cyan palette with translucent cards (LVGL has no true blur).
old_colors = '''lv_color_t MiraPanelUI::colorBg() const {
  return _darkMode ? lv_color_hex(0x101417) : lv_color_hex(0x263238);
}

lv_color_t MiraPanelUI::colorPanel() const {
  return _darkMode ? lv_color_hex(0x1B2024) : lv_color_hex(0x34444B);
}

lv_color_t MiraPanelUI::colorPanel2() const {
  return _darkMode ? lv_color_hex(0x252C31) : lv_color_hex(0x41545C);
}

lv_color_t MiraPanelUI::colorBorder() const {
  return _darkMode ? lv_color_hex(0x30383D) : lv_color_hex(0x60727B);
}
'''
new_colors = '''lv_color_t MiraPanelUI::colorBg() const {
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

void MiraPanelUI::styleMainCard(lv_obj_t* card, bool dateTime) {
  if (!card) return;

  if (_theme == THEME_MIRA && !dateTime) {
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    return;
  }

  lv_obj_set_style_bg_color(card, colorPanel(), 0);
  lv_obj_set_style_bg_opa(card, _theme == THEME_GLASS ? LV_OPA_70 : LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, colorBorder(), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, _theme == THEME_GLASS ? 16 : 12, 0);
  lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(card, _theme == THEME_GLASS ? LV_OPA_20 : LV_OPA_10, 0);
  lv_obj_set_style_shadow_width(card, _theme == THEME_GLASS ? 12 : 6, 0);
  lv_obj_set_style_shadow_ofs_y(card, 2, 0);
}
'''
replace_once(ui, old_colors, new_colors, 'theme palettes')

# Load theme preference; migrate existing dark setting the first time.
replace_once(
    ui,
    '  _brightness = _prefs.getUChar("bright", 220);\n  _darkMode = _prefs.getBool("dark", true);',
    '  _brightness = _prefs.getUChar("bright", 220);\n  uint8_t savedTheme = _prefs.getUChar("theme", 0xFF);\n  if (savedTheme == 0xFF) savedTheme = _prefs.getBool("dark", true) ? THEME_DARK : THEME_MIRA;\n  _theme = savedTheme <= THEME_GLASS ? savedTheme : THEME_DARK;',
    'theme preference migration',
)

# Accent colors need to follow the selected theme.
for old, new, label in [
    ('lv_obj_set_style_bg_color(_navButtons[i], COL_ACCENT, LV_STATE_PRESSED);', 'lv_obj_set_style_bg_color(_navButtons[i], colorAccent(), LV_STATE_PRESSED);', 'nav accent'),
    ('lv_obj_set_style_bg_color(b.control, COL_ACCENT2, 0);', 'lv_obj_set_style_bg_color(b.control, colorAccent2(), 0);', 'button normal accent'),
    ('lv_obj_set_style_bg_color(b.control, COL_ACCENT, LV_STATE_PRESSED);', 'lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);', 'button pressed accent'),
    ('lv_obj_set_style_bg_color(b.control, COL_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);', 'lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR | LV_STATE_CHECKED);', 'toggle accent'),
    ('lv_obj_set_style_bg_color(b.control, COL_ACCENT2, LV_PART_INDICATOR);', 'lv_obj_set_style_bg_color(b.control, colorAccent2(), LV_PART_INDICATOR);', 'range indicator'),
    ('lv_obj_set_style_bg_color(b.control, COL_ACCENT, LV_PART_KNOB);', 'lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_KNOB);', 'range knob'),
]:
    text = ui.read_text(encoding='utf-8')
    if old not in text:
        raise SystemExit(f'{label}: no match in {ui}')
    ui.write_text(text.replace(old, new), encoding='utf-8')

# Value labels in ordinary widgets use the theme accent.
text = ui.read_text(encoding='utf-8')
text = text.replace('lv_obj_set_style_text_color(b.valueLabel, COL_ACCENT, 0);', 'lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);')
ui.write_text(text, encoding='utf-8')

# Main card style becomes theme-aware.
replace_once(
    ui,
    '''  lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 9, 0);''',
    '''  styleMainCard(card, false);
  lv_obj_set_style_pad_all(card, 9, 0);''',
    'theme-aware main cards',
)

# Date/time uses the same theme card engine.
replace_once(
    ui,
    '''  lv_obj_set_style_bg_color(b.card, colorPanel(), 0);
  lv_obj_set_style_bg_opa(b.card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b.card, colorBorder(), 0);
  lv_obj_set_style_border_width(b.card, 1, 0);
  lv_obj_set_style_radius(b.card, 8, 0);
  lv_obj_set_style_pad_all(b.card, 0, 0);''',
    '''  styleMainCard(b.card, true);
  lv_obj_set_style_pad_all(b.card, 0, 0);''',
    'theme-aware datetime card',
)

# Settings: replace Mode sombre switch with a three-choice theme dropdown.
replace_once(
    ui,
    '  _settingsCards[1] = makeSettingsCard(122);',
    '  _settingsCards[1] = makeSettingsCard(130);',
    'display settings card height',
)
old_dark_block = '''  lv_obj_t* darkRow = makeTransparentRow(_settingsCards[1], 28);
  lv_obj_t* darkLbl = lv_label_create(darkRow);
  lv_label_set_text(darkLbl, "Mode sombre");
  lv_obj_set_style_text_color(darkLbl, COL_TEXT, 0);
  lv_obj_set_style_text_font(darkLbl, &mira_font_fr_12, 0);
  _darkSwitch = lv_switch_create(darkRow);
  lv_obj_set_size(_darkSwitch, 48, 25);
  if (_darkMode) lv_obj_add_state(_darkSwitch, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(_darkSwitch, COL_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(_darkSwitch, onDarkMode, LV_EVENT_VALUE_CHANGED, this);
'''
new_theme_block = '''  lv_obj_t* themeRow = makeTransparentRow(_settingsCards[1], 36);
  lv_obj_t* themeLbl = lv_label_create(themeRow);
  lv_label_set_text(themeLbl, "Thème");
  lv_obj_set_style_text_color(themeLbl, COL_TEXT, 0);
  lv_obj_set_style_text_font(themeLbl, &mira_font_fr_12, 0);
  _themeDropdown = lv_dropdown_create(themeRow);
  lv_obj_set_width(_themeDropdown, 126);
  lv_dropdown_set_options(_themeDropdown, "Mira\\nSombre\\nGlass");
  lv_dropdown_set_selected(_themeDropdown, _theme);
  lv_obj_set_style_text_font(_themeDropdown, &mira_font_fr_12, 0);
  lv_obj_set_style_text_color(_themeDropdown, COL_TEXT, 0);
  lv_obj_set_style_bg_color(_themeDropdown, colorPanel2(), 0);
  lv_obj_set_style_border_color(_themeDropdown, colorBorder(), 0);
  lv_obj_set_style_border_width(_themeDropdown, 1, 0);
  lv_obj_set_style_radius(_themeDropdown, 9, 0);
  lv_obj_add_event_cb(_themeDropdown, onTheme, LV_EVENT_VALUE_CHANGED, this);
'''
replace_once(ui, old_dark_block, new_theme_block, 'theme selector UI')

# Callback for theme selection.
old_cb = '''void MiraPanelUI::onDarkMode(lv_event_t* e) {
  MiraPanelUI* ui = static_cast<MiraPanelUI*>(lv_event_get_user_data(e));
  if (!ui || ui->_suppressEvents) return;
  ui->_darkMode = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  ui->_prefs.putBool("dark", ui->_darkMode);
  ui->applyTheme();
  ui->noteActivity();
}
'''
new_cb = '''void MiraPanelUI::onTheme(lv_event_t* e) {
  MiraPanelUI* ui = static_cast<MiraPanelUI*>(lv_event_get_user_data(e));
  if (!ui || ui->_suppressEvents) return;
  uint16_t selected = lv_dropdown_get_selected(lv_event_get_target(e));
  if (selected > THEME_GLASS) selected = THEME_MIRA;
  ui->_theme = (uint8_t)selected;
  ui->_prefs.putUChar("theme", ui->_theme);
  ui->applyTheme();
  ui->noteActivity();
}
'''
replace_once(ui, old_cb, new_cb, 'theme callback')

# List buttons also follow the theme accent.
replace_once(
    ui,
    'lv_obj_set_style_bg_color(b.listButtons[i], i == (uint8_t)b.selectedIndex ? COL_ACCENT2 : colorPanel2(), 0);',
    'lv_obj_set_style_bg_color(b.listButtons[i], i == (uint8_t)b.selectedIndex ? colorAccent2() : colorPanel2(), 0);',
    'list buttons accent',
)

# Apply theme to every live object, so changing the dropdown is immediate.
old_apply = '''void MiraPanelUI::applyTheme() {
  if (!_screen) return;

  lv_obj_set_style_bg_color(_screen, colorBg(), 0);
  lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
  if (_nav) {
    lv_obj_set_style_bg_color(_nav, lv_color_hex(0x101419), 0);
    lv_obj_set_style_bg_opa(_nav, LV_OPA_COVER, 0);
  }

  for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
    if (_pages[i]) lv_obj_set_style_bg_color(_pages[i], colorBg(), 0);
  }

  for (uint8_t i = 0; i < _bindingCount; ++i) {
    if (_bindings[i].card) {
      if (_bindings[i].type == W_DATETIME) {
        lv_obj_set_style_bg_color(_bindings[i].card, colorPanel(), 0);
        lv_obj_set_style_bg_opa(_bindings[i].card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(_bindings[i].card, colorBorder(), 0);
        lv_obj_set_style_border_width(_bindings[i].card, 1, 0);
        lv_obj_set_style_radius(_bindings[i].card, 8, 0);
      } else {
        lv_obj_set_style_bg_opa(_bindings[i].card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(_bindings[i].card, 0, 0);
      }
    }
    if (_bindings[i].type == W_ROUNDRANGE && _bindings[i].control) {
      lv_obj_set_style_arc_color(_bindings[i].control, colorPanel2(), LV_PART_MAIN);
    }
  }

  for (uint8_t i = 0; i < 4; ++i) {
    if (_settingsCards[i]) {
      lv_obj_set_style_bg_color(_settingsCards[i], colorPanel(), 0);
      lv_obj_set_style_border_color(_settingsCards[i], colorBorder(), 0);
    }
  }

  setNavActive(_currentPage);
}
'''
new_apply = '''void MiraPanelUI::applyTheme() {
  if (!_screen) return;

  lv_obj_set_style_bg_color(_screen, colorBg(), 0);
  lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
  if (_nav) {
    lv_obj_set_style_bg_color(_nav, colorNav(), 0);
    lv_obj_set_style_bg_opa(_nav, LV_OPA_COVER, 0);
  }

  for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
    if (_pages[i]) lv_obj_set_style_bg_color(_pages[i], colorBg(), 0);
    if (_navButtons[i]) lv_obj_set_style_bg_color(_navButtons[i], colorAccent(), LV_STATE_PRESSED);
  }

  for (uint8_t i = 0; i < _bindingCount; ++i) {
    Binding& b = _bindings[i];
    if (b.card) styleMainCard(b.card, b.type == W_DATETIME);

    if (b.type == W_INFO && b.valueLabel) {
      lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    }
    else if (b.type == W_BUTTON && b.control) {
      lv_obj_set_style_bg_color(b.control, colorAccent2(), 0);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_STATE_PRESSED);
    }
    else if (b.type == W_TOGGLE && b.control) {
      lv_obj_set_style_bg_color(b.control, colorPanel2(), LV_PART_MAIN);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    }
    else if (b.type == W_RANGE && b.control) {
      lv_obj_set_style_bg_color(b.control, colorAccent2(), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(b.control, colorAccent(), LV_PART_KNOB);
      if (b.valueLabel) lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    }
    else if (b.type == W_ROUNDRANGE && b.control) {
      lv_obj_set_style_arc_color(b.control, colorPanel2(), LV_PART_MAIN);
      lv_obj_set_style_arc_color(b.control, colorAccent(), LV_PART_INDICATOR);
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
      lv_obj_set_style_radius(_settingsCards[i], _theme == THEME_GLASS ? 15 : 11, 0);
    }
  }

  if (_brightnessSlider) {
    lv_obj_set_style_bg_color(_brightnessSlider, colorAccent2(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_brightnessSlider, colorAccent(), LV_PART_KNOB);
  }
  if (_brightnessValue) lv_obj_set_style_text_color(_brightnessValue, colorAccent(), 0);
  if (_themeDropdown) {
    lv_obj_set_style_bg_color(_themeDropdown, colorPanel2(), 0);
    lv_obj_set_style_border_color(_themeDropdown, colorBorder(), 0);
  }

  setNavActive(_currentPage);
}
'''
replace_once(ui, old_apply, new_apply, 'live theme application')

# Round-range indicator adopts theme accent rather than a hard-coded blue.
replace_once(ui, 'lv_obj_set_style_arc_color(b.control, lv_color_hex(0x1478FF), LV_PART_INDICATOR);', 'lv_obj_set_style_arc_color(b.control, colorAccent(), LV_PART_INDICATOR);', 'roundrange accent')

# Settings headings and sliders get the current accent at creation too.
text = ui.read_text(encoding='utf-8')
text = text.replace('lv_obj_set_style_text_color(h1, COL_ACCENT, 0);', 'lv_obj_set_style_text_color(h1, colorAccent(), 0);')
text = text.replace('lv_obj_set_style_text_color(h2, COL_ACCENT, 0);', 'lv_obj_set_style_text_color(h2, colorAccent(), 0);')
text = text.replace('lv_obj_set_style_text_color(h3, COL_ACCENT, 0);', 'lv_obj_set_style_text_color(h3, colorAccent(), 0);')
text = text.replace('lv_obj_set_style_text_color(_brightnessValue, COL_ACCENT, 0);', 'lv_obj_set_style_text_color(_brightnessValue, colorAccent(), 0);')
ui.write_text(text, encoding='utf-8')

# Sanity checks: the old dark switch must be fully gone.
combined = hdr.read_text(encoding='utf-8') + ui.read_text(encoding='utf-8')
if '_darkSwitch' in combined or 'onDarkMode' in combined or '_darkMode' in combined:
    raise SystemExit('legacy dark-mode symbols still present after theme migration')
if 'Mira\\nSombre\\nGlass' not in ui.read_text(encoding='utf-8'):
    raise SystemExit('theme selector options missing')

print('Mira Panel 0.5.0 theme engine applied: Mira / Sombre / Glass')
