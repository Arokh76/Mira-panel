from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'
server_h = root / 'MiraPanelServer.h'
server_cpp = root / 'MiraPanelServer.cpp'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


def replace_in_function(path: Path, signature: str, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f'{label}: function not found in {path}')
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
    func = text[start:end]
    count = func.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in function, got {count}')
    func = func.replace(old, new, 1)
    path.write_text(text[:start] + func + text[end:], encoding='utf-8')


# Version
replace_once(ino, '"0.5.12"', '"0.5.13"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.12 initialise', '[MIRA] Firmware 0.5.13 initialise', 'startup log')

# The server range storage was integer-only. Keep the same API name so every
# existing linear slider remains compatible, but let the stored/event value be a
# float. Integer callers continue to work unchanged while thermostat can send .5.
replace_once(server_h, '  int IndexGetRange(int ID);', '  float IndexGetRange(int ID);', 'range getter float declaration')
replace_once(server_h, '  void IndexSetRange(int ID, int Value);', '  void IndexSetRange(int ID, float Value);', 'range setter float declaration')

replace_once(server_cpp,
             'void MiraPanelServer::IndexSetRange(int ID, int Value) {',
             'void MiraPanelServer::IndexSetRange(int ID, float Value) {',
             'range setter float definition')
replace_in_function(server_cpp,
                    'void MiraPanelServer::IndexSetRange(int ID, float Value) {',
                    '      String SValue = String(Value);',
                    '      String SValue = (fabsf(Value - roundf(Value)) < 0.001f) ? String((int)roundf(Value)) : String(Value, 1);',
                    'range setter decimal event text')
replace_once(server_cpp,
             'int MiraPanelServer::IndexGetRange(int ID) {',
             'float MiraPanelServer::IndexGetRange(int ID) {',
             'range getter float definition')
replace_in_function(server_cpp,
                    'float MiraPanelServer::IndexGetRange(int ID) {',
                    '  int Val = 0;',
                    '  float Val = 0.0f;',
                    'range getter float local')
replace_in_function(server_cpp,
                    'float MiraPanelServer::IndexGetRange(int ID) {',
                    '      Val = SPIFFS_Config_Actions["actions"][i]["value"].as<String>().toInt();',
                    '      Val = SPIFFS_Config_Actions["actions"][i]["value"].as<String>().toFloat();',
                    'range getter decimal parse')

# Round thermostat only: LVGL arc remains integer internally but one arc unit now
# means 0.5 degree. Generic linear sliders keep their historical 1-unit step.
replace_in_function(ui,
                    'void MiraPanelUI::createRoundRange(uint8_t idx) {',
                    '  lv_arc_set_range(b.control, b.minValue, b.maxValue);\n  lv_arc_set_value(b.control, b.currentValue);',
                    '  lv_arc_set_range(b.control, b.minValue * 2, b.maxValue * 2);\n  lv_arc_set_value(b.control, b.currentValue * 2);',
                    'thermostat doubled LVGL scale')

# Live thermostat event: convert the doubled LVGL value back to a real setpoint
# before HTTP/event + MQTT. Keep 0.5 precision and the 0.5.12 local anti-echo logic.
old_round = '''  int value = lv_arc_get_value(lv_event_get_target(e));
  b.currentValue = value;
  if (b.valueLabel) {
    String txt = roundRangeDisplayValue(value, b.unit);
    lv_label_set_text(b.valueLabel, txt.c_str());
  }
  ui.noteActivity();
  if (ui._suppressEvents) return;

  const uint32_t now = millis();
  const bool finalSend = code == LV_EVENT_RELEASED;
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {
    b.localInputUntilMs = now + 250;
  }
  if (finalSend) b.localInputUntilMs = now + 350;

  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue;
  if (liveSend) {
    b.lastLiveSendMs = now;
    b.lastLiveValue = value;
    ui._server.IndexSetRange(b.id, value);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, String(value));
  }
  if (finalSend) {
    b.lastLiveSendMs = now;
    b.lastLiveValue = value;
    ui._server.IndexSetRange(b.id, value);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, String(value));
  }'''
