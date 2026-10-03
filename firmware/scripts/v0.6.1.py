from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'
mqtt_h = root / 'MiraMqtt.h'
mqtt = root / 'MiraMqtt.cpp'
server = root / 'MiraPanelServer.cpp'
html = root / 'HTML' / 'HTML_Parametres.h'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


# -----------------------------------------------------------------------------
# Version
# -----------------------------------------------------------------------------
replace_once(ino, '"0.6.0"', '"0.6.1"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.0 initialise', '[MIRA] Firmware 0.6.1 initialise', 'startup log')

# -----------------------------------------------------------------------------
# Stability: 0.6.0 rebuilt a large String signature on every MQTT loop pass.
# On the ESP32 this creates needless heap churn and can progressively make the
# local settings UI lag before a watchdog/reset. Check display states only once
# per second unless a command explicitly forces an immediate publication.
# -----------------------------------------------------------------------------
replace_once(
    mqtt_h,
    '  String _lastDisplayStateSignature;\n',
    '  String _lastDisplayStateSignature;\n  uint32_t _lastDisplayStateCheckMs = 0;\n',
    'mqtt display state throttle field',
)

replace_once(
    mqtt,
    '''void MiraMqtt::publishDisplayStates(bool force) {
  if (!_connected || !_net.connected()) return;
  String signature = String(_ui.displayDayBrightness()) + "|" + String(_ui.displayNightBrightness()) + "|" +''',
    '''void MiraMqtt::publishDisplayStates(bool force) {
  if (!_connected || !_net.connected()) return;
  const uint32_t now = millis();
  if (!force && (uint32_t)(now - _lastDisplayStateCheckMs) < 1000UL) return;
  _lastDisplayStateCheckMs = now;
  String signature = String(_ui.displayDayBrightness()) + "|" + String(_ui.displayNightBrightness()) + "|" +''',
    'mqtt display state throttle',
)

# The web configuration does not need to be reread from the server eight times
# every two seconds. Five seconds keeps web changes responsive while reducing
# allocations and JSON/config access during normal LVGL use.
replace_once(
    ui,
    '  if (!force && millis() - _lastDisplayConfigCheck < 2000UL) return;',
    '  if (!force && millis() - _lastDisplayConfigCheck < 5000UL) return;',
    'display config polling interval',
)

# -----------------------------------------------------------------------------
# Schedule persistence: some builds of the historical generic /save handler do
# not round-trip ':' reliably in parameter values. Store web times as HHMM and
# normalize back to HH:MM inside the UI and template renderer. This also repairs
# already stored values such as 23%3A00 if one was produced by the old form.
# -----------------------------------------------------------------------------
replace_once(
    server,
    '  if (var == "DISPLAYNIGHTSTART") return GetLocalParamValue("DISPLAY_DebutNuit");',
    '''  if (var == "DISPLAYNIGHTSTART") {
    String value = GetLocalParamValue("DISPLAY_DebutNuit");
    value.trim();
    value.replace("%3A", ":");
    value.replace("%3a", ":");
    if (value.length() == 4 && value.indexOf(':') < 0) value = value.substring(0, 2) + ":" + value.substring(2);
    return value;
  }''',
    'display night start template normalization',
)
replace_once(
    server,
    '  if (var == "DISPLAYNIGHTEND") return GetLocalParamValue("DISPLAY_FinNuit");',
    '''  if (var == "DISPLAYNIGHTEND") {
    String value = GetLocalParamValue("DISPLAY_FinNuit");
    value.trim();
    value.replace("%3A", ":");
    value.replace("%3a", ":");
    if (value.length() == 4 && value.indexOf(':') < 0) value = value.substring(0, 2) + ":" + value.substring(2);
    return value;
  }''',
    'display night end template normalization',
)

