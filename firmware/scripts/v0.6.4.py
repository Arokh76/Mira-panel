from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
server = root / 'MiraPanelServer.cpp'
html = root / 'HTML' / 'HTML_Parametres.h'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


def insert_before_nth(path: Path, needle: str, insertion: str, nth: int, label: str):
    text = path.read_text(encoding='utf-8')
    positions = []
    start = 0
    while True:
        i = text.find(needle, start)
        if i < 0:
            break
        positions.append(i)
        start = i + len(needle)
    if len(positions) < nth:
        raise SystemExit(f'{label}: expected at least {nth} anchors in {path}, got {len(positions)}')
    pos = positions[nth - 1]
    path.write_text(text[:pos] + insertion + text[pos:], encoding='utf-8')


replace_once(ino, '"0.6.3"', '"0.6.4"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.3 initialise', '[MIRA] Firmware 0.6.4 initialise', 'startup log')

# Dedicated web endpoint for the system Display card. The historical
# /save?config=params path is designed for the dynamic HTTP-widget parameter
# list; using it for system display settings made time/checkbox persistence
# depend on hidden-field browser glue. Save these settings explicitly instead.
display_save_route = r'''  server.on("/display-save", HTTP_POST, [this](AsyncWebServerRequest* request) {
    auto postValue = [request](const char* name) -> String {
      AsyncWebParameter* p = request->getParam(name, true);
      if (!p) return String();
      String value = p->value();
      value.trim();
      return value;
    };

    auto normalizeClockValue = [](String value, String fallback) -> String {
      value.trim();
      value.replace("%3A", ":");
      value.replace("%3a", ":");
      if (value.length() == 4 && value.indexOf(':') < 0) {
        value = value.substring(0, 2) + ":" + value.substring(2, 4);
      }
      auto valid = [](const String& v) -> bool {
        if (v.length() != 5 || v[2] != ':') return false;
        if (!isDigit(v[0]) || !isDigit(v[1]) || !isDigit(v[3]) || !isDigit(v[4])) return false;
        int h = v.substring(0, 2).toInt();
        int m = v.substring(3, 5).toInt();
        return h >= 0 && h <= 23 && m >= 0 && m <= 59;
      };
      if (valid(value)) return value;
      fallback.trim();
      fallback.replace("%3A", ":");
      fallback.replace("%3a", ":");
      if (fallback.length() == 4 && fallback.indexOf(':') < 0) {
        fallback = fallback.substring(0, 2) + ":" + fallback.substring(2, 4);
      }
      return valid(fallback) ? fallback : String("00:00");
    };

    auto boundedInt = [](String value, int fallback, int minValue, int maxValue) -> int {
      value.trim();
      if (!value.length()) return fallback;
      for (size_t i = 0; i < value.length(); ++i) {
        if (!isDigit(value[i]) && !(i == 0 && value[i] == '-')) return fallback;
      }
      long parsed = value.toInt();
      if (parsed < minValue) parsed = minValue;
      if (parsed > maxValue) parsed = maxValue;
      return (int)parsed;
    };

    int currentDay = GetParamText("DISPLAY_Jour").toInt();
    int currentNight = GetParamText("DISPLAY_Nuit").toInt();
    int currentSleep = GetParamText("DISPLAY_Veille").toInt();
    int currentTimeout = GetParamText("DISPLAY_Delai").toInt();
    if (currentDay < 1 || currentDay > 100) currentDay = 85;
    if (currentNight < 1 || currentNight > 100) currentNight = 20;
    if (currentSleep < 0 || currentSleep > 100) currentSleep = 8;
    if (currentTimeout < 0 || currentTimeout > 86400) currentTimeout = 120;

    const int day = boundedInt(postValue("DISPLAY_Jour"), currentDay, 1, 100);
    const int night = boundedInt(postValue("DISPLAY_Nuit"), currentNight, 1, 100);
    const int sleepBrightness = boundedInt(postValue("DISPLAY_Veille"), currentSleep, 0, 100);
    const int sleepTimeout = boundedInt(postValue("DISPLAY_Delai"), currentTimeout, 0, 86400);
    const String start = normalizeClockValue(postValue("DISPLAY_DebutNuit"), GetParamText("DISPLAY_DebutNuit"));
    const String end = normalizeClockValue(postValue("DISPLAY_FinNuit"), GetParamText("DISPLAY_FinNuit"));
    const bool nightAuto = request->hasParam("DISPLAY_NuitAuto_Check", true);
    const bool clockSleep = request->hasParam("DISPLAY_HorlogeVeille_Check", true);

    auto updateTextIfChanged = [this](const char* name, const String& value) {
      if (GetParamText(name) != value) UpdateParamText(name, value);
    };
    auto updateBoolIfChanged = [this](const char* name, bool value) {
      if (GetParamBool(name) != value) UpdateParamBool(name, value);
    };

    updateTextIfChanged("DISPLAY_Jour", String(day));
    updateTextIfChanged("DISPLAY_Nuit", String(night));
    updateTextIfChanged("DISPLAY_Veille", String(sleepBrightness));
    updateTextIfChanged("DISPLAY_Delai", String(sleepTimeout));
    updateTextIfChanged("DISPLAY_DebutNuit", start);
    updateTextIfChanged("DISPLAY_FinNuit", end);
    updateBoolIfChanged("DISPLAY_NuitAuto", nightAuto);
    updateBoolIfChanged("DISPLAY_HorlogeVeille", clockSleep);

    Serial.printf("[MIRA][DISPLAY] Web save jour=%d%% nuit=%d%% veille=%d%% delai=%ds auto=%s %s-%s horloge=%s\n",
                  day, night, sleepBrightness, sleepTimeout,
                  nightAuto ? "oui" : "non", start.c_str(), end.c_str(),
                  clockSleep ? "oui" : "non");
    request->redirect("/load?nav=params");
  });

'''

