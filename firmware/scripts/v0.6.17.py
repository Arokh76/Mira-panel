from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'
hdr = root / 'MiraPanelUI.h'
spiffs = root / 'SPIFFS_Process.h'
server = root / 'MiraPanelServer.cpp'


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


# 0.6.17 introduces a separate, optional presentation layout field.
# Existing JSON without "layout" remains 100% compatible and keeps automatic layout.
replace_once(ino, '"0.6.16"', '"0.6.17"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.16 initialise', '[MIRA] Firmware 0.6.17 initialise', 'startup log')

# Persist layout independently from widget style. Valid values: auto / tile / wide / hero.
replace_once(
    spiffs,
    'String LVGL_Style = "standard";\n',
    'String LVGL_Style = "standard";\nString LVGL_Layout = "auto";\n',
    'LVGL layout storage',
)
replace_once(
    spiffs,
    '  lvgl_array["style"] = LVGL_Style.length() ? LVGL_Style : "standard";\n',
    '  lvgl_array["style"] = LVGL_Style.length() ? LVGL_Style : "standard";\n  lvgl_array["layout"] = LVGL_Layout.length() ? LVGL_Layout : "auto";\n',
    'LVGL layout json',
)

# Both programming endpoints accept the optional layout query argument.
old_style_block = '''                    LVGL_Style = request->hasParam("style") ? request->getParam("style")->value() : "standard";
                    LVGL_Style.trim();
                    LVGL_Style.toLowerCase();
                    if (!LVGL_Style.length()) LVGL_Style = "standard";
                    AddListLVGL();'''
new_style_block = '''                    LVGL_Style = request->hasParam("style") ? request->getParam("style")->value() : "standard";
                    LVGL_Style.trim();
                    LVGL_Style.toLowerCase();
                    if (!LVGL_Style.length()) LVGL_Style = "standard";
                    LVGL_Layout = request->hasParam("layout") ? request->getParam("layout")->value() : "auto";
                    LVGL_Layout.trim();
                    LVGL_Layout.toLowerCase();
                    if (LVGL_Layout != "tile" && LVGL_Layout != "wide" && LVGL_Layout != "hero") LVGL_Layout = "auto";
                    AddListLVGL();'''
replace_all_exact(server, old_style_block, new_style_block, 2, 'layout query support')

# Binding remembers the optional layout override independently from the existing style.
replace_once(
    hdr,
    '    String style = "standard";\n',
    '    String style = "standard";\n    String layout = "auto";\n',
    'binding layout member',
)

# Count only AUTO home info widgets for the automatic 2-column/odd-wide algorithm.
replace_once(
    ui,
    '''    String style = obj["style"] | "standard";
    type.trim();
    type.toLowerCase();
    style.trim();
    style.toLowerCase();
    if (!style.length()) style = "standard";
    if (type == "info" && pageIndex(location) == 0 && style == "standard") {
      ++_homeStandardInfoTotal;
    }''',
    '''    String style = obj["style"] | "standard";
    String layout = obj["layout"] | "auto";
    type.trim();
    type.toLowerCase();
    style.trim();
    style.toLowerCase();
    if (!style.length()) style = "standard";
    layout.trim();
    layout.toLowerCase();
    if (layout != "tile" && layout != "wide" && layout != "hero") layout = "auto";
    if (type == "info" && pageIndex(location) == 0 && style == "standard" && layout == "auto") {
      ++_homeStandardInfoTotal;
    }''',
    'auto dashboard count',
)

# Read and normalize layout for each binding.
replace_once(
    ui,
    '''  String widgetStyle = obj["style"] | "standard";
  widgetStyle.trim();
  widgetStyle.toLowerCase();
  if (!widgetStyle.length()) widgetStyle = "standard";

  uint8_t idx = addBinding();''',
    '''  String widgetStyle = obj["style"] | "standard";
  widgetStyle.trim();
  widgetStyle.toLowerCase();
  if (!widgetStyle.length()) widgetStyle = "standard";
  String widgetLayout = obj["layout"] | "auto";
  widgetLayout.trim();
  widgetLayout.toLowerCase();
  if (widgetLayout != "tile" && widgetLayout != "wide" && widgetLayout != "hero") widgetLayout = "auto";

  uint8_t idx = addBinding();''',
    'read widget layout',
)
replace_once(
    ui,
    '  b.style = widgetStyle;\n',
    '  b.style = widgetStyle;\n  b.layout = widgetLayout;\n',
    'assign widget layout',
)