replace_once(
    ui,
    '''String MiraPanelUI::normalizeClock(const String& value, const String& fallback) {
  String v = value;
  v.trim();
  return parseClockMinutes(v) >= 0 ? v : fallback;
}''',
    '''String MiraPanelUI::normalizeClock(const String& value, const String& fallback) {
  String v = value;
  v.trim();
  v.replace("%3A", ":");
  v.replace("%3a", ":");
  if (v.length() == 4 && v.indexOf(':') < 0) v = v.substring(0, 2) + ":" + v.substring(2);
  return parseClockMinutes(v) >= 0 ? v : fallback;
}''',
    'clock normalization',
)

replace_once(
    ui,
    '''  } else if (k == "night_start") {
    String v = normalizeClock(p, _nightStart);
    _server.UpdateParamText("DISPLAY_DebutNuit", v);
  } else if (k == "night_end") {
    String v = normalizeClock(p, _nightEnd);
    _server.UpdateParamText("DISPLAY_FinNuit", v);''',
    '''  } else if (k == "night_start") {
    String v = normalizeClock(p, _nightStart);
    String compact = v.substring(0, 2) + v.substring(3, 5);
    _server.UpdateParamText("DISPLAY_DebutNuit", compact);
  } else if (k == "night_end") {
    String v = normalizeClock(p, _nightEnd);
    String compact = v.substring(0, 2) + v.substring(3, 5);
    _server.UpdateParamText("DISPLAY_FinNuit", compact);''',
    'mqtt schedule compact storage',
)

# Keep the browser's native time picker, but submit a hidden HHMM value so the
# historical parameter saver never has to persist a colon.
replace_once(
    html,
    '<form action="/save?config=params" method="POST">',
    '<form id="displayForm" action="/save?config=params" method="POST">',
    'display form id',
)
replace_once(
    html,
    '<div class="field"><label for="DISPLAY_DebutNuit">Début du mode nuit</label><input id="DISPLAY_DebutNuit" name="DISPLAY_DebutNuit" type="time" value="%DISPLAYNIGHTSTART%" required></div>',
    '<div class="field"><label for="DISPLAY_DebutNuit_UI">Début du mode nuit</label><input id="DISPLAY_DebutNuit_UI" type="time" value="%DISPLAYNIGHTSTART%" required><input id="DISPLAY_DebutNuit" name="DISPLAY_DebutNuit" type="hidden"></div>',
    'display start time hidden storage',
)
replace_once(
    html,
    '<div class="field"><label for="DISPLAY_FinNuit">Fin du mode nuit</label><input id="DISPLAY_FinNuit" name="DISPLAY_FinNuit" type="time" value="%DISPLAYNIGHTEND%" required></div>',
    '<div class="field"><label for="DISPLAY_FinNuit_UI">Fin du mode nuit</label><input id="DISPLAY_FinNuit_UI" type="time" value="%DISPLAYNIGHTEND%" required><input id="DISPLAY_FinNuit" name="DISPLAY_FinNuit" type="hidden"></div>',
    'display end time hidden storage',
)
replace_once(
    html,
    '''<script>
function ClickToogle(box,element){document.getElementById(element).value=box.checked?'1':'0';var t=document.getElementById('Value'+element);if(t)t.textContent=box.checked?'Actif':'Inactif';}''',
    '''<script>
(function(){
  var form=document.getElementById('displayForm');
  if(!form)return;
  form.addEventListener('submit',function(){
    var start=document.getElementById('DISPLAY_DebutNuit_UI');
    var end=document.getElementById('DISPLAY_FinNuit_UI');
    document.getElementById('DISPLAY_DebutNuit').value=(start.value||'23:00').replace(':','');
    document.getElementById('DISPLAY_FinNuit').value=(end.value||'07:00').replace(':','');
  });
})();
function ClickToogle(box,element){document.getElementById(element).value=box.checked?'1':'0';var t=document.getElementById('Value'+element);if(t)t.textContent=box.checked?'Actif':'Inactif';}''',
    'display schedule submit normalization',
)

print('Mira Panel 0.6.1 display stability + schedule persistence fix applied')
