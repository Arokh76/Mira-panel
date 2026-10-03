from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui_h = root / 'MiraPanelUI.h'
ui = root / 'MiraPanelUI.cpp'
mqtt_h = root / 'MiraMqtt.h'
mqtt = root / 'MiraMqtt.cpp'
server = root / 'MiraPanelServer.cpp'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


def insert_before_once(path: Path, needle: str, insertion: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(needle)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 anchor in {path}, got {count}')
    path.write_text(text.replace(needle, insertion + needle, 1), encoding='utf-8')


def replace_function(path: Path, signature: str, new_function: str, label: str):
    text = path.read_text(encoding='utf-8')
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f'{label}: signature not found in {path}')
    brace = text.find('{', start)
    if brace < 0:
        raise SystemExit(f'{label}: opening brace not found in {path}')
    depth = 0
    end = -1
    for i in range(brace, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end < 0:
        raise SystemExit(f'{label}: closing brace not found in {path}')
    path.write_text(text[:start] + new_function.rstrip() + text[end:], encoding='utf-8')


def replace_in_function(path: Path, signature: str, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f'{label}: function not found in {path}')
    brace = text.find('{', start)
    depth = 0
    end = -1
    for i in range(brace, len(text)):
        if text[i] == '{': depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    func = text[start:end]
    if func.count(old) != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in function, got {func.count(old)}')
    func = func.replace(old, new, 1)
    path.write_text(text[:start] + func + text[end:], encoding='utf-8')


# -----------------------------------------------------------------------------
# Version + persistent web parameters
# -----------------------------------------------------------------------------
replace_once(ino, '"0.5.13"', '"0.6.0"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.13 initialise', '[MIRA] Firmware 0.6.0 initialise', 'startup log')

replace_once(
    ino,
    '  Serveur.AddParamText("MQTT_Mot_de_passe", "", true);',
    '''  Serveur.AddParamText("MQTT_Mot_de_passe", "", true);

  // Affichage autonome Mira Panel. Les valeurs sont aussi exposees en MQTT.
  Serveur.AddParamText("DISPLAY_Jour", "85");
  Serveur.AddParamText("DISPLAY_Nuit", "20");
  Serveur.AddParamText("DISPLAY_Veille", "8");
  Serveur.AddParamText("DISPLAY_Delai", "120");
  Serveur.AddParamBool("DISPLAY_NuitAuto", false);
  Serveur.AddParamText("DISPLAY_DebutNuit", "23:00");
  Serveur.AddParamText("DISPLAY_FinNuit", "07:00");
  Serveur.AddParamBool("DISPLAY_HorlogeVeille", true);''',
    'display configuration parameters',
)

# -----------------------------------------------------------------------------
# Web template variables. DISPLAY_* parameters are rendered in their dedicated
# card, not again in the generic HTTP widget parameter list.
# -----------------------------------------------------------------------------
insert_before_once(
    server,
    '  if (var == "PARAMS") {',
    '''  if (var == "DISPLAYDAY") return GetLocalParamValue("DISPLAY_Jour");
  if (var == "DISPLAYNIGHT") return GetLocalParamValue("DISPLAY_Nuit");
  if (var == "DISPLAYSLEEPBRIGHT") return GetLocalParamValue("DISPLAY_Veille");
  if (var == "DISPLAYSLEEPTIMEOUT") return GetLocalParamValue("DISPLAY_Delai");
  if (var == "DISPLAYNIGHTSTART") return GetLocalParamValue("DISPLAY_DebutNuit");
  if (var == "DISPLAYNIGHTEND") return GetLocalParamValue("DISPLAY_FinNuit");
  if (var == "DISPLAYNIGHTAUTO") return GetLocalParamValue("DISPLAY_NuitAuto");
  if (var == "DISPLAYCLOCKSLEEP") return GetLocalParamValue("DISPLAY_HorlogeVeille");
  if (var == "DISPLAYNIGHTCHECKED") return GetLocalParamValue("DISPLAY_NuitAuto") == "1" ? "checked" : "";
  if (var == "DISPLAYCLOCKCHECKED") return GetLocalParamValue("DISPLAY_HorlogeVeille") == "1" ? "checked" : "";

''',
    'display web placeholders',
)

# Hide the dedicated display settings from the historical generic PARAMS list.
text = server.read_text(encoding='utf-8')
params_start = text.find('if (var == "PARAMS") {')
if params_start < 0:
    raise SystemExit('PARAMS block not found')
params_end = text.find('\n  if (var == ', params_start + 20)
if params_end < 0:
    params_end = min(len(text), params_start + 10000)
block = text[params_start:params_end]
loop_anchor = 'for (int i = 0; i < Params.size(); i++) {'
if loop_anchor in block and 'startsWith("DISPLAY_")' not in block:
    block = block.replace(loop_anchor, loop_anchor + '''
      String miraParamName = SPIFFS_Config_Params["params"][i]["param"].as<String>();
      if (miraParamName.startsWith("DISPLAY_")) continue;''', 1)
    server.write_text(text[:params_start] + block + text[params_end:], encoding='utf-8')
else:
    raise SystemExit('generic PARAMS loop anchor not found or already patched')

# -----------------------------------------------------------------------------
# UI public system display API for MQTT.
# -----------------------------------------------------------------------------
insert_before_once(
    ui_h,
    'private:\n',
    '''  // Mira 0.6.0 - affichage autonome + commandes systeme MQTT.
  void handleMqttDisplayCommand(const String& key, const String& payload);
  uint8_t displayDayBrightness() const { return _dayBrightnessPct; }
  uint8_t displayNightBrightness() const { return _nightBrightnessPct; }
  uint8_t displaySleepBrightness() const { return _sleepBrightnessPct; }
  uint32_t displaySleepTimeout() const { return _sleepTimeoutSec; }
  bool displayNightAuto() const { return _nightScheduleEnabled; }
  String displayNightStart() const { return _nightStart; }
  String displayNightEnd() const { return _nightEnd; }
  bool displayClockInSleep() const { return _clockInSleep; }
  bool displaySleeping() const { return _sleeping; }
  bool displayNightActive() const { return _nightActive; }
  uint8_t displayActiveBrightness() const;

''',
    'display public API',
)

insert_before_once(
    ui_h,
    '  bool _sleepEnabled = true;\n',
    '''  uint8_t _dayBrightnessPct = 85;
  uint8_t _nightBrightnessPct = 20;
  uint8_t _sleepBrightnessPct = 8;
  uint32_t _sleepTimeoutSec = 120;
  bool _nightScheduleEnabled = false;
  bool _nightActive = false;
  bool _manualBrightnessOverride = false;
  uint8_t _manualBrightnessPct = 85;
  String _nightStart = "23:00";
  String _nightEnd = "07:00";
  String _displayConfigSignature;
  uint32_t _lastDisplayConfigCheck = 0;
  int _lastScheduleMinute = -1;
''',
    'display state fields',
)

# Add private helpers before the existing NTP helper.
insert_before_once(
    ui_h,
    '  void startNtpIfPossible();\n',
    '''  void refreshDisplayConfig(bool force = false);
  void updateDisplaySchedule(bool force = false);
  void applyAwakeBrightness();
  void applySleepBrightness();
  static uint8_t percentToBrightness(uint8_t percent);
  static uint8_t brightnessToPercent(uint8_t raw);
  static int parseClockMinutes(const String& value);
  static bool parseBoolPayload(const String& value, bool fallback = false);
  static String normalizeClock(const String& value, const String& fallback);
''',
    'display private helpers',
)

# Initialize the display using web configuration rather than only legacy prefs.
replace_once(
    ui,
    '  _display.begin();\n  _display.setBrightness(_brightness);',
    '  _display.begin();\n  refreshDisplayConfig(true);\n  updateDisplaySchedule(true);\n  applyAwakeBrightness();',
    'display startup configuration',
)

# Runtime web changes + autonomous schedule; replace fixed 120 s timeout.
replace_in_function(
    ui,
    'void MiraPanelUI::loop() {',
    '  uint32_t now = millis();',
    '''  refreshDisplayConfig(false);
  updateDisplaySchedule(false);
  uint32_t now = millis();''',
    'display config polling in UI loop',
)
replace_in_function(
    ui,
    'void MiraPanelUI::loop() {',
    'if (!_sleeping && _sleepEnabled && idle >= 120000UL) enterSleep();',
    'if (!_sleeping && _sleepEnabled && _sleepTimeoutSec > 0 && idle >= _sleepTimeoutSec * 1000UL) enterSleep();',
    'configurable sleep timeout',
)

# Existing on-panel brightness slider remains raw internally, but updates the
# currently relevant day/night percentage so screen, web config and MQTT agree.
replace_function(ui, 'void MiraPanelUI::onBrightness(lv_event_t* e) {', r'''void MiraPanelUI::onBrightness(lv_event_t* e) {
  MiraPanelUI* ui = static_cast<MiraPanelUI*>(lv_event_get_user_data(e));
  if (!ui) return;
  int value = lv_slider_get_value(lv_event_get_target(e));
  value = clampInt(value, 30, 255);
  ui->_brightness = (uint8_t)value;
  uint8_t pct = brightnessToPercent(ui->_brightness);
  ui->_manualBrightnessOverride = false;
  if (ui->_nightScheduleEnabled && ui->_nightActive) {
    ui->_nightBrightnessPct = pct;
    ui->_server.UpdateParamText("DISPLAY_Nuit", String(pct));
  } else {
    ui->_dayBrightnessPct = pct;
    ui->_server.UpdateParamText("DISPLAY_Jour", String(pct));
  }
  ui->_display.setBrightness(ui->_brightness);
  if (ui->_brightnessValue) lv_label_set_text_fmt(ui->_brightnessValue, "%u%%", pct);
  ui->noteActivity();
}''', 'panel brightness sync')

# Keep the existing sleep switch useful: disabled => timeout 0; enabled => restore 120 s.
replace_function(ui, 'void MiraPanelUI::onSleepToggle(lv_event_t* e) {', r'''void MiraPanelUI::onSleepToggle(lv_event_t* e) {
  MiraPanelUI* ui = static_cast<MiraPanelUI*>(lv_event_get_user_data(e));
  if (!ui) return;
  bool enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  ui->_sleepEnabled = enabled;
  if (enabled && ui->_sleepTimeoutSec == 0) ui->_sleepTimeoutSec = 120;
  if (!enabled) ui->_sleepTimeoutSec = 0;
  ui->_server.UpdateParamText("DISPLAY_Delai", String(ui->_sleepTimeoutSec));
  ui->_prefs.putBool("sleep", enabled);
  ui->noteActivity();
  if (!enabled && ui->_sleeping) ui->wakeFromSleep();
}''', 'panel sleep toggle sync')

# Clock-in-sleep switch is mirrored to the web configuration.
replace_function(ui, 'void MiraPanelUI::onClockSleepToggle(lv_event_t* e) {', r'''void MiraPanelUI::onClockSleepToggle(lv_event_t* e) {
  MiraPanelUI* ui = static_cast<MiraPanelUI*>(lv_event_get_user_data(e));
  if (!ui) return;
  ui->_clockInSleep = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  ui->_server.UpdateParamBool("DISPLAY_HorlogeVeille", ui->_clockInSleep);
  ui->_prefs.putBool("clock", ui->_clockInSleep);
  ui->noteActivity();
  if (ui->_sleeping) ui->applySleepBrightness();
}''', 'panel sleep clock sync')

replace_function(ui, 'void MiraPanelUI::enterSleep() {', r'''void MiraPanelUI::enterSleep() {
  if (_sleeping) return;
  _sleeping = true;
  if (_clockInSleep) {
    applySleepBrightness();
    if (_sleepOverlay) lv_obj_clear_flag(_sleepOverlay, LV_OBJ_FLAG_HIDDEN);
    updateClock();
  } else {
    _display.setBrightness(0);
  }
  Serial.println("[MIRA][DISPLAY] Veille active");
}''', 'configurable sleep entry')

replace_function(ui, 'void MiraPanelUI::wakeFromSleep() {', r'''void MiraPanelUI::wakeFromSleep() {
  if (!_sleeping) return;
  _sleeping = false;
  if (_sleepOverlay) lv_obj_add_flag(_sleepOverlay, LV_OBJ_FLAG_HIDDEN);
  applyAwakeBrightness();
  _lastActivity = millis();
  Serial.println("[MIRA][DISPLAY] Reveil");
}''', 'scheduled wake brightness')

# Display engine. Non-zero percentages deliberately map to the historical safe
# 30..255 range; 0 remains a true backlight off command.
display_engine = r'''
uint8_t MiraPanelUI::percentToBrightness(uint8_t percent) {
  if (percent == 0) return 0;
  if (percent > 100) percent = 100;
  return (uint8_t)(30 + ((uint16_t)(percent - 1) * 225U) / 99U);
}

uint8_t MiraPanelUI::brightnessToPercent(uint8_t raw) {
  if (raw == 0) return 0;
  if (raw <= 30) return 1;
  return (uint8_t)(1 + ((uint16_t)(raw - 30) * 99U + 112U) / 225U);
}

int MiraPanelUI::parseClockMinutes(const String& value) {
  if (value.length() != 5 || value[2] != ':') return -1;
  if (!isDigit(value[0]) || !isDigit(value[1]) || !isDigit(value[3]) || !isDigit(value[4])) return -1;
  int h = value.substring(0, 2).toInt();
  int m = value.substring(3, 5).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

String MiraPanelUI::normalizeClock(const String& value, const String& fallback) {
  String v = value;
  v.trim();
  return parseClockMinutes(v) >= 0 ? v : fallback;
}

bool MiraPanelUI::parseBoolPayload(const String& value, bool fallback) {
  String v = value;
  v.trim();
  v.toLowerCase();
  if (v == "1" || v == "on" || v == "true" || v == "oui") return true;
  if (v == "0" || v == "off" || v == "false" || v == "non") return false;
  return fallback;
}

uint8_t MiraPanelUI::displayActiveBrightness() const {
  if (_sleeping) return _clockInSleep ? _sleepBrightnessPct : 0;
  if (_manualBrightnessOverride) return _manualBrightnessPct;
  return (_nightScheduleEnabled && _nightActive) ? _nightBrightnessPct : _dayBrightnessPct;
}

void MiraPanelUI::refreshDisplayConfig(bool force) {
  if (!force && millis() - _lastDisplayConfigCheck < 2000UL) return;
  _lastDisplayConfigCheck = millis();

  String dayText = _server.GetParamText("DISPLAY_Jour");
  String nightText = _server.GetParamText("DISPLAY_Nuit");
  String sleepText = _server.GetParamText("DISPLAY_Veille");
  String timeoutText = _server.GetParamText("DISPLAY_Delai");
  String startText = normalizeClock(_server.GetParamText("DISPLAY_DebutNuit"), "23:00");
  String endText = normalizeClock(_server.GetParamText("DISPLAY_FinNuit"), "07:00");
  bool nightAuto = _server.GetParamBool("DISPLAY_NuitAuto");
  bool clockSleep = _server.GetParamBool("DISPLAY_HorlogeVeille");

  int day = dayText.toInt();
  int night = nightText.toInt();
  int sleep = sleepText.toInt();
  long timeout = timeoutText.toInt();
  day = clampInt(day, 1, 100);
  night = clampInt(night, 1, 100);
  sleep = clampInt(sleep, 0, 100);
  if (timeout < 0) timeout = 0;
  if (timeout > 86400) timeout = 86400;

  String signature = String(day) + "|" + String(night) + "|" + String(sleep) + "|" + String(timeout) + "|" +
                     (nightAuto ? "1" : "0") + "|" + startText + "|" + endText + "|" + (clockSleep ? "1" : "0");
  if (!force && signature == _displayConfigSignature) return;

  bool changedAfterStartup = _displayConfigSignature.length() && signature != _displayConfigSignature;
  _displayConfigSignature = signature;
  _dayBrightnessPct = (uint8_t)day;
  _nightBrightnessPct = (uint8_t)night;
  _sleepBrightnessPct = (uint8_t)sleep;
  _sleepTimeoutSec = (uint32_t)timeout;
  _sleepEnabled = _sleepTimeoutSec > 0;
  _nightScheduleEnabled = nightAuto;
  _nightStart = startText;
  _nightEnd = endText;
  _clockInSleep = clockSleep;
  if (changedAfterStartup) _manualBrightnessOverride = false;

  if (_sleepSwitch) {
    if (_sleepEnabled) lv_obj_add_state(_sleepSwitch, LV_STATE_CHECKED);
    else lv_obj_clear_state(_sleepSwitch, LV_STATE_CHECKED);
  }
  if (_clockSleepSwitch) {
    if (_clockInSleep) lv_obj_add_state(_clockSleepSwitch, LV_STATE_CHECKED);
    else lv_obj_clear_state(_clockSleepSwitch, LV_STATE_CHECKED);
  }

  updateDisplaySchedule(true);
  if (_sleeping) applySleepBrightness();
  else applyAwakeBrightness();
  Serial.printf("[MIRA][DISPLAY] Config jour=%u%% nuit=%u%% veille=%u%% delai=%lus nuit_auto=%s %s-%s\n",
                _dayBrightnessPct, _nightBrightnessPct, _sleepBrightnessPct,
                (unsigned long)_sleepTimeoutSec, _nightScheduleEnabled ? "oui" : "non",
                _nightStart.c_str(), _nightEnd.c_str());
}

void MiraPanelUI::updateDisplaySchedule(bool force) {
  if (!_nightScheduleEnabled) {
    if (_nightActive || force) {
      bool wasNight = _nightActive;
      _nightActive = false;
      if (wasNight) _manualBrightnessOverride = false;
      if (!_sleeping) applyAwakeBrightness();
    }
    return;
  }

  struct tm t;
  if (!getLocalTime(&t, 10)) return;
  int minute = t.tm_hour * 60 + t.tm_min;
  if (!force && minute == _lastScheduleMinute) return;
  _lastScheduleMinute = minute;

  int start = parseClockMinutes(_nightStart);
  int end = parseClockMinutes(_nightEnd);
  if (start < 0 || end < 0 || start == end) return;
  bool night = (start < end) ? (minute >= start && minute < end)
                             : (minute >= start || minute < end);
  if (force || night != _nightActive) {
    bool changed = night != _nightActive;
    _nightActive = night;
    if (changed) _manualBrightnessOverride = false;
    if (!_sleeping) applyAwakeBrightness();
    if (changed) Serial.printf("[MIRA][DISPLAY] Mode %s\n", night ? "nuit" : "jour");
  }
}

void MiraPanelUI::applyAwakeBrightness() {
  uint8_t pct = displayActiveBrightness();
  _brightness = percentToBrightness(pct);
  _display.setBrightness(_brightness);
  if (_brightnessSlider) lv_slider_set_value(_brightnessSlider, _brightness, LV_ANIM_OFF);
  if (_brightnessValue) lv_label_set_text_fmt(_brightnessValue, "%u%%", pct);
}

void MiraPanelUI::applySleepBrightness() {
  if (!_sleeping) return;
  if (!_clockInSleep) {
    _display.setBrightness(0);
    return;
  }
  _display.setBrightness(percentToBrightness(_sleepBrightnessPct));
}

void MiraPanelUI::handleMqttDisplayCommand(const String& key, const String& payload) {
  String k = key;
  String p = payload;
  k.trim(); k.toLowerCase();
  p.trim();

  if (k == "wake") {
    if (parseBoolPayload(p, p == "1")) wakeFromSleep();
    return;
  }
  if (k == "sleep") {
    if (parseBoolPayload(p, p == "1")) enterSleep();
    return;
  }
  if (k == "brightness") {
    String lower = p; lower.toLowerCase();
    if (lower == "auto") {
      _manualBrightnessOverride = false;
      updateDisplaySchedule(true);
      if (!_sleeping) applyAwakeBrightness();
      return;
    }
    int pct = clampInt(p.toInt(), 0, 100);
    _manualBrightnessPct = (uint8_t)pct;
    _manualBrightnessOverride = true;
    if (!_sleeping) applyAwakeBrightness();
    return;
  }

  if (k == "day") {
    int v = clampInt(p.toInt(), 1, 100);
    _server.UpdateParamText("DISPLAY_Jour", String(v));
  } else if (k == "night") {
    int v = clampInt(p.toInt(), 1, 100);
    _server.UpdateParamText("DISPLAY_Nuit", String(v));
  } else if (k == "sleep_brightness") {
    int v = clampInt(p.toInt(), 0, 100);
    _server.UpdateParamText("DISPLAY_Veille", String(v));
  } else if (k == "sleep_timeout") {
    long v = p.toInt(); if (v < 0) v = 0; if (v > 86400) v = 86400;
    _server.UpdateParamText("DISPLAY_Delai", String(v));
  } else if (k == "night_auto") {
    _server.UpdateParamBool("DISPLAY_NuitAuto", parseBoolPayload(p, _nightScheduleEnabled));
  } else if (k == "night_start") {
    String v = normalizeClock(p, _nightStart);
    _server.UpdateParamText("DISPLAY_DebutNuit", v);
  } else if (k == "night_end") {
    String v = normalizeClock(p, _nightEnd);
    _server.UpdateParamText("DISPLAY_FinNuit", v);
  } else if (k == "clock_sleep") {
    _server.UpdateParamBool("DISPLAY_HorlogeVeille", parseBoolPayload(p, _clockInSleep));
  } else {
    return;
  }
  refreshDisplayConfig(true);
}

'''
insert_before_once(ui, 'void MiraPanelUI::startNtpIfPossible() {', display_engine, 'display engine implementation')

# -----------------------------------------------------------------------------
# MQTT display namespace: mira/<panel>/Display/<key>/set + retained /state.
# -----------------------------------------------------------------------------
replace_once(
    mqtt_h,
    '  String statePrefix() const;\n',
    '  String statePrefix() const;\n  String displayPrefix() const;\n  bool publishDisplayState(const String& key, const String& payload);\n',
    'mqtt display public API',
)
insert_before_once(
    mqtt_h,
    '  bool sendPing();\n',
    '  void publishDisplayStates(bool force = false);\n  String _lastDisplayStateSignature;\n',
    'mqtt display state publisher',
)

replace_once(
    mqtt,
    '''String MiraMqtt::statePrefix() const {
  return "mira/" + _screenName + "/States/";
}
''',
    '''String MiraMqtt::statePrefix() const {
  return "mira/" + _screenName + "/States/";
}

String MiraMqtt::displayPrefix() const {
  return "mira/" + _screenName + "/Display/";
}
''',
    'mqtt display prefix',
)

replace_function(mqtt, 'bool MiraMqtt::subscribeStates() {', r'''bool MiraMqtt::subscribeStates() {
  String stateTopic = statePrefix() + "#";
  String displayTopic = displayPrefix() + "+/set";
  uint8_t packet[MAX_PACKET];
  size_t pos = 0;
  uint16_t id = _messageId++;
  if (_messageId == 0) _messageId = 1;
  packet[pos++] = (uint8_t)((id >> 8) & 0xFF);
  packet[pos++] = (uint8_t)(id & 0xFF);
  if (!writeMqttString(packet, sizeof(packet), pos, stateTopic)) return false;
  if (pos >= sizeof(packet)) return false;
  packet[pos++] = 0;
  if (!writeMqttString(packet, sizeof(packet), pos, displayTopic)) return false;
  if (pos >= sizeof(packet)) return false;
  packet[pos++] = 0;
  bool ok = sendPacket(0x82, packet, pos);
  if (ok) {
    Serial.println("[MIRA][MQTT] Abonnement : " + stateTopic);
    Serial.println("[MIRA][MQTT] Commandes  : " + displayTopic);
  }
  return ok;
}''', 'mqtt subscribe states and display')

# Publish display settings and runtime state retained so Jeedom can discover the
# current values immediately after subscribing.
insert_before_once(
    mqtt,
    'bool MiraMqtt::sendPing() {',
    r'''bool MiraMqtt::publishDisplayState(const String& key, const String& payload) {
  if (!_enabled) return false;
  return publishRaw(displayPrefix() + key + "/state", payload, true);
}

void MiraMqtt::publishDisplayStates(bool force) {
  if (!_connected || !_net.connected()) return;
  String signature = String(_ui.displayDayBrightness()) + "|" + String(_ui.displayNightBrightness()) + "|" +
                     String(_ui.displaySleepBrightness()) + "|" + String(_ui.displaySleepTimeout()) + "|" +
                     (_ui.displayNightAuto() ? "1" : "0") + "|" + _ui.displayNightStart() + "|" +
                     _ui.displayNightEnd() + "|" + (_ui.displayClockInSleep() ? "1" : "0") + "|" +
                     String(_ui.displayActiveBrightness()) + "|" + (_ui.displaySleeping() ? "sleep" : "awake") + "|" +
                     (_ui.displayNightActive() ? "night" : "day");
  if (!force && signature == _lastDisplayStateSignature) return;
  _lastDisplayStateSignature = signature;

  publishDisplayState("day", String(_ui.displayDayBrightness()));
  publishDisplayState("night", String(_ui.displayNightBrightness()));
  publishDisplayState("sleep_brightness", String(_ui.displaySleepBrightness()));
  publishDisplayState("sleep_timeout", String(_ui.displaySleepTimeout()));
  publishDisplayState("night_auto", _ui.displayNightAuto() ? "1" : "0");
  publishDisplayState("night_start", _ui.displayNightStart());
  publishDisplayState("night_end", _ui.displayNightEnd());
  publishDisplayState("clock_sleep", _ui.displayClockInSleep() ? "1" : "0");
  publishDisplayState("brightness", String(_ui.displayActiveBrightness()));
  publishDisplayState("mode", _ui.displayNightActive() ? "night" : "day");
  publishDisplayState("state", _ui.displaySleeping() ? "sleep" : "awake");
}

''',
    'mqtt retained display states',
)

replace_function(mqtt, 'void MiraMqtt::processPublish(uint8_t header, const uint8_t* packet, size_t length) {', r'''void MiraMqtt::processPublish(uint8_t header, const uint8_t* packet, size_t length) {
  if (length < 2) return;
  size_t pos = 0;
  uint16_t topicLen = ((uint16_t)packet[pos] << 8) | packet[pos + 1];
  pos += 2;
  if (pos + topicLen > length) return;

  String topic;
  topic.reserve(topicLen);
  for (uint16_t i = 0; i < topicLen; ++i) topic += (char)packet[pos + i];
  pos += topicLen;

  uint8_t qos = (header >> 1) & 0x03;
  if (qos > 0) {
    if (pos + 2 > length) return;
    pos += 2;
  }

  String payload;
  payload.reserve(length - pos);
  while (pos < length) payload += (char)packet[pos++];

  String states = statePrefix();
  if (topic.startsWith(states)) {
    String leaf = topic.substring(states.length());
    if (!leaf.length()) return;
    Serial.printf("[MIRA][MQTT] State %s => %s\n", topic.c_str(), payload.c_str());
    _ui.handleMqttState(leaf, payload);
    return;
  }

  String display = displayPrefix();
  if (topic.startsWith(display) && topic.endsWith("/set")) {
    String key = topic.substring(display.length(), topic.length() - 4);
    if (!key.length()) return;
    Serial.printf("[MIRA][MQTT] Display %s <= %s\n", key.c_str(), payload.c_str());
    _ui.handleMqttDisplayCommand(key, payload);
    publishDisplayStates(true);
  }
}''', 'mqtt display command routing')

replace_in_function(
    mqtt,
    'bool MiraMqtt::connectBroker() {',
    '''  if (!subscribeStates()) {
    disconnectBroker();
    return false;
  }
  return true;''',
    '''  if (!subscribeStates()) {
    disconnectBroker();
    return false;
  }
  _lastDisplayStateSignature = "";
  publishDisplayStates(true);
  return true;''',
    'publish display states on MQTT connect',
)

replace_in_function(
    mqtt,
    'void MiraMqtt::loop() {',
    '  processIncoming();',
    '  processIncoming();\n  publishDisplayStates(false);',
    'publish changing display states',
)

# Better MQTT diagnostics for the new namespace.
replace_once(
    mqtt,
    '  Serial.println("[MIRA][MQTT] States  : " + statePrefix() + "#topic#");',
    '  Serial.println("[MIRA][MQTT] States  : " + statePrefix() + "#topic#");\n  Serial.println("[MIRA][MQTT] Display : " + displayPrefix() + "<cle>/set|state");',
    'mqtt startup display trace',
)

print('Mira Panel 0.6.0 autonomous display settings, schedule, sleep and MQTT controls applied')
