"""Mira 0.6.41: repair settings panels overflowing on the Waveshare.

Only settings presentation is changed. WT32 dimensions and control callbacks
stay exactly as on firmware 0.6.40.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
ui = root / "MiraPanelUI.cpp"
ino = root / "MIRA_PANEL.ino"

def replace_once(text, before, after, what):
    n = text.count(before)
    if n != 1:
        raise SystemExit(f"0.6.41 {what}: expected 1 occurrence, found {n}")
    return text.replace(before, after, 1)

main = ino.read_text(encoding="utf-8")
main = replace_once(main, '"0.6.40"', '"0.6.41"', "version")
main = replace_once(main, "[MIRA] Firmware 0.6.40 initialise",
                    "[MIRA] Firmware 0.6.41 initialise", "startup")
ino.write_text(main, encoding="utf-8")

source = ui.read_text(encoding="utf-8")
a = source.index("lv_obj_t* MiraPanelUI::makeSettingsCard(")
b = source.index("void MiraPanelUI::makeNameLabel(", a)
settings_helpers = source[a:b]
settings_helpers = replace_once(
    settings_helpers, "lv_obj_set_width(card, 304);",
    "lv_obj_set_width(card, MIRA_DISPLAY_WIDTH >= 800 ? 640 : 304);",
    "settings card width",
)
settings_helpers = replace_once(
    settings_helpers, "lv_obj_set_width(row, MIRA_DISPLAY_WIDTH >= 800 ? 366 : 284);",
    # A 366px row inside the old 304px panel caused both-sided clipping.
    # Use the actual inner width of whichever widget or settings card owns it.
    "lv_obj_set_width(row, MIRA_DISPLAY_WIDTH >= 800 ? lv_pct(100) : 284);",
    "settings/control row width",
)
source = source[:a] + settings_helpers + source[b:]

a = source.index("void MiraPanelUI::buildSettingsPage(")
b = source.index("void MiraPanelUI::applyTheme(", a)
page = source[a:b]
page = replace_once(
    page, "lv_obj_set_width(title, 286);",
    "lv_obj_set_width(title, MIRA_DISPLAY_WIDTH >= 800 ? 620 : 286);",
    "settings title",
)
page = replace_once(
    page, "lv_obj_set_size(_brightnessSlider, 280, 14);",
    "lv_obj_set_size(_brightnessSlider, MIRA_DISPLAY_WIDTH >= 800 ? 600 : 280, 14);",
    "brightness slider",
)
page = replace_once(
    page, "lv_obj_set_width(_themeDropdown, 126);",
    "lv_obj_set_width(_themeDropdown, MIRA_DISPLAY_WIDTH >= 800 ? 205 : 126);",
    "theme dropdown",
)
source = source[:a] + page + source[b:]
ui.write_text(source, encoding="utf-8")
print("Mira 0.6.41: settings width 640px, row 100% parent, WT32 unchanged")
