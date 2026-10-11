"""Mira 0.6.44: replace blurred sleep-image zoom by a native 120px font.

The WT32 sleep label stays Montserrat 48. Waveshare uses the exact same font
family rasterized at 120px when generating the firmware, not at runtime.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino, ui, hdr = (root / n for n in ("MIRA_PANEL.ino", "MiraPanelUI.cpp", "MiraPanelUI.h"))


def one(source, old, new, label):
    actual = source.count(old)
    if actual != 1:
        raise SystemExit(f"0.6.44 {label}: expected 1 match, got {actual}")
    return source.replace(old, new, 1)


source = ino.read_text(encoding="utf-8")
source = one(source, '"0.6.43"', '"0.6.44"', "version")
source = one(source, "[MIRA] Firmware 0.6.43 initialise",
             "[MIRA] Firmware 0.6.44 initialise", "startup")
ino.write_text(source, encoding="utf-8")

source = hdr.read_text(encoding="utf-8")
source = one(source, """  lv_obj_t* _sleepClock = nullptr;
  lv_obj_t* _sleepClockCanvas = nullptr; // Waveshare only, magnified Montserrat
  lv_color_t* _sleepClockPixels = nullptr; // allocated once for the panel's lifetime
""", """  lv_obj_t* _sleepClock = nullptr;
""", "remove scaled canvas state")
hdr.write_text(source, encoding="utf-8")

source = ui.read_text(encoding="utf-8")
source = one(source, "static String roundRangeDisplayValue(",
    """#if defined(MIRA_TARGET_WAVESHARE_43C)
LV_FONT_DECLARE(mira_sleep_montserrat_120);
#endif

static String roundRangeDisplayValue(""", "declare native 120px font")

start = source.index("#if defined(MIRA_TARGET_WAVESHARE_43C)\n  // A 48px font")
needle = "    lv_obj_align(_sleepClock, LV_ALIGN_CENTER, 0, -48);\n  }"
end = source.index(needle, start) + len(needle)
native_label = """  _sleepClock = lv_label_create(_sleepOverlay);
  lv_label_set_text(_sleepClock, "--:--");
  lv_obj_set_style_text_color(_sleepClock, COL_MUTED, 0);
#if defined(MIRA_TARGET_WAVESHARE_43C)
  lv_obj_set_style_text_font(_sleepClock, &mira_sleep_montserrat_120, 0);
#else
  // WT32: unchanged original Montserrat 48, no stretching.
  lv_obj_set_style_text_font(_sleepClock, &lv_font_montserrat_48, 0);
#endif
  lv_obj_set_style_text_align(_sleepClock, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(_sleepClock, LV_ALIGN_CENTER, 0, -48);
"""
source = source[:start] + native_label + source[end:]

source = one(source,
    "  if (!_sleeping || !_clockInSleep || (!_sleepClock && !_sleepClockCanvas)) return;",
    "  if (!_sleeping || !_clockInSleep || !_sleepClock) return;",
    "sleep guard")
source = one(source,
    """      if (_sleepClock) lv_label_set_text(_sleepClock, buf);
      if (_sleepClockCanvas) {
        lv_canvas_fill_bg(_sleepClockCanvas, lv_color_hex(0x000000), LV_OPA_COVER);
        lv_draw_label_dsc_t ds;
        lv_draw_label_dsc_init(&ds);
        ds.font = &lv_font_montserrat_48;
        ds.color = COL_MUTED;
        ds.align = LV_TEXT_ALIGN_CENTER;
        lv_canvas_draw_text(_sleepClockCanvas, 0, 7, 184, &ds, buf);
      }""",
    "      lv_label_set_text(_sleepClock, buf);", "direct text update")
source = one(source,
    """    if (_sleepClock) {
      lv_obj_align(_sleepClock, LV_ALIGN_CENTER,
                   offsetX[_sleepClockPosition], -48 + offsetY[_sleepClockPosition]);
    }
    if (_sleepClockCanvas) {
      lv_obj_align(_sleepClockCanvas, LV_ALIGN_CENTER,
                   offsetX[_sleepClockPosition] * 2, -20 + offsetY[_sleepClockPosition] * 2);
    }""",
    """    lv_obj_align(_sleepClock, LV_ALIGN_CENTER,
                 offsetX[_sleepClockPosition], -48 + offsetY[_sleepClockPosition]);""",
    "anti-burn movement")

if "_sleepClockCanvas" in source or "lv_img_set_zoom(_sleepClockCanvas" in source:
    raise SystemExit("0.6.44: obsolete enlarged bitmap path remains")
ui.write_text(source, encoding="utf-8")
print("Mira 0.6.44: native 120px Waveshare sleep font; WT32 native 48px unchanged")
