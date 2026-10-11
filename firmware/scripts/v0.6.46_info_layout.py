"""Mira 0.6.46: fix absolute positioning for responsive Info values.

LVGL's lv_obj_set_pos() modifies X/Y styles, but DOES NOT reset an existing
LV_ALIGN_TOP_MID set by the original widget renderer. This made x/y offsets
relative to an already centered label and pushed initial '-- hPa' values
above the cards. Correct the alignment explicitly and keep its bounding box
in the icon's own coordinate system before and after MQTT/Jeedom updates.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino = root / "MIRA_PANEL.ino"
ui = root / "MiraPanelUI.cpp"

def once(text, old, new, part):
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"0.6.46 {part}: expected 1 match, found {n}")
    return text.replace(old, new, 1)

s = ino.read_text(encoding="utf-8")
s = once(s, '"0.6.45"', '"0.6.46"', "version")
s = once(s, "[MIRA] Firmware 0.6.45 initialise",
         "[MIRA] Firmware 0.6.46 initialise", "startup")
ino.write_text(s, encoding="utf-8")

s = ui.read_text(encoding="utf-8")
start = s.index("static void miraFitInfoValue(lv_obj_t* label, lv_obj_t* icon) {")
end = s.index("void MiraPanelUI::decorateInfo(uint8_t idx)", start)
fitter = s[start:end]

fitter = once(fitter,
  """  const lv_coord_t avail = lv_obj_get_width(label) - 8; // clipping safety
""",
  """  // The label's X position is relative to its LEFT/TOP, not to TOP_MID.
  // Remove the old alignment inherited from the unmodified Mira renderer.
  lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
  const lv_coord_t avail = lv_obj_get_width(label) - 12; // border/text safety
""", "left alignment in updates")

fitter = once(fitter,
  """    lv_coord_t measured = lv_txt_get_width(
        value, strlen(value), fonts[i], 0, LV_TEXT_FLAG_NONE);
""",
  """    lv_coord_t measured = lv_txt_get_width(
        value, strlen(value), fonts[i],
        lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
        LV_TEXT_FLAG_NONE);
""", "font text spacing")

fitter = once(fitter,
  """  // Icons and values form ONE vertically centered line even after font change.
  lv_obj_set_y(label, lv_obj_get_y(icon) +
       (lv_obj_get_height(icon) - lv_font_get_line_height(selected)) / 2);
""",
  """  // Do not use lv_obj_get_y(icon)/get_height(icon) here: the sibling icon
  // was just created and LVGL may not have updated its screen coordinates.
  // Stored style position/size are stable immediately, even during startup.
  const lv_coord_t iconY = lv_obj_get_y_aligned(icon);
  const lv_coord_t iconH = lv_obj_get_style_height(icon, LV_PART_MAIN);
  const lv_coord_t textH = lv_font_get_line_height(selected);
  lv_obj_set_height(label, textH + 4);
  lv_obj_set_pos(label, lv_obj_get_x_aligned(label),
                 iconY + (iconH - textH) / 2);
""", "consistent vertical positioning")

s = s[:start] + fitter + s[end:]
a = s.index("void MiraPanelUI::decorateInfo(uint8_t idx)")
b = s.index("void MiraPanelUI::createInfo(", a)
dec = s[a:b]
dec=once(dec,
"""  lv_obj_add_flag(b.valueLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_width(b.valueLabel, valueW > 10 ? valueW : 10);
""",
"""  lv_obj_add_flag(b.valueLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
  // Original Info creation aligned this label TOP_MID. set_pos() alone does
  // NOT override that alignment, which caused both clipping and top overflow.
  lv_obj_set_align(b.valueLabel, LV_ALIGN_TOP_LEFT);
  lv_obj_set_style_pad_all(b.valueLabel, 0, 0);
  lv_obj_set_width(b.valueLabel, valueW > 10 ? valueW : 10);
""", "explicit origin")
s = s[:a] + dec + s[b:]
ui.write_text(s, encoding="utf-8")
print("Mira 0.6.46: stable top-left value placement with real-unit font fitting")
