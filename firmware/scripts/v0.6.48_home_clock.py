"""Mira 0.6.48: unify Waveshare HOME clock typography with the WT32.

The seven-segment-only Waveshare dashboard clock introduced in 0.6.42 was
inconsistent with WT32's Montserrat home clock and both sleep clocks.
Replace it by a crisp native 96px Montserrat label on the Waveshare HOME,
keeping the date + Wi-Fi/day-night contextual information. Preserve 40px
WT32 HOME, 48px WT32 sleep, 120px Waveshare sleep and all widgets untouched.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv)>1 else Path("firmware/source/work/MIRA_PANEL")
ino, ui, hdr = (root/n for n in ("MIRA_PANEL.ino", "MiraPanelUI.cpp", "MiraPanelUI.h"))

def one(src, old, new, label):
    n = src.count(old)
    if n != 1:
        raise SystemExit(f"0.6.48 {label}: expected once, got {n}")
    return src.replace(old, new, 1)

s=ino.read_text(encoding="utf-8")
s=one(s, '"0.6.47"', '"0.6.48"', "version")
s=one(s, "[MIRA] Firmware 0.6.47 initialise",
      "[MIRA] Firmware 0.6.48 initialise", "boot")
ino.write_text(s, encoding="utf-8")

s=hdr.read_text(encoding="utf-8")
s=one(s, """  lv_obj_t* _clockSegments[4][7] = {};
  lv_obj_t* _clockColon[2] = {};
  lv_obj_t* _clockStatusLabel = nullptr;
  String _clockStatusCache;
  int8_t _lastClockDigits[4] = {-1, -1, -1, -1};""",
"""  lv_obj_t* _clockStatusLabel = nullptr;
  String _clockStatusCache;""", "remove segment members")
hdr.write_text(s, encoding="utf-8")

s=ui.read_text(encoding="utf-8")
s=one(s, "LV_FONT_DECLARE(mira_sleep_montserrat_120);",
      "LV_FONT_DECLARE(mira_sleep_montserrat_120);\nLV_FONT_DECLARE(mira_home_montserrat_96);",
      "declare home Montserrat")
s=one(s, """// Clock segments use LVGL primitives instead of a larger external font.
static const uint8_t MIRA_DIGIT_MASK[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};
""", "// Waveshare clock and WT32 clock now share the Montserrat family.\n",
      "remove digit bits")
s=one(s, """  _clockStatusLabel = nullptr;
  _clockStatusCache = "";
  for (int d = 0; d < 4; ++d) {
    _lastClockDigits[d] = -1;
    for (int j = 0; j < 7; ++j) _clockSegments[d][j] = nullptr;
  }
  _clockColon[0] = _clockColon[1] = nullptr;""",
"""  _clockStatusLabel = nullptr;
  _clockStatusCache = "";""", "cleanup init")

start=s.index("void MiraPanelUI::createDateTime(")
a=s.index('  if (MIRA_DISPLAY_WIDTH >= 800 && b.layout != "free") {',start)
b=s.index("  b.valueLabel = lv_label_create(b.card);",a)
new = """  if (MIRA_DISPLAY_WIDTH >= 800 && b.layout != "free") {
    // Same Montserrat typography as WT32 HOME, natively rasterized at 96px
    // for Waveshare 800px; no image enlargement and no seven-segment face.
    // Keep the existing Waveshare-wide dashboard date and connection status.
    lv_obj_set_height(b.card, 158);
    _dateTimeLabel = lv_label_create(b.card);
    b.valueLabel = _dateTimeLabel;
    lv_label_set_text(_dateTimeLabel, "--:--");
    lv_obj_set_align(_dateTimeLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_width(_dateTimeLabel, 390);
    lv_label_set_long_mode(_dateTimeLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(_dateTimeLabel, colorAccent(), 0);
#if defined(MIRA_TARGET_WAVESHARE_43C)
    lv_obj_set_style_text_font(_dateTimeLabel, &mira_home_montserrat_96, 0);
#else
    lv_obj_set_style_text_font(_dateTimeLabel, &lv_font_montserrat_40, 0);
#endif
    lv_obj_set_style_text_align(_dateTimeLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(_dateTimeLabel, 14, 0);

    _dateSubLabel = lv_label_create(b.card);
    lv_label_set_text(_dateSubLabel, "-- -- ----");
    lv_obj_set_align(_dateSubLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_width(_dateSubLabel, 370);
    lv_obj_set_style_text_color(_dateSubLabel, COL_MUTED, 0);
    lv_obj_set_style_text_font(_dateSubLabel, &mira_font_fr_16, 0);
    lv_obj_set_style_text_align(_dateSubLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(_dateSubLabel, 14, 125);

    _clockStatusLabel = lv_label_create(b.card);
    lv_obj_set_width(_clockStatusLabel, 355);
    lv_obj_set_style_text_font(_clockStatusLabel, &mira_font_fr_16, 0);
    lv_obj_set_style_text_color(_clockStatusLabel, COL_MUTED, 0);
    lv_label_set_text(_clockStatusLabel, "MIRA PANEL\\nConnexion...\\nMode jour");
    lv_obj_set_pos(_clockStatusLabel, 422, 36);
    return;
  }
"""
s=s[:a]+new+s[b:]
s=one(s, """void MiraPanelUI::updateClock() {
  if (!_dateTimeLabel && !_clockSegments[0][0]) return;""",
"""void MiraPanelUI::updateClock() {
  if (!_dateTimeLabel) return;""", "clock guard")
at=s.index("void MiraPanelUI::updateClock()")
a=s.index("  if (_clockSegments[0][0]) {",at)
b=s.index("  char timeBuf[8];",a)
s=s[:a]+s[b:]
if "_clockSegments" in s or "_clockColon" in s or "MIRA_DIGIT_MASK" in s:
    raise SystemExit("0.6.48: obsolete seven-segment code left in UI")
if "mira_home_montserrat_96" not in s or "lv_font_montserrat_40" not in s:
    raise SystemExit("0.6.48: home font missing")
ui.write_text(s, encoding="utf-8")
print("0.6.48 HOME: WT32 Montserrat40 unchanged; Waveshare native Montserrat96 + info panel")
