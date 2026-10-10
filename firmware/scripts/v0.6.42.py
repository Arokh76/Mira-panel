"""Mira 0.6.42: a proper Waveshare 800x480 dashboard.

Only large-screen automatic cards are changed: a 92px 7-segment
clock, context-aware info tile widths, last-reception times, Wi-Fi state.
No Jeedom schema change or extra sensor values, WT32 remains untouched.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino, ui, hdr = (root / n for n in ("MIRA_PANEL.ino", "MiraPanelUI.cpp", "MiraPanelUI.h"))

def once(s, old, new, name):
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"0.6.42 {name}: expected 1 anchor, found {n}")
    return s.replace(old, new, 1)

def section(s, start, stop, op):
    a = s.index(start)
    b = s.index(stop, a + len(start))
    return s[:a] + op(s[a:b]) + s[b:]

s = ino.read_text(encoding="utf-8")
s = once(s, '"0.6.41"', '"0.6.42"', "version")
s = once(s, "[MIRA] Firmware 0.6.41 initialise", "[MIRA] Firmware 0.6.42 initialise", "startup")
ino.write_text(s, encoding="utf-8")

s = hdr.read_text(encoding="utf-8")
s = once(s, "    lv_obj_t* valueLabel = nullptr;\n",
         "    lv_obj_t* valueLabel = nullptr;\n    lv_obj_t* infoLastSeenLabel = nullptr;\n", "Info metadata")
s = once(s, "  lv_obj_t* _dateSubLabel = nullptr;\n", """  lv_obj_t* _dateSubLabel = nullptr;
  lv_obj_t* _clockSegments[4][7] = {};
  lv_obj_t* _clockColon[2] = {};
  lv_obj_t* _clockStatusLabel = nullptr;
  String _clockStatusCache;
  int8_t _lastClockDigits[4] = {-1, -1, -1, -1};
""", "digital clock pointers")
hdr.write_text(s, encoding="utf-8")

s = ui.read_text(encoding="utf-8")
helpers = r'''// Clock segments use LVGL primitives instead of a larger external font.
static const uint8_t MIRA_DIGIT_MASK[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};
static void miraMarkInfoArrival(lv_obj_t* label) {
  if (!label) return;
  time_t epoch = time(nullptr);
  if (epoch < 1700000000) { lv_label_set_text(label, "Valeur reçue"); return; }
  struct tm t;
  localtime_r(&epoch, &t);
  char stamp[24];
  snprintf(stamp, sizeof(stamp), "MAJ %02d:%02d", t.tm_hour, t.tm_min);
  lv_label_set_text(label, stamp);
}
'''
s = once(s, "static String roundRangeDisplayValue(", helpers + "\nstatic String roundRangeDisplayValue(", "clock/helpers")
s = once(s, "  _dateSubLabel = nullptr;\n", """  _dateSubLabel = nullptr;
  _clockStatusLabel = nullptr;
  _clockStatusCache = "";
  for (int d = 0; d < 4; ++d) {
    _lastClockDigits[d] = -1;
    for (int j = 0; j < 7; ++j) _clockSegments[d][j] = nullptr;
  }
  _clockColon[0] = _clockColon[1] = nullptr;
