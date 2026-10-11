"""0.6.43: proportional sleep clock on 800px Waveshare; WT32 stays original.

The 48px Montserrat font is rasterized into an LVGL canvas and rendered at
a 2.5x zoom. This preserves the actual WT32 glyph shape without enabling a
heavy 120px font or modifying the RGB display driver.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino = root / "MIRA_PANEL.ino"
ui = root / "MiraPanelUI.cpp"
hdr = root / "MiraPanelUI.h"

def once(src, old, new, tag):
    n = src.count(old)
    if n != 1:
        raise SystemExit(f"0.6.43 sleep {tag}: expected one match, got {n}")
    return src.replace(old, new, 1)

src = ino.read_text(encoding="utf-8")
src = once(src, '"0.6.42"', '"0.6.43"', "version")
src = once(src, "[MIRA] Firmware 0.6.42 initialise", "[MIRA] Firmware 0.6.43 initialise", "startup log")
ino.write_text(src, encoding="utf-8")

src = hdr.read_text(encoding="utf-8")
src = once(src, "  lv_obj_t* _sleepClock = nullptr;\n",
    """  lv_obj_t* _sleepClock = nullptr;
  lv_obj_t* _sleepClockCanvas = nullptr; // Waveshare only, magnified Montserrat
  lv_color_t* _sleepClockPixels = nullptr; // allocated once for the panel's lifetime
""", "clock canvas members")
hdr.write_text(src, encoding="utf-8")

src = ui.read_text(encoding="utf-8")
needle = """  _sleepClock = lv_label_create(_sleepOverlay);
  // Veille inspiree du firmware officiel : heure seule, tres lisible.
  lv_label_set_text(_sleepClock, "--:--");
  lv_obj_set_style_text_color(_sleepClock, COL_MUTED, 0);
  lv_obj_set_style_text_font(_sleepClock, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_align(_sleepClock, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(_sleepClock, LV_ALIGN_CENTER, 0, -48);
"""
replacement = """#if defined(MIRA_TARGET_WAVESHARE_43C)
  // A 48px font covers much less of an 800px panel than a 320px panel.
  // Zoom the *same Montserrat 48px glyphs* at 800/320 = 2.5x.
  // The source canvas uses RGB565 and costs about 24 KiB once per boot.
  static constexpr uint16_t SLEEP_CLOCK_W = 184;
  static constexpr uint16_t SLEEP_CLOCK_H = 66;
  _sleepClockPixels = static_cast<lv_color_t*>(
    lv_mem_alloc(SLEEP_CLOCK_W * SLEEP_CLOCK_H * sizeof(lv_color_t)));
  if (_sleepClockPixels) {
    _sleepClockCanvas = lv_canvas_create(_sleepOverlay);
    lv_canvas_set_buffer(_sleepClockCanvas, _sleepClockPixels,
                         SLEEP_CLOCK_W, SLEEP_CLOCK_H, LV_IMG_CF_TRUE_COLOR);
    lv_img_set_zoom(_sleepClockCanvas, 640); // 256 = 100%; 640 = 250%
    lv_obj_align(_sleepClockCanvas, LV_ALIGN_CENTER, 0, -20);
    lv_obj_clear_flag(_sleepClockCanvas, LV_OBJ_FLAG_CLICKABLE);
  } else {
    Serial.println("[MIRA][DISPLAY] Clock canvas unavailable: fallback font");
  }
#endif
  if (!_sleepClockCanvas) {
    _sleepClock = lv_label_create(_sleepOverlay);
    // WT32 original sleep font and its placement are unchanged.
    lv_label_set_text(_sleepClock, "--:--");
    lv_obj_set_style_text_color(_sleepClock, COL_MUTED, 0);
    lv_obj_set_style_text_font(_sleepClock, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(_sleepClock, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_sleepClock, LV_ALIGN_CENTER, 0, -48);
  }
"""
src = once(src, needle, replacement, "sleep clock creation")
src = once(src, "  if (!_sleeping || !_clockInSleep || !_sleepClock) return;",
  "  if (!_sleeping || !_clockInSleep || (!_sleepClock && !_sleepClockCanvas)) return;",
  "sleep clock guard")
src = once(src, """      lv_label_set_text(_sleepClock, buf);
""",
"""      if (_sleepClock) lv_label_set_text(_sleepClock, buf);
      if (_sleepClockCanvas) {
        lv_canvas_fill_bg(_sleepClockCanvas, lv_color_hex(0x000000), LV_OPA_COVER);
        lv_draw_label_dsc_t ds;
        lv_draw_label_dsc_init(&ds);
        ds.font = &lv_font_montserrat_48;
        ds.color = COL_MUTED;
        ds.align = LV_TEXT_ALIGN_CENTER;
        lv_canvas_draw_text(_sleepClockCanvas, 0, 7, 184, &ds, buf);
      }
""", "sleep clock drawing")
src = once(src,
"""    lv_obj_align(_sleepClock, LV_ALIGN_CENTER,
                 offsetX[_sleepClockPosition], -48 + offsetY[_sleepClockPosition]);""",
"""    if (_sleepClock) {
      lv_obj_align(_sleepClock, LV_ALIGN_CENTER,
                   offsetX[_sleepClockPosition], -48 + offsetY[_sleepClockPosition]);
    }
    if (_sleepClockCanvas) {
      lv_obj_align(_sleepClockCanvas, LV_ALIGN_CENTER,
                   offsetX[_sleepClockPosition] * 2, -20 + offsetY[_sleepClockPosition] * 2);
    }""", "sleep anti-burn")
ui.write_text(src, encoding="utf-8")
print("0.6.43 sleep: WT32 Montserrat 48 intact, Waveshare canvas zoom 2.5x")
