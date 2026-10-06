from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'
hdr = root / 'MiraPanelUI.h'
server = root / 'MiraPanelServer.cpp'
profile = root / 'MiraDisplayProfile.h'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


def ensure_include(path: Path, include_line: str):
    text = path.read_text(encoding='utf-8')
    if include_line in text:
        return
    lines = text.splitlines(True)
    insert_at = 0
    for i, line in enumerate(lines):
        if line.startswith('#include'):
            insert_at = i + 1
    lines.insert(insert_at, include_line + '\n')
    path.write_text(''.join(lines), encoding='utf-8')


# 0.6.18 is deliberately a no-visual-change foundation release:
# display geometry moves into one hardware profile, and Jeedom can discover it over HTTP.
replace_once(ino, '"0.6.17"', '"0.6.18"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.17 initialise', '[MIRA] Firmware 0.6.18 initialise', 'startup log')

profile.write_text(r'''#pragma once

// Mira hardware display profile.
// The UI engine consumes these values instead of hard-coding one panel geometry.
// A future board only needs its own profile + display/touch driver glue.
#define MIRA_DISPLAY_PROFILE_ID "wt32-sc01-plus"
#define MIRA_DISPLAY_WIDTH 320
#define MIRA_DISPLAY_HEIGHT 480
#define MIRA_NAV_HEIGHT 46
#define MIRA_CONTENT_HEIGHT (MIRA_DISPLAY_HEIGHT - MIRA_NAV_HEIGHT)
#define MIRA_DISPLAY_ROTATION 0

// Transport-independent logical canvas used by the future visual designer.
// Jeedom may edit in pixels, but persisted free-layout geometry can be normalized
// to this range so layouts can be adapted to another resolution.
#define MIRA_LAYOUT_LOGICAL_WIDTH 1000
#define MIRA_LAYOUT_LOGICAL_HEIGHT 1000
''', encoding='utf-8')

ensure_include(hdr, '#include "MiraDisplayProfile.h"')
ensure_include(server, '#include "MiraDisplayProfile.h"')

# LVGL/display geometry now comes from the profile. Existing WT32 behaviour is identical.
replacements = [
    ('  dispDrv.hor_res = 320;\n', '  dispDrv.hor_res = MIRA_DISPLAY_WIDTH;\n', 'display width'),
    ('  dispDrv.ver_res = 480;\n', '  dispDrv.ver_res = MIRA_DISPLAY_HEIGHT;\n', 'display height'),
    ('    lv_obj_set_size(_pages[i], 320, 434);\n', '    lv_obj_set_size(_pages[i], MIRA_DISPLAY_WIDTH, MIRA_CONTENT_HEIGHT);\n', 'page size'),
    ('  lv_obj_set_pos(_nav, 0, 434);\n', '  lv_obj_set_pos(_nav, 0, MIRA_CONTENT_HEIGHT);\n', 'nav y'),
    ('  lv_obj_set_size(_nav, 320, 46);\n', '  lv_obj_set_size(_nav, MIRA_DISPLAY_WIDTH, MIRA_NAV_HEIGHT);\n', 'nav size'),
    ('  lv_obj_set_size(_sleepOverlay, 320, 480);\n', '  lv_obj_set_size(_sleepOverlay, MIRA_DISPLAY_WIDTH, MIRA_DISPLAY_HEIGHT);\n', 'sleep overlay size'),
]
for old, new, label in replacements:
    replace_once(ui, old, new, label)

replace_once(
    hdr,
    '  static lv_color_t _buf1[320 * 20];\n  static lv_color_t _buf2[320 * 20];\n',
    '  static lv_color_t _buf1[MIRA_DISPLAY_WIDTH * 20];\n  static lv_color_t _buf2[MIRA_DISPLAY_WIDTH * 20];\n',
    'draw buffers',
)

# HTTP remains Mira's primary Jeedom transport. This endpoint lets the plugin
# discover the actual screen profile instead of assuming 320x480.
anchor = '''  server.on("/getlvgl", HTTP_GET, [](AsyncWebServerRequest* request) {
'''
route = r'''  server.on("/displayinfo", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("key") || request->getParam("key")->value() != Uncrypt_Pass) {
      request->send(200, "text/plain", "Clé invalide");
      return;
    }

    DynamicJsonDocument displayInfo(384);
    displayInfo["profile"] = MIRA_DISPLAY_PROFILE_ID;
    displayInfo["width"] = MIRA_DISPLAY_WIDTH;
    displayInfo["height"] = MIRA_DISPLAY_HEIGHT;
    displayInfo["contentWidth"] = MIRA_DISPLAY_WIDTH;
    displayInfo["contentHeight"] = MIRA_CONTENT_HEIGHT;
    displayInfo["navHeight"] = MIRA_NAV_HEIGHT;
    displayInfo["rotation"] = MIRA_DISPLAY_ROTATION;
    displayInfo["logicalWidth"] = MIRA_LAYOUT_LOGICAL_WIDTH;
    displayInfo["logicalHeight"] = MIRA_LAYOUT_LOGICAL_HEIGHT;
    displayInfo["httpPrimary"] = true;
    displayInfo["mqttOptional"] = true;
    displayInfo["freeLayout"] = false;

    String Retour_Json = "";
    serializeJson(displayInfo, Retour_Json);
    request->send(200, "application/json", Retour_Json);
  });

'''
text = server.read_text(encoding='utf-8')
if route.strip() not in text:
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f'displayinfo route anchor: expected 1 match, got {count}')
    server.write_text(text.replace(anchor, route + anchor, 1), encoding='utf-8')

print('Mira Panel 0.6.18 generic display profile + HTTP display discovery applied')
