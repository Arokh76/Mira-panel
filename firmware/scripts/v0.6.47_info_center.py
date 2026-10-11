"""Mira 0.6.47 — center the *icon + displayed value + unit* as ONE group.

The 0.6.46 renderer uses a fixed 16px left anchor, which makes short
temperature/luminosity readings look left-weighted. Measure the real LVGL
glyph width and center the complete row in the card's CONTENT width.
This also handles placeholders (--), incoming MQTT/Jeedom updates, varying
units, and keeps WT32's original no-icon auto layout untouched.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino, ui = (root / name for name in ("MIRA_PANEL.ino", "MiraPanelUI.cpp"))

def once(src: str, old: str, new: str, label: str) -> str:
    count = src.count(old)
    if count != 1:
        raise SystemExit(f"0.6.47 {label}: expected one anchor, got {count}")
    return src.replace(old, new, 1)

s = ino.read_text(encoding="utf-8")
s = once(s, '"0.6.46"', '"0.6.47"', "version")
s = once(s, "[MIRA] Firmware 0.6.46 initialise",
         "[MIRA] Firmware 0.6.47 initialise", "startup")
ino.write_text(s, encoding="utf-8")

s = ui.read_text(encoding="utf-8")
begin = s.index("static void miraFitInfoValue(lv_obj_t* label, lv_obj_t* icon) {")
end = s.index("void MiraPanelUI::decorateInfo(uint8_t idx)", begin)
new_fitter = r'''// Reflow only widgets which have an icon. For each new value, choose a font
// that fits and place [icon][gap][value+unit] centered in the tile as a unit.
static void miraFitInfoValue(lv_obj_t* label, lv_obj_t* icon) {
  if (!label || !icon) return;
  lv_obj_t* card = lv_obj_get_parent(label);
  if (!card) return;
  const char* value = lv_label_get_text(label);
  if (!value) return;

  // LVGL child coordinates are relative to the parent's CONTENT box, not
  // always the parent width: account for card padding and borders.
  const lv_coord_t innerW = lv_obj_get_content_width(card);
  const lv_coord_t iconW = lv_obj_get_style_width(icon, LV_PART_MAIN);
  const lv_coord_t iconH = lv_obj_get_style_height(icon, LV_PART_MAIN);
  const lv_coord_t gap = (innerW >= 180) ? 10 : 6;
  const lv_coord_t margin = (innerW >= 180) ? 8 : 4;
  const lv_coord_t room = innerW - 2 * margin - iconW - gap - 6;
  if (room < 8) return;

  const lv_font_t* fonts[] = {
    &lv_font_montserrat_28, &lv_font_montserrat_20,
    &mira_font_fr_16, &mira_font_fr_12
  };
  const bool canBeLarge = innerW >= 200 &&
                          lv_obj_get_height(card) > 105;
  const size_t count = sizeof(fonts) / sizeof(fonts[0]);
  const lv_coord_t spacing =
      lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
  const lv_font_t* selected = fonts[count - 1];
  lv_coord_t textW = 0;
  bool fits = false;
  for (size_t i = canBeLarge ? 0 : 1; i < count; ++i) {
    const lv_coord_t measured =
        lv_txt_get_width(value, strlen(value), fonts[i],
                         spacing, LV_TEXT_FLAG_NONE);
    selected = fonts[i];
    textW = measured;
    if (measured <= room) { fits = true; break; }
  }

  // A long unexpected measurement is still clipped inside the tile.
  const lv_coord_t drawW = fits ? textW : room;
  const lv_coord_t groupW = iconW + gap + drawW;
  const lv_coord_t startX = margin + (innerW - 2 * margin - groupW) / 2;
  const lv_coord_t iconY = lv_obj_get_y_aligned(icon);
  const lv_coord_t textH = lv_font_get_line_height(selected);

  lv_obj_set_align(icon, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(icon, startX, iconY);
  // An old TOP_MID alignment was the reason 0.6.45 overflowed.
  lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(label, selected, 0);
  lv_label_set_long_mode(label, fits ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, drawW + 6);
  lv_obj_set_height(label, textH + 4);
  lv_obj_set_pos(label, startX + iconW + gap,
                 iconY + (iconH - textH) / 2);
}
'''
s = s[:begin] + new_fitter + s[end:]
# The initial decorateInfo() and both async update handlers already call
# miraFitInfoValue(), which is why the entire block remains centered.
if s.count("miraFitInfoValue(b.valueLabel, b.infoIconRoot);") != 3:
    raise SystemExit("0.6.47: expected startup + 2 update hooks")
ui.write_text(s, encoding="utf-8")
print("Mira 0.6.47: grouped icon+value+unit centered dynamically within each tile")
