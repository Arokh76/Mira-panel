"""0.6.43: optional per-widget Info pictograms, always LEFT of the value.

Icon size is calculated from the rendered card dimensions, not hardcoded
per hardware board. Old JSON and WT32 default text tiles are unchanged.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino = root / "MIRA_PANEL.ino"
ui = root / "MiraPanelUI.cpp"
hdr = root / "MiraPanelUI.h"
spiffs = root / "MiraPanelSPIFFS.cpp"
server = root / "MiraPanelServer.cpp"

def one(src, old, new, label, count=1):
    actual = src.count(old)
    if actual != count:
        raise SystemExit(f"0.6.43 Info {label}: wanted {count}, found {actual}")
    return src.replace(old, new)

def part(src, start, stop, patch):
    a = src.index(start)
    b = src.index(stop, a + len(start))
    return src[:a] + patch(src[a:b]) + src[b:]

# 1. Leave every existing command ID and storage key alone. New metadata is
# optional and travels with existing widget JSON.
s = spiffs.read_text(encoding="utf-8")
s = one(s, 'String LVGL_Layout = "auto";\n',
       'String LVGL_Layout = "auto";\nString LVGL_LabelMode = "auto";\nString LVGL_Icon = "auto";\n',
       "persist mode globals")
s = one(s,
       '  lvgl_array["layout"] = LVGL_Layout.length() ? LVGL_Layout : "auto";\n',
       '''  lvgl_array["layout"] = LVGL_Layout.length() ? LVGL_Layout : "auto";
  lvgl_array["labelMode"] = LVGL_LabelMode;
  lvgl_array["icon"] = LVGL_Icon;
''', "persist JSON")
spiffs.write_text(s, encoding="utf-8")

s = server.read_text(encoding="utf-8")
needle = '''                    if (!LVGL_Style.length()) LVGL_Style = "standard";
'''
parse = needle + '''                    LVGL_LabelMode = request->hasParam("labelMode") ? request->getParam("labelMode")->value() : "auto";
                    LVGL_LabelMode.toLowerCase();
                    if (LVGL_LabelMode != "text" && LVGL_LabelMode != "icon" &&
                        LVGL_LabelMode != "both") LVGL_LabelMode = "auto";
                    LVGL_Icon = request->hasParam("icon") ? request->getParam("icon")->value() : "auto";
                    LVGL_Icon.toLowerCase();
                    if (LVGL_Icon.length() > 24) LVGL_Icon = "auto";
'''
s = one(s, needle, parse, "both programming endpoints", count=2)
s = one(s,
       '    displayInfo["freeLayout"] = true;\n',
       '''    displayInfo["freeLayout"] = true;
    displayInfo["widgetLabelModes"] = "auto,text,icon,both";
    displayInfo["widgetIcons"] = "auto,temperature,humidity,pressure,light,energy,plug,door";
''', "discover capabilities")
server.write_text(s, encoding="utf-8")

s = hdr.read_text(encoding="utf-8")
s = one(s, '    String layout = "auto";\n',
        '    String layout = "auto";\n    String labelMode = "auto";\n    String icon = "auto";\n', "binding fields")
s = one(s, '    lv_obj_t* valueLabel = nullptr;\n',
        '    lv_obj_t* valueLabel = nullptr;\n    lv_obj_t* infoNameLabel = nullptr;\n    lv_obj_t* infoIconRoot = nullptr;\n',
        "LVGL pointers")
s = one(s, '  void createInfo(uint8_t idx);\n',
        '  void createInfo(uint8_t idx);\n  void decorateInfo(uint8_t idx);\n',
        "renderer declaration")
hdr.write_text(s, encoding="utf-8")

s = ui.read_text(encoding="utf-8")
# Concrete figures built from LVGL primitives; no bitmap font, emoji or
# graphics asset is added to either board's flash.
glyph = r'''
static bool miraIconKnown(const String& kind) {
  return kind == "temperature" || kind == "humidity" || kind == "pressure" ||
         kind == "light" || kind == "energy" || kind == "plug" || kind == "door";
}
static String miraResolveInfoIcon(const String& overrideIcon,
                                  const String& name, const String& unit) {
  if (miraIconKnown(overrideIcon)) return overrideIcon;
  if (overrideIcon != "auto" && overrideIcon.length()) return "";
  String n = name, u = unit;
  n.toLowerCase(); u.toLowerCase();
  if (n.indexOf("temp") >= 0 || n.indexOf("thermo") >= 0 ||
      u.indexOf("°") >= 0) return "temperature";
  if (n.indexOf("humid") >= 0 || n.indexOf("hygro") >= 0) return "humidity";
  if (n.indexOf("press") >= 0 || u == "hpa" || u == "mbar") return "pressure";
  if (n.indexOf("lumi") >= 0 || n.indexOf("éclai") >= 0 ||
      n.indexOf("eclai") >= 0 || u == "lux") return "light";
  if (n.indexOf("porte") >= 0 || n.indexOf("fenêtre") >= 0) return "door";
  if (n.indexOf("prise") >= 0) return "plug";
  if (n.indexOf("énergie") >= 0 || n.indexOf("energie") >= 0 ||
      n.indexOf("puissance") >= 0 || u == "w" || u == "kw" ||
      u == "wh" || u == "kwh") return "energy";
  return "";
}
static lv_obj_t* miraGlyphPart(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                              lv_coord_t w, lv_coord_t h, bool outline,
                              lv_coord_t radius, lv_color_t accent) {
  lv_obj_t* item = lv_obj_create(parent);
  lv_obj_set_pos(item, x, y);
  lv_obj_set_size(item, w, h);
  lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(item, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_pad_all(item, 0, 0);
  lv_obj_set_style_radius(item, radius, 0);
  lv_obj_set_style_bg_opa(item, outline ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(item, accent, 0);
  lv_obj_set_style_border_width(item, outline ? 2 : 0, 0);
  if (outline) lv_obj_set_style_border_color(item, accent, 0);
  return item;
}
static lv_obj_t* miraCreateInfoGlyph(lv_obj_t* card, const String& kind,
                                    lv_coord_t side, lv_color_t accent) {
  lv_obj_t* root = lv_obj_create(card);
  lv_obj_set_size(root, side, side);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(root, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  const int d = side;
  const int stroke = d >= 42 ? 4 : 3;
  if (kind == "temperature") {
    miraGlyphPart(root, d*40/100, d/8, d/5, d*54/100, true, 7, accent);
    miraGlyphPart(root, d*31/100, d*58/100, d*38/100, d*38/100, false,
                  LV_RADIUS_CIRCLE, accent);
    miraGlyphPart(root, d*46/100, d/3, stroke, d*35/100, false, 2, accent);
  } else if (kind == "light") {
    miraGlyphPart(root, d/3, d/3, d/3, d/3, false, LV_RADIUS_CIRCLE, accent);
    miraGlyphPart(root, d/2-stroke/2, 0, stroke, d/5, false, 2, accent);
    miraGlyphPart(root, d/2-stroke/2, d*4/5, stroke, d/5, false, 2, accent);
    miraGlyphPart(root, 0, d/2-stroke/2, d/5, stroke, false, 2, accent);
    miraGlyphPart(root, d*4/5, d/2-stroke/2, d/5, stroke, false, 2, accent);
  } else if (kind == "pressure") {
    miraGlyphPart(root, d/7, d/6, d*5/7, d*5/7, true,
                  LV_RADIUS_CIRCLE, accent);
    miraGlyphPart(root, d*29/100, d*56/100, d*42/100, stroke,
                  false, 2, accent);
    miraGlyphPart(root, d*44/100, d*43/100, d/8, d/8,
                  false, LV_RADIUS_CIRCLE, accent);
  } else if (kind == "humidity") {
    miraGlyphPart(root, d*25/100, d*41/100, d/2, d/2,
                  false, LV_RADIUS_CIRCLE, accent);
    miraGlyphPart(root, d*41/100, d/7, d*18/100, d/2,
                  false, 4, accent);
  } else if (kind == "door") {
    miraGlyphPart(root, d/4, d/10, d/2, d*8/10, true, 2, accent);
    miraGlyphPart(root, d*59/100, d/2, stroke, stroke,
                  false, LV_RADIUS_CIRCLE, accent);
  } else {
    // Energy/plug: scalable plug silhouette.
    miraGlyphPart(root, d/4, d*33/100, d/2, d*4/10, false, 6, accent);
    miraGlyphPart(root, d*36/100, d*14/100, stroke, d/4, false, 2, accent);
    miraGlyphPart(root, d*59/100, d*14/100, stroke, d/4, false, 2, accent);
    miraGlyphPart(root, d/2-stroke/2, d*7/10, stroke, d/5,
                  false, 2, accent);
  }
  return root;
}
'''
s = one(s, 'static String roundRangeDisplayValue(',
        glyph + '\nstatic String roundRangeDisplayValue(', "vector glyph helpers")

s = one(s, '  String widgetLayout = obj["layout"] | "auto";\n',
'''  String widgetLabelMode = obj["labelMode"] | "auto";
  widgetLabelMode.toLowerCase();
  if (widgetLabelMode != "text" && widgetLabelMode != "icon" &&
      widgetLabelMode != "both") widgetLabelMode = "auto";
  String widgetIcon = obj["icon"] | "auto";
  widgetIcon.toLowerCase();
  String widgetLayout = obj["layout"] | "auto";
''', "JSON widget preferences")
s = one(s, "  b.layout = widgetLayout;\n",
        "  b.layout = widgetLayout;\n  b.labelMode = widgetLabelMode;\n  b.icon = widgetIcon;\n",
        "binding preferences")
def keepLabels(block):
    old = "    lv_obj_t* name = lv_label_create(b.card);"
    expected = block.count(old)
    if expected < 3:
        raise SystemExit(f"0.6.43 createInfo had only {expected} name labels")
    block = block.replace(old, "    b.infoNameLabel = lv_label_create(b.card);\n    lv_obj_t* name = b.infoNameLabel;")
    # The non-indented fallback branch also creates a label.
    old2 = "  lv_obj_t* name = lv_label_create(b.card);"
    block = one(block, old2, "  b.infoNameLabel = lv_label_create(b.card);\n  lv_obj_t* name = b.infoNameLabel;",
                "fallback name label")
    return block
a = s.index("void MiraPanelUI::createInfo(")
b = s.index("void MiraPanelUI::createButton(", a)
s = s[:a] + keepLabels(s[a:b]) + s[b:]

renderer = r'''
void MiraPanelUI::decorateInfo(uint8_t idx) {
  Binding& b = _bindings[idx];
  if (!b.card || !b.valueLabel) return;
  // Default WT32 remains byte-for-byte equivalent in its visual behavior.
  // On large panels, auto chooses an icon when the sensor can be recognized.
  if (b.labelMode == "text" ||
      (b.labelMode == "auto" && MIRA_DISPLAY_WIDTH <= 320)) return;
  const String kind = miraResolveInfoIcon(b.icon, b.name, b.unit);
  if (kind.isEmpty()) return; // Unknown icon: keep name rather than an empty glyph.

  lv_obj_update_layout(b.card);
  const lv_coord_t w = lv_obj_get_width(b.card);
  lv_coord_t h = lv_obj_get_height(b.card);
  if (w < 90 || h < 35) return;
  const bool iconOnly = b.labelMode == "icon";
  const bool showTitle = !iconOnly;
  if (iconOnly && MIRA_DISPLAY_WIDTH >= 800 && b.layout != "free" &&
      h >= 115) {
    // Compact card height is meaningful, not simply a hidden label.
    h = 100;
    lv_obj_set_height(b.card, h);
  }
  const lv_coord_t pad = w >= 220 ? 16 : 8;
  lv_coord_t side = (lv_coord_t)min((int)(w / 5), (int)(h / (showTitle ? 2 : 1.7f)));
  if (side > 70) side = 70;
  if (side < 18) side = 18;
  const int left = pad;
  int y = (h - side) / 2;
  if (showTitle && h > 90) y += 8;

  b.infoIconRoot = miraCreateInfoGlyph(b.card, kind, side, colorAccent());
  lv_obj_set_pos(b.infoIconRoot, left, y);

  lv_obj_add_flag(b.valueLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_width(b.valueLabel, w - left - side - pad - 6);
  lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(b.valueLabel,
      h > 105 && w > 210 ? &lv_font_montserrat_28 : &lv_font_montserrat_20, 0);
  lv_obj_set_pos(b.valueLabel, left + side + 10,
                 y + (side - (h > 105 && w > 210 ? 30 : 22)) / 2);
  if (b.infoNameLabel) {
    if (showTitle) {
      lv_obj_add_flag(b.infoNameLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_set_width(b.infoNameLabel, w - 2 * pad);
      lv_obj_set_style_text_align(b.infoNameLabel, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_font(b.infoNameLabel,
          h > 105 ? &mira_font_fr_16 : &mira_font_fr_12, 0);
      lv_obj_set_pos(b.infoNameLabel, pad, h > 105 ? 10 : 3);
    } else {
      lv_obj_add_flag(b.infoNameLabel, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (b.infoLastSeenLabel) {
    lv_obj_add_flag(b.infoLastSeenLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(b.infoLastSeenLabel, w - pad * 2);
    lv_obj_set_style_text_align(b.infoLastSeenLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(b.infoLastSeenLabel, pad, h - 21);
  }
}
'''
s = one(s, "void MiraPanelUI::createInfo(uint8_t idx) {",
        renderer + "\nvoid MiraPanelUI::createInfo(uint8_t idx) {", "renderer insertion")
s = one(s, "  applyFreeLayout(idx);\n",
        "  applyFreeLayout(idx);\n  if (b.type == W_INFO) decorateInfo(idx);\n",
        "decorate after real geometry")
ui.write_text(s, encoding="utf-8")
print("0.6.43 Info: auto/text/icon/both persisted, responsive left-of-value vector glyphs")