# Info widgets are the first widgets where an explicit layout really matters.
# AUTO preserves the validated 0.6.16 behavior. TILE/WIDE/HERO are optional overrides.
replace_function(ui, 'void MiraPanelUI::createInfo(uint8_t idx) {', r'''void MiraPanelUI::createInfo(uint8_t idx) {
  Binding& b = _bindings[idx];
  const bool compact = b.style == "compact";
  const bool valueDominant = b.style == "value";
  const uint8_t page = pageIndex(b.location);
  const bool autoDashboardTile = page == 0 && b.style == "standard" && b.layout == "auto";
  const bool forcedTile = page == 0 && b.style == "standard" && b.layout == "tile";
  const bool forcedWide = b.layout == "wide";
  const bool forcedHero = b.layout == "hero";

  if (forcedWide) {
    b.card = makeCard(page, 56);
    lv_obj_set_width(b.card, 304);
    lv_obj_fade_in(b.card, 140, 0);
    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_left(b.card, 16, 0);
    lv_obj_set_style_pad_right(b.card, 16, 0);
    lv_obj_set_style_pad_top(b.card, 0, 0);
    lv_obj_set_style_pad_bottom(b.card, 0, 0);
    lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* name = lv_label_create(b.card);
    lv_label_set_text(name, b.name.c_str());
    lv_obj_set_width(name, 150);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(name, COL_MUTED, 0);
    lv_obj_set_style_text_font(name, &mira_font_fr_14, 0);

    b.valueLabel = lv_label_create(b.card);
    String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
    lv_label_set_text(b.valueLabel, txt.c_str());
    lv_obj_set_width(b.valueLabel, 116);
    lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_20, 0);
    return;
  }

  if (autoDashboardTile) {
    ++_homeStandardInfoCreated;
    const bool lastOddTile = (_homeStandardInfoTotal & 1U) &&
                             (_homeStandardInfoCreated == _homeStandardInfoTotal);

    if (lastOddTile) {
      b.card = makeCard(page, 56);
      lv_obj_set_width(b.card, 304);
      lv_obj_fade_in(b.card, 140, 0);
      lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_pad_left(b.card, 16, 0);
      lv_obj_set_style_pad_right(b.card, 16, 0);
      lv_obj_set_style_pad_top(b.card, 0, 0);
      lv_obj_set_style_pad_bottom(b.card, 0, 0);
      lv_obj_set_flex_flow(b.card, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(b.card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

      lv_obj_t* name = lv_label_create(b.card);
      lv_label_set_text(name, b.name.c_str());
      lv_obj_set_width(name, 150);
      lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
      lv_obj_set_style_text_color(name, COL_MUTED, 0);
      lv_obj_set_style_text_font(name, &mira_font_fr_14, 0);

      b.valueLabel = lv_label_create(b.card);
      String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
      lv_label_set_text(b.valueLabel, txt.c_str());
      lv_obj_set_width(b.valueLabel, 116);
      lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
      lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_20, 0);
      return;
    }
  }

  if (autoDashboardTile || forcedTile) {
    b.card = makeCard(page, 66);
    lv_obj_set_width(b.card, 149);
    lv_obj_fade_in(b.card, 140, 0);
    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(b.card, 4, 0);

    b.valueLabel = lv_label_create(b.card);
    String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
    lv_label_set_text(b.valueLabel, txt.c_str());
    lv_obj_set_width(b.valueLabel, 137);
    lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_20, 0);
    lv_obj_align(b.valueLabel, LV_ALIGN_TOP_MID, 0, 7);

    lv_obj_t* name = lv_label_create(b.card);
    lv_label_set_text(name, b.name.c_str());
    lv_obj_set_width(name, 137);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, COL_MUTED, 0);
    lv_obj_set_style_text_font(name, &mira_font_fr_12, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -6);
    return;
  }

  if (forcedHero || valueDominant) {
    b.card = makeCard(page, forcedHero ? 84 : 72);
    lv_obj_fade_in(b.card, 150, 0);
    lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(b.card, 4, 0);

    b.valueLabel = lv_label_create(b.card);
    String txt = String("--") + (b.unit.length() ? " " + b.unit : "");
    lv_label_set_text(b.valueLabel, txt.c_str());
    lv_obj_set_width(b.valueLabel, 286);
    lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    lv_obj_set_style_text_font(b.valueLabel, forcedHero ? &lv_font_montserrat_32 : &lv_font_montserrat_28, 0);
    lv_obj_align(b.valueLabel, LV_ALIGN_TOP_MID, 0, forcedHero ? 4 : 2);

    lv_obj_t* name = lv_label_create(b.card);
    lv_label_set_text(name, b.name.c_str());
    lv_obj_set_width(name, 286);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, COL_MUTED, 0);
    lv_obj_set_style_text_font(name, &mira_font_fr_12, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -4);
    return;
  }

  b.card = makeCard(page, compact ? 30 : 36);
  lv_obj_fade_in(b.card, 140, 0);
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
}''', 'layout-aware info')

print('Mira Panel 0.6.17 intelligent layout controls applied; old configs stay automatic')
