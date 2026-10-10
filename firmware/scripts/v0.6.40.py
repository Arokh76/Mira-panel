"""Mira Panel 0.6.40 preview: Waveshare 800x480 responsive widgets.

WT32 dimensions, Jeedom/MQTT bindings, and manual free-layout coordinates
are unchanged. Applied after the successful 0.6.39 reconstruction.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino = root / "MIRA_PANEL.ino"
ui = root / "MiraPanelUI.cpp"

def once(s, old, new, label):
    count = s.count(old)
    if count != 1:
        raise SystemExit(f"0.6.40 {label}: expected one match, got {count}")
    return s.replace(old, new, 1)

def within(s, start, end, patch):
    a = s.index(start)
    b = s.index(end, a + len(start))
    return s[:a] + patch(s[a:b]) + s[b:]

source = ino.read_text(encoding="utf-8")
source = once(source, '"0.6.39"', '"0.6.40"', "version")
source = once(source, "[MIRA] Firmware 0.6.39 initialise",
              "[MIRA] Firmware 0.6.40 initialise", "boot log")
ino.write_text(source, encoding="utf-8")

source = ui.read_text(encoding="utf-8")
constants = """// Automatic widget grid from screen geometry, without affecting free layout.
// 800px page - 12px margins = 788px; 4*192+3*6 = 2*390+6 = 786px.
static constexpr lv_coord_t MIRA_CARD_AUTO = MIRA_DISPLAY_WIDTH >= 800 ? 390 : 304;
static constexpr lv_coord_t MIRA_CARD_TILE = MIRA_DISPLAY_WIDTH >= 800 ? 192 : 149;
static constexpr lv_coord_t MIRA_CARD_FULL = MIRA_DISPLAY_WIDTH >= 800 ? 786 : 304;
static constexpr lv_coord_t MIRA_LABEL_TILE = MIRA_DISPLAY_WIDTH >= 800 ? 180 : 137;
static constexpr lv_coord_t MIRA_LABEL_AUTO = MIRA_DISPLAY_WIDTH >= 800 ? 370 : 286;
static constexpr lv_coord_t MIRA_LABEL_WIDE_NAME = MIRA_DISPLAY_WIDTH >= 800 ? 440 : 150;
static constexpr lv_coord_t MIRA_LABEL_WIDE_VALUE = MIRA_DISPLAY_WIDTH >= 800 ? 292 : 116;

