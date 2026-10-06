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


replace_once(ino, '"0.6.18"', '"0.6.19"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.18 initialise', '[MIRA] Firmware 0.6.19 initialise', 'startup log')

# Persist free-layout geometry. X/W use the logical display width; Y/H use
# 1000 units per visible content-height, and Y may exceed 1000 for scrolling pages.
replace_once(
    spiffs,
    'String LVGL_Layout = "auto";\n',
    '''String LVGL_Layout = "auto";
int LVGL_LayoutX = -1;
int LVGL_LayoutY = -1;
int LVGL_LayoutW = -1;
int LVGL_LayoutH = -1;
''',
    'layout geometry globals',
)
replace_once(
    spiffs,
    '  lvgl_array["layout"] = LVGL_Layout.length() ? LVGL_Layout : "auto";\n',
    '''  lvgl_array["layout"] = LVGL_Layout.length() ? LVGL_Layout : "auto";
  if (LVGL_Layout == "free" && LVGL_LayoutX >= 0 && LVGL_LayoutY >= 0 && LVGL_LayoutW > 0 && LVGL_LayoutH > 0) {
    lvgl_array["lx"] = LVGL_LayoutX;
    lvgl_array["ly"] = LVGL_LayoutY;
    lvgl_array["lw"] = LVGL_LayoutW;
    lvgl_array["lh"] = LVGL_LayoutH;
  }
''',
    'persist free geometry',
)

# Both legacy programming HTTP routes accept free-layout geometry by name.
old_server_layout = '''                    LVGL_Layout = request->hasParam("layout") ? request->getParam("layout")->value() : "auto";
                    LVGL_Layout.trim();
                    LVGL_Layout.toLowerCase();
                    if (LVGL_Layout != "tile" && LVGL_Layout != "wide" && LVGL_Layout != "hero") LVGL_Layout = "auto";
                    AddListLVGL();'''
new_server_layout = '''                    LVGL_Layout = request->hasParam("layout") ? request->getParam("layout")->value() : "auto";
                    LVGL_Layout.trim();
                    LVGL_Layout.toLowerCase();
                    if (LVGL_Layout != "tile" && LVGL_Layout != "wide" && LVGL_Layout != "hero" && LVGL_Layout != "free") LVGL_Layout = "auto";

                    LVGL_LayoutX = request->hasParam("lx") ? request->getParam("lx")->value().toInt() : -1;
                    LVGL_LayoutY = request->hasParam("ly") ? request->getParam("ly")->value().toInt() : -1;
                    LVGL_LayoutW = request->hasParam("lw") ? request->getParam("lw")->value().toInt() : -1;
                    LVGL_LayoutH = request->hasParam("lh") ? request->getParam("lh")->value().toInt() : -1;
                    if (LVGL_Layout == "free") {
                      if (LVGL_LayoutX < 0 || LVGL_LayoutY < 0 || LVGL_LayoutW <= 0 || LVGL_LayoutH <= 0) {
                        LVGL_Layout = "auto";
                      }
                    } else {
                      LVGL_LayoutX = LVGL_LayoutY = LVGL_LayoutW = LVGL_LayoutH = -1;
                    }
                    AddListLVGL();'''
replace_all_exact(server, old_server_layout, new_server_layout, 2, 'free geometry HTTP parsing')
replace_once(
    server,
    '    displayInfo["freeLayout"] = false;\n',
    '    displayInfo["freeLayout"] = true;\n',
    'displayinfo free-layout capability',
)

# Binding carries geometry without affecting old automatic configs.
replace_once(
    hdr,
    '    String layout = "auto";\n',
    '''    String layout = "auto";
    int layoutX = -1;
    int layoutY = -1;
    int layoutW = -1;
    int layoutH = -1;
''',
    'binding geometry',
)
replace_once(
    hdr,
    '  lv_obj_t* makeCard(uint8_t page, lv_coord_t height = LV_SIZE_CONTENT);\n',
    '''  lv_obj_t* makeCard(uint8_t page, lv_coord_t height = LV_SIZE_CONTENT);
  void applyFreeLayout(uint8_t idx);
''',
    'free layout declaration',
)

