"""Mira 0.6.49: restore white home clocks and align the Waveshare date.

Hardware photos (2026-10-11) show the new Waveshare Montserrat clock
left-aligned while the date is centered below it. Both HOME times have the
theme cyan instead of original white. Keep fonts, tiles, sleep and status.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ino, ui = (root/n for n in ("MIRA_PANEL.ino", "MiraPanelUI.cpp"))

def one(s, old, new, label):
    n=s.count(old)
    if n != 1:
        raise SystemExit(f"0.6.49 {label}: expected one match, got {n}")
    return s.replace(old,new,1)

s=ino.read_text(encoding="utf-8")
s=one(s,'"0.6.48"','"0.6.49"',"version")
s=one(s,"[MIRA] Firmware 0.6.48 initialise","[MIRA] Firmware 0.6.49 initialise","startup")
ino.write_text(s,encoding="utf-8")

s=ui.read_text(encoding="utf-8")
start=s.index("void MiraPanelUI::createDateTime(")
end=s.index("void MiraPanelUI::createUnsupported(", start)
block=s[start:end]

# The home time and date MUST share precisely the same horizontal box.
# LVGL text alignment remains independent of the label's X/Y alignment.
block=one(block, """    lv_obj_set_width(_dateTimeLabel, 390);
""", """    lv_obj_set_width(_dateTimeLabel, 370);
""", "same clock/date box width")
block=one(block, """    lv_obj_set_style_text_color(_dateTimeLabel, colorAccent(), 0);
""", """    lv_obj_set_style_text_color(_dateTimeLabel, lv_color_white(), 0);
""", "white waveshare clock")
block=one(block, """    lv_obj_set_style_text_align(_dateTimeLabel, LV_TEXT_ALIGN_LEFT, 0);
""", """    lv_obj_set_style_text_align(_dateTimeLabel, LV_TEXT_ALIGN_CENTER, 0);
""", "horizontal clock centering")

# Native WT32 clock stays the same 40px font and position, but receives
# its original white text rather than the cyan theme accent.
anchor="lv_obj_set_style_text_font(b.valueLabel, &lv_font_montserrat_40, 0);"
if block.count(anchor)!=1:
    # Fail closed rather than accidentally modifying a sensor tile.
    raise SystemExit(f"0.6.49 WT32 clock font anchor count: {block.count(anchor)}")
block=one(block,anchor,anchor+"\n  lv_obj_set_style_text_color(b.valueLabel, lv_color_white(), 0);",
          "WT32 clock white")
s=s[:start]+block+s[end:]
ui.write_text(s,encoding="utf-8")
print("Mira 0.6.49: white HOME clocks; Wave clock/date horizontally centered together; sleep unchanged")