""", "reload cleanup")

# Use all the available width when only 2 or 3 Info widgets are configured,
# without forcing the user to configure a different layout on Jeedom.
def improveInfo(q):
    old = """  if (autoDashboardTile || forcedTile) {
    b.card = makeCard(page, MIRA_DISPLAY_WIDTH >= 800 ? 78 : 66);
    lv_obj_set_width(b.card, MIRA_CARD_TILE);"""
    new = """  if (autoDashboardTile || forcedTile) {
    const bool adaptive = MIRA_DISPLAY_WIDTH >= 800 && autoDashboardTile;
    lv_coord_t tileW = MIRA_CARD_TILE;
    if (adaptive) {
      const uint8_t total = _homeStandardInfoTotal;
      const uint8_t lastRow = total % 4 ? total % 4 : 4;
      const bool finalRow = _homeStandardInfoCreated > total - lastRow;
      uint8_t columns = finalRow ? lastRow : 4;
      if (columns < 2) columns = 2; // one tile is 390px, not 786px
      tileW = (MIRA_CARD_FULL - (columns - 1) * 6) / columns;
    }
    b.card = makeCard(page, adaptive ? 142 : (MIRA_DISPLAY_WIDTH >= 800 ? 78 : 66));
    lv_obj_set_width(b.card, tileW);"""
    q = once(q, old, new, "responsive info width")
    q = once(q, """    lv_obj_set_width(b.valueLabel, MIRA_LABEL_TILE);
    lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_20, 0);
    lv_obj_align(b.valueLabel, LV_ALIGN_TOP_MID, 0, 7);""",
    """    lv_obj_set_width(b.valueLabel, adaptive ? tileW - 14 : MIRA_LABEL_TILE);
    lv_label_set_long_mode(b.valueLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(b.valueLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(b.valueLabel, colorAccent(), 0);
    lv_obj_set_style_text_font(b.valueLabel,
      adaptive ? (tileW >= 250 ? &lv_font_montserrat_32 : &lv_font_montserrat_28) : &lv_font_montserrat_20, 0);
    lv_obj_align(b.valueLabel, LV_ALIGN_TOP_MID, 0, adaptive ? 48 : 7);""",
    "larger info values")
    q = once(q, """    lv_obj_set_width(name, MIRA_LABEL_TILE);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, COL_MUTED, 0);
    lv_obj_set_style_text_font(name, &mira_font_fr_12, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -6);
    return;""", """    lv_obj_set_width(name, adaptive ? tileW - 18 : MIRA_LABEL_TILE);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, COL_MUTED, 0);
    lv_obj_set_style_text_font(name, adaptive ? &mira_font_fr_16 : &mira_font_fr_12, 0);
    lv_obj_align(name, adaptive ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, adaptive ? 12 : -6);
    if (adaptive) {
      b.infoLastSeenLabel = lv_label_create(b.card);
      lv_label_set_text(b.infoLastSeenLabel, "En attente des données");
      lv_obj_set_width(b.infoLastSeenLabel, tileW - 18);
      lv_label_set_long_mode(b.infoLastSeenLabel, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_text_align(b.infoLastSeenLabel, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_color(b.infoLastSeenLabel, COL_MUTED, 0);
      lv_obj_set_style_text_font(b.infoLastSeenLabel, &mira_font_fr_12, 0);
      lv_obj_align(b.infoLastSeenLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
    return;""", "info arrival caption")
    return q
s = section(s, "void MiraPanelUI::createInfo(", "void MiraPanelUI::createButton(", improveInfo)

# Both state channels are read-only UI updates, not action re-publications.
s = once(s, """    if (b.valueLabel) lv_label_set_text(b.valueLabel, txt.c_str());
  }
  else if (b.type == W_TOGGLE) {""",
"""    if (b.valueLabel) lv_label_set_text(b.valueLabel, txt.c_str());
    miraMarkInfoArrival(b.infoLastSeenLabel);
  }
  else if (b.type == W_TOGGLE) {""", "MQTT arrival")
s = once(s, """    if (b.valueLabel) lv_label_set_text(b.valueLabel, txt.c_str());
  }
  else if (b.type == W_TOGGLE && (eventId == b.idOff || eventId == b.idOn)) {""",
"""    if (b.valueLabel) lv_label_set_text(b.valueLabel, txt.c_str());
    miraMarkInfoArrival(b.infoLastSeenLabel);
  }
  else if (b.type == W_TOGGLE && (eventId == b.idOff || eventId == b.idOn)) {""",
"Jeedom arrival")

# Place the new clock ONLY on Waveshare automatic layouts. The WT32 uses
# the original code, and free-layout geometry is not changed.
clock = r'''  if (MIRA_DISPLAY_WIDTH >= 800 && b.layout != "free") {
    lv_obj_set_height(b.card, 158);
    static const lv_coord_t baseX[4] = {36, 100, 190, 254};
    static const lv_coord_t partX[7] = {8, 47, 47, 8, 0, 0, 8};
    static const lv_coord_t partY[7] = {0, 8, 50, 84, 50, 8, 42};
    static const lv_coord_t partW[7] = {39, 8, 8, 39, 8, 8, 39};
    static const lv_coord_t partH[7] = {8, 34, 34, 8, 34, 34, 8};
    for (int d = 0; d < 4; ++d) {
      for (int j = 0; j < 7; ++j) {
        lv_obj_t* part = lv_obj_create(b.card);
        _clockSegments[d][j] = part;
        lv_obj_set_pos(part, baseX[d] + partX[j], 10 + partY[j]);
        lv_obj_set_size(part, partW[j], partH[j]);
        lv_obj_clear_flag(part, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(part, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(part, colorAccent(), 0);
        lv_obj_set_style_bg_opa(part, LV_OPA_10, 0);
        lv_obj_set_style_radius(part, 4, 0);
        lv_obj_set_style_border_width(part, 0, 0);
        lv_obj_set_style_pad_all(part, 0, 0);
      }
    }
    for (int i = 0; i < 2; ++i) {
      _clockColon[i] = lv_obj_create(b.card);
      lv_obj_set_pos(_clockColon[i], 174, i ? 74 : 35);
      lv_obj_set_size(_clockColon[i], 9, 9);
      lv_obj_set_style_radius(_clockColon[i], LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(_clockColon[i], colorAccent(), 0);
      lv_obj_set_style_bg_opa(_clockColon[i], LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(_clockColon[i], 0, 0);
      lv_obj_set_style_pad_all(_clockColon[i], 0, 0);
      lv_obj_clear_flag(_clockColon[i], LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_clear_flag(_clockColon[i], LV_OBJ_FLAG_CLICKABLE);
    }
    _dateSubLabel = lv_label_create(b.card);
    lv_label_set_text(_dateSubLabel, "-- -- ----");
    lv_obj_set_width(_dateSubLabel, 340);
    lv_obj_set_style_text_color(_dateSubLabel, COL_MUTED, 0);
    lv_obj_set_style_text_font(_dateSubLabel, &mira_font_fr_16, 0);
    lv_obj_set_style_text_align(_dateSubLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(_dateSubLabel, 10, 121);
    _clockStatusLabel = lv_label_create(b.card);
    lv_obj_set_width(_clockStatusLabel, 355);
    lv_obj_set_style_text_font(_clockStatusLabel, &mira_font_fr_16, 0);
    lv_obj_set_style_text_color(_clockStatusLabel, COL_MUTED, 0);
    lv_label_set_text(_clockStatusLabel, "MIRA PANEL\nConnexion...\nMode jour");
    lv_obj_set_pos(_clockStatusLabel, 422, 36);
    return;
  }
'''
s = section(s, "void MiraPanelUI::createDateTime(", "void MiraPanelUI::createUnsupported(",
            lambda q: once(q, "  b.valueLabel = lv_label_create(b.card);\n",
                           clock + "\n  b.valueLabel = lv_label_create(b.card);\n", "large clock design"))

s = once(s, "void MiraPanelUI::updateClock() {\n  if (!_dateTimeLabel) return;",
    "void MiraPanelUI::updateClock() {\n  if (!_dateTimeLabel && !_clockSegments[0][0]) return;", "clock active guard")
s = once(s, "  if (!getLocalTime(&t, 10)) return;\n\n  char timeBuf[8];", r'''  if (_clockStatusLabel) {
    const String wifi = WiFi.status() == WL_CONNECTED
      ? String("Wi-Fi  ") + String(WiFi.RSSI()) + " dBm"
      : String("Wi-Fi déconnecté");
    const String value = String("MIRA PANEL\n") + wifi + "\n" + (_nightActive ? "Mode nuit" : "Mode jour");
    if (value != _clockStatusCache) {
      _clockStatusCache = value;
      lv_label_set_text(_clockStatusLabel, value.c_str());
    }
  }
  if (!getLocalTime(&t, 10)) return;
  if (_clockSegments[0][0]) {
    const uint8_t digits[4] = {
      uint8_t(t.tm_hour / 10), uint8_t(t.tm_hour % 10),
      uint8_t(t.tm_min / 10), uint8_t(t.tm_min % 10)
    };
    for (int d = 0; d < 4; ++d) {
      if (_lastClockDigits[d] == digits[d]) continue;
      _lastClockDigits[d] = digits[d];
      for (int j = 0; j < 7; ++j) {
        lv_obj_set_style_bg_opa(_clockSegments[d][j],
          (MIRA_DIGIT_MASK[digits[d]] & (1U << j)) ? LV_OPA_COVER : LV_OPA_10, 0);
      }
    }
    for (int i = 0; i < 2; ++i)
      if (_clockColon[i])
        lv_obj_set_style_bg_opa(_clockColon[i], (t.tm_sec & 1) ? LV_OPA_30 : LV_OPA_COVER, 0);
  }

  char timeBuf[8];''', "segment clock updater")
s = once(s, "  lv_label_set_text(_dateTimeLabel, timeBuf);",
         "  if (_dateTimeLabel) lv_label_set_text(_dateTimeLabel, timeBuf);", "legacy time label")
ui.write_text(s, encoding="utf-8")
print("Mira 0.6.42 preview: real 92px clock and adaptive info widgets")