new_round = '''  const int rawValue = lv_arc_get_value(lv_event_get_target(e));
  const float setpoint = rawValue * 0.5f;
  b.currentValue = (int)roundf(setpoint);
  if (b.valueLabel) {
    String txt = String(setpoint, 1);
    if (b.unit.length()) txt += " " + b.unit;
    lv_label_set_text(b.valueLabel, txt.c_str());
  }
  ui.noteActivity();
  if (ui._suppressEvents) return;

  const uint32_t now = millis();
  const bool finalSend = code == LV_EVENT_RELEASED;
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {
    b.localInputUntilMs = now + 250;
  }
  if (finalSend) b.localInputUntilMs = now + 350;

  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && rawValue != b.lastLiveValue;
  const String setpointText = (rawValue & 1) ? String(setpoint, 1) : String((int)setpoint);
  if (liveSend) {
    b.lastLiveSendMs = now;
    b.lastLiveValue = rawValue;
    ui._server.IndexSetRange(b.id, setpoint);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, setpointText);
  }
  if (finalSend) {
    b.lastLiveSendMs = now;
    b.lastLiveValue = rawValue;
    ui._server.IndexSetRange(b.id, setpoint);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, setpointText);
  }'''
replace_in_function(ui, 'void MiraPanelUI::onRoundRange(lv_event_t* e) {', old_round, new_round, 'thermostat half-degree live event')

# Server/event readback: generic slider still rounds to integer; round thermostat
# restores the half-degree setpoint on the doubled arc scale and displays one decimal.
old_readback = '''  else if ((b.type == W_RANGE || b.type == W_ROUNDRANGE) && eventId == b.id) {
    int value = _server.IndexGetRange(b.id);
    value = clampInt(value, b.minValue, b.maxValue);
    b.currentValue = value;
    _suppressEvents = true;
    if (b.type == W_RANGE && b.control) lv_slider_set_value(b.control, value, LV_ANIM_OFF);
    if (b.type == W_ROUNDRANGE && b.control) lv_arc_set_value(b.control, value);
    if (b.valueLabel) {
      if (b.type == W_ROUNDRANGE) {
        String txt = roundRangeDisplayValue(value, b.unit);
        lv_label_set_text(b.valueLabel, txt.c_str());
      } else {
        lv_label_set_text_fmt(b.valueLabel, "%d%s%s", value, b.unit.length() ? " " : "", b.unit.c_str());
      }
    }
    _suppressEvents = false;
  }'''
new_readback = '''  else if ((b.type == W_RANGE || b.type == W_ROUNDRANGE) && eventId == b.id) {
    float serverValue = _server.IndexGetRange(b.id);
    if (serverValue < b.minValue) serverValue = b.minValue;
    if (serverValue > b.maxValue) serverValue = b.maxValue;
    _suppressEvents = true;
    if (b.type == W_RANGE) {
      int value = (int)roundf(serverValue);
      b.currentValue = value;
      if (b.control) lv_slider_set_value(b.control, value, LV_ANIM_OFF);
      if (b.valueLabel) lv_label_set_text_fmt(b.valueLabel, "%d%s%s", value, b.unit.length() ? " " : "", b.unit.c_str());
    } else {
      int rawValue = (int)roundf(serverValue * 2.0f);
      b.currentValue = (int)roundf(serverValue);
      if (b.control) lv_arc_set_value(b.control, rawValue);
      if (b.valueLabel) {
        String txt = String(rawValue * 0.5f, 1);
        if (b.unit.length()) txt += " " + b.unit;
        lv_label_set_text(b.valueLabel, txt.c_str());
      }
    }
    _suppressEvents = false;
  }'''
replace_in_function(ui, 'void MiraPanelUI::updateBindingFromServer(', old_readback, new_readback, 'thermostat half-degree readback')

print('Mira Panel 0.5.13 thermostat 0.5 degree step applied; linear sliders unchanged')