# Two legacy /save registrations exist: setup mode first, normal web UI second.
insert_before_nth(
    server,
    '  server.on("/save", HTTP_POST, [](AsyncWebServerRequest* request) {',
    display_save_route,
    2,
    'normal display save route',
)

replace_once(
    html,
    '<form id="displayForm" action="/save?config=params" method="POST">',
    '<form id="displayForm" action="/display-save" method="POST">',
    'display dedicated form action',
)
replace_once(
    html,
    '<div class="field"><label for="DISPLAY_DebutNuit_UI">Début du mode nuit</label><input id="DISPLAY_DebutNuit_UI" type="time" value="%DISPLAYNIGHTSTART%" required><input id="DISPLAY_DebutNuit" name="DISPLAY_DebutNuit" type="hidden"></div>',
    '<div class="field"><label for="DISPLAY_DebutNuit">Début du mode nuit</label><input id="DISPLAY_DebutNuit" name="DISPLAY_DebutNuit" type="time" value="%DISPLAYNIGHTSTART%" required></div>',
    'native display start time post',
)
replace_once(
    html,
    '<div class="field"><label for="DISPLAY_FinNuit_UI">Fin du mode nuit</label><input id="DISPLAY_FinNuit_UI" type="time" value="%DISPLAYNIGHTEND%" required><input id="DISPLAY_FinNuit" name="DISPLAY_FinNuit" type="hidden"></div>',
    '<div class="field"><label for="DISPLAY_FinNuit">Fin du mode nuit</label><input id="DISPLAY_FinNuit" name="DISPLAY_FinNuit" type="time" value="%DISPLAYNIGHTEND%" required></div>',
    'native display end time post',
)
replace_once(
    html,
    '<input type="hidden" id="DISPLAY_NuitAuto" name="DISPLAY_NuitAuto" value="%DISPLAYNIGHTAUTO%"><input type="checkbox" %DISPLAYNIGHTCHECKED% onchange="document.getElementById(\'DISPLAY_NuitAuto\').value=this.checked?\'1\':\'0\'">',
    '<input id="DISPLAY_NuitAuto_Check" name="DISPLAY_NuitAuto_Check" type="checkbox" value="1" %DISPLAYNIGHTCHECKED%>',
    'display night auto checkbox post',
)
replace_once(
    html,
    '<input type="hidden" id="DISPLAY_HorlogeVeille" name="DISPLAY_HorlogeVeille" value="%DISPLAYCLOCKSLEEP%"><input type="checkbox" %DISPLAYCLOCKCHECKED% onchange="document.getElementById(\'DISPLAY_HorlogeVeille\').value=this.checked?\'1\':\'0\'">',
    '<input id="DISPLAY_HorlogeVeille_Check" name="DISPLAY_HorlogeVeille_Check" type="checkbox" value="1" %DISPLAYCLOCKCHECKED%>',
    'display clock sleep checkbox post',
)
replace_once(
    html,
    '''(function(){
  var form=document.getElementById('displayForm');
  if(!form)return;
  form.addEventListener('submit',function(){
    var start=document.getElementById('DISPLAY_DebutNuit_UI');
    var end=document.getElementById('DISPLAY_FinNuit_UI');
    document.getElementById('DISPLAY_DebutNuit').value=(start.value||'23:00').replace(':','');
    document.getElementById('DISPLAY_FinNuit').value=(end.value||'07:00').replace(':','');
  });
})();
''',
    '',
    'remove hidden-time submit shim',
)

print('Mira Panel 0.6.4 dedicated Display web persistence applied')