"""
source = once(source, "static String roundRangeDisplayValue(",
              constants + "static String roundRangeDisplayValue(", "responsive constants")

old_pages = """    } else {
      if (i == 1 || i == 2) lv_obj_set_style_pad_top(_pages[i], 8, 0);
      lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    }"""
new_pages = """    } else {
      if (i == 1 || i == 2) lv_obj_set_style_pad_top(_pages[i], 8, 0);
      if (MIRA_DISPLAY_WIDTH >= 800 && (i == 1 || i == 2)) {
        lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      } else {
        lv_obj_set_flex_flow(_pages[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_pages[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      }
    }"""
source = once(source, old_pages, new_pages, "secondary page grid")
source = within(source, "lv_obj_t* MiraPanelUI::makeCard(",
    "void MiraPanelUI::applyFreeLayout(", lambda s:
    once(s, "lv_obj_set_width(card, 304);",
         "lv_obj_set_width(card, MIRA_CARD_AUTO);", "base card"))
source = within(source, "lv_obj_t* MiraPanelUI::makeTransparentRow(",
    "void MiraPanelUI::makeNameLabel(", lambda s:
    once(s, "lv_obj_set_width(row, 284);",
         "lv_obj_set_width(row, MIRA_DISPLAY_WIDTH >= 800 ? 366 : 284);", "row width"))

def info(s):
    if s.count("lv_obj_set_width(b.card, 304);") != 2:
        raise SystemExit("info full card count changed")
    s = s.replace("lv_obj_set_width(b.card, 304);",
                  "lv_obj_set_width(b.card, MIRA_CARD_FULL);")
    for old, new, expected in (
        ("lv_obj_set_width(name, 150);",
         "lv_obj_set_width(name, MIRA_LABEL_WIDE_NAME);", 2),
        ("lv_obj_set_width(b.valueLabel, 116);",
         "lv_obj_set_width(b.valueLabel, MIRA_LABEL_WIDE_VALUE);", 2),
        ("lv_obj_set_width(b.valueLabel, 137);",
         "lv_obj_set_width(b.valueLabel, MIRA_LABEL_TILE);", 1),
        ("lv_obj_set_width(name, 137);",
         "lv_obj_set_width(name, MIRA_LABEL_TILE);", 1),
        ("lv_obj_set_width(b.valueLabel, 286);",
         "lv_obj_set_width(b.valueLabel, MIRA_LABEL_AUTO);", 1),
        ("lv_obj_set_width(name, 286);",
         "lv_obj_set_width(name, MIRA_LABEL_AUTO);", 1),
    ):
        if s.count(old) != expected:
            raise SystemExit(f"info expected {expected} uses of {old}")
        s = s.replace(old, new)
    s = once(s,
        "const bool lastOddTile = (_homeStandardInfoTotal & 1U) &&",
        "const bool lastOddTile = MIRA_DISPLAY_WIDTH <= 320 && (_homeStandardInfoTotal & 1U) &&",
        "WT32-only odd-row arrangement")
    s = once(s, "b.card = makeCard(page, 66);",
             "b.card = makeCard(page, MIRA_DISPLAY_WIDTH >= 800 ? 78 : 66);",
             "info height")
    s = once(s, "lv_obj_set_width(b.card, 149);",
             "lv_obj_set_width(b.card, MIRA_CARD_TILE);",
             "info tile width")
    return s
source = within(source, "void MiraPanelUI::createInfo(",
                "void MiraPanelUI::createButton(", info)

source = within(source, "void MiraPanelUI::createButton(",
    "void MiraPanelUI::createToggle(", lambda s: once(s,
    "lv_obj_set_size(b.control, compact ? 230 : 288, compact ? 34 : 44);",
    "lv_obj_set_size(b.control, MIRA_DISPLAY_WIDTH >= 800 ? (compact ? 344 : 360) : (compact ? 230 : 288), compact ? 34 : 44);",
    "button control size"))

source = within(source, "void MiraPanelUI::createRange(",
    "void MiraPanelUI::createRoundRange(", lambda s: once(s,
    "lv_obj_set_size(b.control, compact ? 230 : 258, compact ? 12 : 14);",
    "lv_obj_set_size(b.control, MIRA_DISPLAY_WIDTH >= 800 ? 350 : (compact ? 230 : 258), compact ? 12 : 14);",
    "slider width"))

def list_cards(s):
    for old, new, label in (
        ("lv_obj_set_width(listName, 280);",
         "lv_obj_set_width(listName, MIRA_DISPLAY_WIDTH >= 800 ? 364 : 280);",
         "list name"),
        ("lv_obj_set_width(b.control, compact ? 236 : 268);",
         "lv_obj_set_width(b.control, MIRA_DISPLAY_WIDTH >= 800 ? 350 : (compact ? 236 : 268));",
         "dropdown"),
        ("lv_obj_set_width(wrap, 280);",
         "lv_obj_set_width(wrap, MIRA_DISPLAY_WIDTH >= 800 ? 364 : 280);",
         "button group"),
        ("const int btnWidth = b.listCount <= 2 ? 134 : 88;",
         "const int btnWidth = MIRA_DISPLAY_WIDTH >= 800 ? (b.listCount <= 2 ? 170 : 110) : (b.listCount <= 2 ? 134 : 88);",
         "segmented buttons"),
        ("lv_obj_set_size(btn, compact ? 88 : 132, compact ? 34 : 38);",
         "lv_obj_set_size(btn, MIRA_DISPLAY_WIDTH >= 800 ? (compact ? 110 : 170) : (compact ? 88 : 132), compact ? 34 : 38);",
         "group buttons"),
    ):
        s = once(s, old, new, label)
    return s
source = within(source, "void MiraPanelUI::createList(",
                "void MiraPanelUI::createColor(", list_cards)

def date_header(s):
    for old, new, label in (
        ("lv_obj_set_width(b.card, 304);",
         "lv_obj_set_width(b.card, MIRA_CARD_FULL);", "clock card"),
        ("lv_obj_set_width(b.valueLabel, 304);",
         "lv_obj_set_width(b.valueLabel, MIRA_CARD_FULL);", "clock time"),
        ("lv_obj_set_width(_dateSubLabel, 300);",
         "lv_obj_set_width(_dateSubLabel, MIRA_DISPLAY_WIDTH >= 800 ? 780 : 300);",
         "clock subtitle"),
    ):
        s = once(s, old, new, label)
    return s
source = within(source, "void MiraPanelUI::createDateTime(",
                "void MiraPanelUI::createUnsupported(", date_header)
ui.write_text(source, encoding="utf-8")
print("Mira 0.6.40: Waveshare 4 info / 2 controls per row, WT32 unchanged")
