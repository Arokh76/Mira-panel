from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: diag_0621.py <source-root> <test1|test2|test3>")

root = Path(sys.argv[1])
variant = sys.argv[2]
if variant not in {"test1", "test2", "test3"}:
    raise SystemExit(f"unknown variant: {variant}")

ino = root / "MIRA_PANEL.ino"
ui = root / "MiraPanelUI.cpp"
hdr = root / "MiraPanelUI.h"
profile = root / "MiraDisplayProfile.h"
server = root / "MiraPanelServer.cpp"

def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly 1 match in {path}, got {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")

version = {
    "test1": "0.6.21-t1",
    "test2": "0.6.21-t2",
    "test3": "0.6.21-t3",
}[variant]

replace_once(ino, '"0.6.20"', f'"{version}"', "firmware version")
replace_once(
    ino,
    "[MIRA] Firmware 0.6.20 initialise",
    f"[MIRA] Firmware {version} initialise",
    "startup log",
)

# TEST 1: only the bulk HTTP programming route added by 0.6.21.
anchor = '  server.on("/displayinfo", HTTP_GET, [](AsyncWebServerRequest* request) {\n'
bulk = r'''  server.on("/programui", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("key", true) || request->getParam("key", true)->value() != Uncrypt_Pass) {
      request->send(200, "text/plain", "Clé invalide");
      return;
    }
    if (!request->hasParam("json", true)) {
      request->send(400, "text/plain", "Configuration absente");
      return;
    }

    String raw = request->getParam("json", true)->value();
    DynamicJsonDocument incoming(16384);
    DeserializationError err = deserializeJson(incoming, raw);
    if (err || !incoming["lvgl"].is<JsonArray>()) {
      request->send(400, "text/plain", "Configuration invalide");
      return;
    }

    mira_Json_LVGL.clear();
    mira_Json_LVGL.set(incoming);
    if (mira_Json_LVGL.overflowed()) {
      request->send(413, "text/plain", "Configuration trop grande");
      return;
    }

    NB_List_lvgl = incoming["lvgl"].size();
    if (NB_List_lvgl == 0) NB_List_lvgl = 1;
    SaveListLVGL();
    request->send(200, "text/plain", "OK");
  });

'''
text = server.read_text(encoding="utf-8")
if bulk.strip() not in text:
    if text.count(anchor) != 1:
        raise SystemExit("programui anchor not found")
    server.write_text(text.replace(anchor, bulk + anchor, 1), encoding="utf-8")

# TEST 2: add only DMA synchronization on top of test1.
if variant in {"test2", "test3"}:
    replace_once(
        ui,
        '''  _displayForLvgl->pushImageDMA(
    area->x1,
    area->y1,
    area->x2 - area->x1 + 1,
    area->y2 - area->y1 + 1,
    (lgfx::swap565_t*)&color_p->full
  );

  lv_disp_flush_ready(disp);
''',
        '''  _displayForLvgl->pushImageDMA(
    area->x1,
    area->y1,
    area->x2 - area->x1 + 1,
    area->y2 - area->y1 + 1,
    (lgfx::swap565_t*)&color_p->full
  );
  _displayForLvgl->waitDMA();

  lv_disp_flush_ready(disp);
''',
        "DMA flush synchronization",
    )

# TEST 3: add the 32-line draw buffers on top of test2.
if variant == "test3":
    text = profile.read_text(encoding="utf-8")
    if "#define MIRA_DRAW_BUFFER_LINES" not in text:
        text += "\n#define MIRA_DRAW_BUFFER_LINES 32\n"
    profile.write_text(text, encoding="utf-8")

    replace_once(
        ui,
        "lv_color_t MiraPanelUI::_buf1[320 * 20];\nlv_color_t MiraPanelUI::_buf2[320 * 20];\n",
        "lv_color_t MiraPanelUI::_buf1[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];\nlv_color_t MiraPanelUI::_buf2[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];\n",
        "draw buffer definitions",
    )
    replace_once(
        hdr,
        "  static lv_color_t _buf1[MIRA_DISPLAY_WIDTH * 20];\n  static lv_color_t _buf2[MIRA_DISPLAY_WIDTH * 20];\n",
        "  static lv_color_t _buf1[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];\n  static lv_color_t _buf2[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];\n",
        "draw buffer arrays",
    )
    replace_once(
        ui,
        "  lv_disp_draw_buf_init(&_drawBuffer, _buf1, _buf2, 320 * 20);\n",
        "  lv_disp_draw_buf_init(&_drawBuffer, _buf1, _buf2, MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES);\n",
        "draw buffer init",
    )

print(f"Built diagnostic delta {version}: programui={'yes'}, waitDMA={'yes' if variant in {'test2','test3'} else 'no'}, buffers32={'yes' if variant == 'test3' else 'no'}")
