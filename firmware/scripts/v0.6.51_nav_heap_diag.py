"""0.6.51 diagnostic only: heap health during top-menu web navigation.

There are NO changes to widget events, HTTP programming, Jeedom actions,
MQTT, or the display driver. A lightweight /mira-health HTTP endpoint reports
the navigation count, free/minimum heap and largest available block.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("firmware/source/work/MIRA_PANEL")
server = root / "MiraPanelServer.cpp"
ino = root / "MIRA_PANEL.ino"

def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"0.6.51 {label}: expected 1 anchor, got {count}")
    return text.replace(old, new, 1)

s = server.read_text(encoding="utf-8")
s = once(s, '#include <Update.h>\n',
         '#include <Update.h>\n#include <esp_heap_caps.h>\n',
         "heap info include")

diagnostics = r"""
// Diagnostics for UI navigation saturation. Counts only real page loads,
// not polling; this intentionally does not allocate a JsonDocument or use
// the heavy HTML response templating path.
static volatile uint32_t miraNavCount = 0;
static volatile uint32_t miraNavLastHeap = 0;
static volatile uint32_t miraNavLowestHeap = 0xffffffffU;

static void miraObserveNavigation(const char* page) {
  ++miraNavCount;
  const uint32_t heap = ESP.getFreeHeap();
  miraNavLastHeap = heap;
  if (heap < miraNavLowestHeap) miraNavLowestHeap = heap;
  Serial.printf("[MIRA][WEB] %s nav=%u heap=%u min=%u largest=%u\n",
                page, (unsigned)miraNavCount, (unsigned)heap,
                (unsigned)ESP.getMinFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
"""
s = once(s, "void MiraPanelServer::ServeurStation() {",
         diagnostics + "\nvoid MiraPanelServer::ServeurStation() {",
         "station observation helpers")

station = 'void MiraPanelServer::ServeurStation() {\n'
endpoint = r"""  server.on("/mira-health", HTTP_GET, [](AsyncWebServerRequest* request) {
    char body[288];
    snprintf(body, sizeof(body),
      "{\"uptime_ms\":%lu,\"wifi\":%d,\"rssi_dbm\":%d,\"heap_free\":%u,"
      "\"heap_min\":%u,\"heap_largest_block\":%u,\"psram_free\":%u,"
      "\"nav_count\":%u,\"nav_last_heap\":%u,\"nav_lowest_heap\":%u}",
      (unsigned long)millis(), (int)WiFi.status(), (int)WiFi.RSSI(),
      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
      (unsigned)ESP.getFreePsram(), (unsigned)miraNavCount,
      (unsigned)miraNavLastHeap, (unsigned)miraNavLowestHeap);
    AsyncWebServerResponse* response =
        request->beginResponse(200, "application/json", body);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

"""
s = once(s, station, station+endpoint, "health route")
root_anchor = '''  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    char CPass[50];
    Uncrypt_Pass.toCharArray(CPass, 50);
    if (!request->authenticate(Ident_ID, CPass))
      return request->requestAuthentication();
    request->send_P(200, "text/html", Page_Index, RetourVariable);
  });'''
root_new = root_anchor.replace('    request->send_P(200, "text/html", Page_Index, RetourVariable);',
                               '    miraObserveNavigation("accueil");\n    request->send_P(200, "text/html", Page_Index, RetourVariable);')
s = once(s, root_anchor, root_new, "home menu page")
menu_anchor='''  server.on("/load", HTTP_GET, [](AsyncWebServerRequest* request) {
    char CPass[50];
    Uncrypt_Pass.toCharArray(CPass, 50);
    if (!request->authenticate(Ident_ID, CPass))
      return request->requestAuthentication();
    AsyncWebParameter* p = request->getParam(0);'''
menu_new = menu_anchor.replace('    AsyncWebParameter* p = request->getParam(0);',
                               '''    String page = request->hasParam("nav") ? request->getParam("nav")->value() : "?";
    miraObserveNavigation(page.c_str());
    AsyncWebParameter* p = request->getParam(0);''')
s = once(s, menu_anchor, menu_new, "top navigation page")
server.write_text(s, encoding="utf-8")

i = ino.read_text(encoding="utf-8")
i = once(i, '"0.6.50"', '"0.6.51"', "firmware version")
i = i.replace('[MIRA] Firmware 0.6.50 initialise', '[MIRA] Firmware 0.6.51 initialise')
ino.write_text(i, encoding="utf-8")
print("0.6.51: passive web navigation and heap diagnostics enabled")
