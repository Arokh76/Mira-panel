"""Mira 0.6.50 — separate the widget display label from its stable technical name.

The technical "name" remains untouched everywhere except on LVGL's visible
title labels. The optional "displayName" survives SPIFFS and /getlvgl.
This script intentionally fails closed if source anchors drift.
"""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
spiffs = root / "SPIFFS_Process.h"
server = root / "MiraPanelServer.cpp"
hdr = root / "MiraPanelUI.h"
ui = root / "MiraPanelUI.cpp"
ino = root / "MIRA_PANEL.ino"

def replace_exact(path, old, new, n, what):
    text = path.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual != n:
        raise SystemExit(f"0.6.50 {what}: expected {n} occurrences, found {actual}")
    path.write_text(text.replace(old, new), encoding="utf-8")

replace_exact(spiffs,
              'String LVGL_Icon = "auto";\n',
              'String LVGL_Icon = "auto";\nString LVGL_DisplayName = "";\n',
              1, "display label state")
replace_exact(spiffs,
              '  lvgl_array["icon"] = LVGL_Icon;\n',
              '  lvgl_array["icon"] = LVGL_Icon;\n  lvgl_array["displayName"] = LVGL_DisplayName;\n',
              1, "persist display label")

# Both firmware programming endpoints share this parsing block. The stored
# identifier "name" must NEVER be overwritten by this optional field.
replace_exact(server,
              '                    if (LVGL_Icon.length() > 24) LVGL_Icon = "auto";\n',
              '''                    if (LVGL_Icon.length() > 24) LVGL_Icon = "auto";
                    LVGL_DisplayName = request->hasParam("displayName") ?
                        request->getParam("displayName")->value() : "";
                    if (LVGL_DisplayName.length() > 96) LVGL_DisplayName = LVGL_DisplayName.substring(0, 96);
''', 2, "POST display label parsing")

replace_exact(hdr,
              '    String icon = "auto";\n',
              '    String icon = "auto";\n    String displayName = "";\n',
              1, "binding display label")
replace_exact(ui,
              '  String widgetIcon = obj["icon"] | "auto";\n',
              '  String widgetIcon = obj["icon"] | "auto";\n  String widgetDisplayName = obj["displayName"] | "";\n',
              1, "read optional JSON label")
replace_exact(ui,
              '  b.icon = widgetIcon;\n',
              '  b.icon = widgetIcon;\n  b.displayName = widgetDisplayName;\n',
              1, "bind optional label")

# Change only text rendered by LVGL; retain b.name in networking, routing,
# MQTT, getindex and stable Jeedom command mappings. A missing displayName
# naturally falls back to the old technical name (backward compatibility).
code = ui.read_text(encoding="utf-8")
pattern = re.compile(r'(lv_label_set_text(?:_fmt)?\s*\([^;]{0,500}?)b\.name\.c_str\(\)', re.S)
code, changed = pattern.subn(
    lambda m: m.group(1) + '(b.displayName.length() ? b.displayName : b.name).c_str()',
    code)
if changed < 1:
    raise SystemExit("0.6.50: no LVGL name label usage found; inspect rendering before release")

# Some widget titles use makeNameLabel rather than direct lv_label_set_text.
# The Info card constructs a temporary displayName string locally, too.
render = "(b.displayName.length() ? b.displayName : b.name)"
sites = [
    ("  String displayName = b.name;", "  String displayName = " + render + ";", 1),
    ("makeNameLabel(b.card, b.name);", "makeNameLabel(b.card, " + render + ");", 1),
    ("makeNameLabel(top, b.name);", "makeNameLabel(top, " + render + ");", 1),
]
for before, after, count in sites:
    got = code.count(before)
    if got != count:
        raise SystemExit(f"0.6.50 title anchor: expected {count}, found {got}: {before}")
    code = code.replace(before, after)
print(f"0.6.50: additional {sum(n for _, _, n in sites)} title sites")
ui.write_text(code, encoding="utf-8")
print(f"0.6.50: updated {changed} LVGL name label(s); technical identities preserved")

replace_exact(ino, '"0.6.49"', '"0.6.50"', 1, "firmware version")
# Keep the established startup message consistent when present.
code = ino.read_text(encoding="utf-8")
code = code.replace("[MIRA] Firmware 0.6.49 initialise",
                    "[MIRA] Firmware 0.6.50 initialise")
ino.write_text(code, encoding="utf-8")