# The UI parser now matches the 16K SPIFFS document; designer metadata costs a few bytes/widget.
replace_once(ui, '  DynamicJsonDocument doc(8192);\n', '  DynamicJsonDocument doc(16384);\n', 'UI JSON capacity')

replace_once(
    ui,
    '    if (layout != "tile" && layout != "wide" && layout != "hero") layout = "auto";\n',
    '    if (layout != "tile" && layout != "wide" && layout != "hero" && layout != "free") layout = "auto";\n',
    'pre-count layout validation',
)
replace_once(
    ui,
    '  if (widgetLayout != "tile" && widgetLayout != "wide" && widgetLayout != "hero") widgetLayout = "auto";\n',
    '  if (widgetLayout != "tile" && widgetLayout != "wide" && widgetLayout != "hero" && widgetLayout != "free") widgetLayout = "auto";\n',
    'binding layout validation',
)
replace_once(
    ui,
    '  b.layout = widgetLayout;\n',
    '''  b.layout = widgetLayout;
  if (b.layout == "free") {
    b.layoutX = obj["lx"] | -1;
    b.layoutY = obj["ly"] | -1;
    b.layoutW = obj["lw"] | -1;
    b.layoutH = obj["lh"] | -1;
    if (b.layoutX < 0 || b.layoutY < 0 || b.layoutW <= 0 || b.layoutH <= 0) b.layout = "auto";
  }
''',
    'read free geometry',
)

# Apply free geometry only after the widget factory has created its root card.
replace_once(
    ui,
    '''  else {
    b.type = W_UNSUPPORTED;
    createUnsupported(idx, type);
  }
}
''',
    '''  else {
    b.type = W_UNSUPPORTED;
    createUnsupported(idx, type);
  }

  applyFreeLayout(idx);
}
''',
    'apply free layout after widget creation',
)

# Free cards are taken out of flex flow. Pages remain vertically scrollable.
anchor = '''lv_obj_t* MiraPanelUI::makeSettingsCard(lv_coord_t height) {
'''
helper = r'''void MiraPanelUI::applyFreeLayout(uint8_t idx) {
  if (idx >= _bindingCount) return;
  Binding& b = _bindings[idx];
  if (b.layout != "free" || !b.card) return;
  if (b.layoutX < 0 || b.layoutY < 0 || b.layoutW <= 0 || b.layoutH <= 0) return;

  const int32_t logicalW = MIRA_LAYOUT_LOGICAL_WIDTH;
  const int32_t logicalH = MIRA_LAYOUT_LOGICAL_HEIGHT;
  int32_t x = ((int32_t)b.layoutX * MIRA_DISPLAY_WIDTH) / logicalW;
  int32_t y = ((int32_t)b.layoutY * MIRA_CONTENT_HEIGHT) / logicalH;
  int32_t w = ((int32_t)b.layoutW * MIRA_DISPLAY_WIDTH) / logicalW;
  int32_t h = ((int32_t)b.layoutH * MIRA_CONTENT_HEIGHT) / logicalH;

  const int32_t minW = 40;
  const int32_t minH = 32;
  if (w < minW) w = minW;
  if (h < minH) h = minH;
  if (w > MIRA_DISPLAY_WIDTH) w = MIRA_DISPLAY_WIDTH;
  if (x < 0) x = 0;
  if (x + w > MIRA_DISPLAY_WIDTH) x = MIRA_DISPLAY_WIDTH - w;
  if (y < 0) y = 0;

  lv_obj_add_flag(b.card, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_pos(b.card, (lv_coord_t)x, (lv_coord_t)y);
  lv_obj_set_size(b.card, (lv_coord_t)w, (lv_coord_t)h);
  lv_obj_clear_flag(b.card, LV_OBJ_FLAG_SCROLLABLE);
}

'''
text = ui.read_text(encoding='utf-8')
if helper.strip() not in text:
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f'free layout helper anchor: expected 1 match, got {count}')
    ui.write_text(text.replace(anchor, helper + anchor, 1), encoding='utf-8')

print('Mira Panel 0.6.19 free-layout engine applied; HTTP remains primary, MQTT unchanged')
