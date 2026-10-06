from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'
hdr = root / 'MiraPanelUI.h'
profile = root / 'MiraDisplayProfile.h'
server = root / 'MiraPanelServer.cpp'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


replace_once(ino, '"0.6.20"', '"0.6.21"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.20 initialise', '[MIRA] Firmware 0.6.21 initialise', 'startup log')

# Larger generic draw chunks reduce the number of LVGL flushes on a full redraw.
text = profile.read_text(encoding='utf-8')
if '#define MIRA_DRAW_BUFFER_LINES' not in text:
    text += '\n#define MIRA_DRAW_BUFFER_LINES 32\n'
profile.write_text(text, encoding='utf-8')

replace_once(
    ui,
    'lv_color_t MiraPanelUI::_buf1[320 * 20];\\nlv_color_t MiraPanelUI::_buf2[320 * 20];\\n',
    'lv_color_t MiraPanelUI::_buf1[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];\\nlv_color_t MiraPanelUI::_buf2[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];\\n',
    'draw buffer definitions',
)

replace_once(
    hdr,
    '  static lv_color_t _buf1[MIRA_DISPLAY_WIDTH * 20];\n  static lv_color_t _buf2[MIRA_DISPLAY_WIDTH * 20];\n',
    '  static lv_color_t _buf1[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];\n  static lv_color_t _buf2[MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES];\n',
    'draw buffer arrays',
)
replace_once(
    ui,
    '  lv_disp_draw_buf_init(&_drawBuffer, _buf1, _buf2, 320 * 20);\n',
    '  lv_disp_draw_buf_init(&_drawBuffer, _buf1, _buf2, MIRA_DISPLAY_WIDTH * MIRA_DRAW_BUFFER_LINES);\n',
    'draw buffer init',
)

# 0.6.20 exposed an old DMA race: once fades were removed, LVGL could reuse a
# draw buffer while pushImageDMA() was still reading it. That made page changes
# look as if the bottom navigation did nothing. Wait for DMA completion before
# telling LVGL the buffer is free.
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
    'DMA flush synchronization',
)

# Bulk HTTP programming: one request replaces N sequential add-widget requests.
# HTTP remains the primary Jeedom transport.
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

    // SaveListLVGL keeps the same runtime reload/event path as legacy programming.
    NB_List_lvgl = incoming["lvgl"].size();
    if (NB_List_lvgl == 0) NB_List_lvgl = 1;
    SaveListLVGL();
    request->send(200, "text/plain", "OK");
  });

'''
text = server.read_text(encoding='utf-8')
if bulk.strip() not in text:
    if text.count(anchor) != 1:
        raise SystemExit('programui anchor not found')
    server.write_text(text.replace(anchor, bulk + anchor, 1), encoding='utf-8')

print('Mira Panel 0.6.21 applied: DMA-safe navigation, faster redraws, bulk HTTP UI programming')
