"""Mira 0.6.45: keep icon + numerical value + unit inside its actual tile.

Fixes a real 0.6.44 Waveshare photo where "1015.3 hPa" clipped on the right:
the label position/width previously exceeded card width by 4 px, and the
28px text font was selected solely from panel dimensions.

Measure text at runtime with LVGL's glyph metrics. Retry with smaller fonts
only on the affected tile, and repeat when Jeedom or MQTT values change.
Default WT32 Info (text without icon) is not modified.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino = root / "MIRA_PANEL.ino"
ui = root / "MiraPanelUI.cpp"


def once(content, old, new, label, count=1):
    n = content.count(old)
    if n != count:
        raise SystemExit(f"0.6.45 {label}: expected {count}, found {n}")
    return content.replace(old, new)


src = ino.read_text(encoding="utf-8")
src = once(src, '"0.6.44"', '"0.6.45"', "firmware version")
src = once(src, "[MIRA] Firmware 0.6.44 initialise",
           "[MIRA] Firmware 0.6.45 initialise", "startup")
ino.write_text(src, encoding="utf-8")

src = ui.read_text(encoding="utf-8")
helper = r'''
// The displayed value may change length at any time (MQTT / Jeedom), e.g.
// "999 hPa" -> "1015.3 hPa". Fit the *actual glyph width* into the available
// label, including the unit, instead of assuming a font based on screen width.
static void miraFitInfoValue(lv_obj_t* label, lv_obj_t* icon) {
  if (!label || !icon) return; // Unmodified original WT32 text-only tiles.
  const char* value = lv_label_get_text(label);
  if (!value) return;
  const lv_coord_t avail = lv_obj_get_width(label) - 8; // clipping safety
  if (avail < 8) return;

  lv_obj_t* card = lv_obj_get_parent(label);
  const bool canBeLarge = lv_obj_get_width(card) > 210 &&
                          lv_obj_get_height(card) > 105;
  const lv_font_t* fonts[] = {
      &lv_font_montserrat_28,
      &lv_font_montserrat_20,
      &mira_font_fr_16,
      &mira_font_fr_12
  };
  const size_t start = canBeLarge ? 0 : 1;
  const size_t count = sizeof(fonts) / sizeof(fonts[0]);
  const lv_font_t* selected = fonts[count - 1];
  bool fits = false;
  for (size_t i = start; i < count; ++i) {
    lv_coord_t measured = lv_txt_get_width(
        value, strlen(value), fonts[i], 0, LV_TEXT_FLAG_NONE);
    if (measured <= avail) {
      selected = fonts[i];
      fits = true;
      break;
    }
  }
  lv_obj_set_style_text_font(label, selected, 0);
  lv_label_set_long_mode(label, fits ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_DOT);
  // Icons and values form ONE vertically centered line even after font change.
  lv_obj_set_y(label, lv_obj_get_y(icon) +
       (lv_obj_get_height(icon) - lv_font_get_line_height(selected)) / 2);
}
'''
src=once(src, "void MiraPanelUI::decorateInfo(uint8_t idx) {",
         helper+"\nvoid MiraPanelUI::decorateInfo(uint8_t idx) {",
         "insert fitter")
old_label="""  lv_obj_add_flag(b.valueLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_width(b.valueLabel, w - left - side - pad - 6);
  lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(b.valueLabel,
      h > 105 && w > 210 ? &lv_font_montserrat_28 : &lv_font_montserrat_20, 0);
  lv_obj_set_pos(b.valueLabel, left + side + 10,
                 y + (side - (h > 105 && w > 210 ? 30 : 22)) / 2);"""
new_label="""  const lv_coord_t valueX = left + side + 10;
  // Previous width + x position could extend 4px BEYOND the card's edge.
  const lv_coord_t valueW = w - valueX - pad - 4;
  lv_obj_add_flag(b.valueLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_width(b.valueLabel, valueW > 10 ? valueW : 10);
  lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_pos(b.valueLabel, valueX, y);
  miraFitInfoValue(b.valueLabel, b.infoIconRoot);"""
src=once(src,old_label,new_label,"card value bounds")
needle="    miraMarkInfoArrival(b.infoLastSeenLabel);\n"
src=once(src,needle,"    miraMarkInfoArrival(b.infoLastSeenLabel);\n"
    "    if (b.infoIconRoot) miraFitInfoValue(b.valueLabel, b.infoIconRoot);\n",
    "MQTT and Jeedom value updates",count=2)
ui.write_text(src, encoding="utf-8")
print("Mira 0.6.45: measured responsive icon value/units without clipping")
