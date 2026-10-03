from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'
hdr = root / 'MiraPanelUI.h'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


# Version
replace_once(ino, '"0.5.9"', '"0.5.10"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.9 initialise', '[MIRA] Firmware 0.5.10 initialise', 'startup log')

# Per-widget holdoff used to stop delayed MQTT/HTTP echoes from dragging a control
# backwards while the finger is still moving (or just after release).
replace_once(
    hdr,
    '    uint32_t lastLiveColor = 0xFFFFFFFFUL;\n',
    '    uint32_t lastLiveColor = 0xFFFFFFFFUL;\n'
    '    uint32_t localInputUntilMs = 0;\n',
    'local interaction holdoff state',
)

# The LVGL colorwheel changes Hue/Saturation/Value mode on a long press by default,
# which visually looks like the ring randomly changing style. Lock it permanently
# to Hue mode.
replace_once(
    ui,
    '''  lv_obj_set_style_border_color(b.control, colorAccent(), LV_PART_KNOB);\n  lv_obj_align(b.control, LV_ALIGN_CENTER, 0, 12);''',
    '''  lv_obj_set_style_border_color(b.control, colorAccent(), LV_PART_KNOB);\n  lv_colorwheel_set_mode(b.control, LV_COLORWHEEL_MODE_HUE);\n  lv_colorwheel_set_mode_fixed(b.control, true);\n  lv_obj_align(b.control, LV_ALIGN_CENTER, 0, 12);''',
    'fixed hue-only colorwheel',
)

# Linear slider: visual tracking remains purely local and immediate. Network traffic
# is throttled to ~16 Hz and only MQTT is emitted while moving. The legacy HTTP/event
# path is sent once on release. This avoids a train of delayed server echoes fighting
# the finger position.
replace_once(
    ui,
    '''  const uint32_t now = millis();\n  const bool finalSend = code == LV_EVENT_RELEASED;\n  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue;\n  if (liveSend || finalSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveValue = value;\n    ui._server.IndexSetRange(b.id, value);\n    ui._server.IndexSetEvent(b.id);\n    ui.publishMqttAction(b, String(value));\n  }''',
    '''  const uint32_t now = millis();\n  const bool finalSend = code == LV_EVENT_RELEASED;\n  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {\n    b.localInputUntilMs = now + 250;\n  }\n  if (finalSend) b.localInputUntilMs = now + 350;\n\n  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue &&\n                        (b.lastLiveSendMs == 0 || (uint32_t)(now - b.lastLiveSendMs) >= 60);\n  if (liveSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveValue = value;\n    ui.publishMqttAction(b, String(value));\n  }\n  if (finalSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveValue = value;\n    ui._server.IndexSetRange(b.id, value);\n    ui._server.IndexSetEvent(b.id);\n    ui.publishMqttAction(b, String(value));\n  }''',
    'stable live linear range',
)

# Round range: same local-first / remote-echo holdoff policy.
replace_once(
    ui,
    '''  const uint32_t now = millis();\n  const bool finalSend = code == LV_EVENT_RELEASED;\n  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue;\n  if (liveSend || finalSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveValue = value;\n    ui._server.IndexSetRange(b.id, value);\n    ui._server.IndexSetEvent(b.id);\n    ui.publishMqttAction(b, String(value));\n  }''',
    '''  const uint32_t now = millis();\n  const bool finalSend = code == LV_EVENT_RELEASED;\n  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {\n    b.localInputUntilMs = now + 250;\n  }\n  if (finalSend) b.localInputUntilMs = now + 350;\n\n  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue &&\n                        (b.lastLiveSendMs == 0 || (uint32_t)(now - b.lastLiveSendMs) >= 60);\n  if (liveSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveValue = value;\n    ui.publishMqttAction(b, String(value));\n  }\n  if (finalSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveValue = value;\n    ui._server.IndexSetRange(b.id, value);\n    ui._server.IndexSetEvent(b.id);\n    ui.publishMqttAction(b, String(value));\n  }''',
    'stable live round range',
)

# Colour wheel: same policy; do not run IndexSetEvent continuously while dragging.
replace_once(
    ui,
    '''  const uint32_t now = millis();\n  const bool finalSend = code == LV_EVENT_RELEASED;\n  const bool liveSend = (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSING) &&\n                        rgb24 != b.lastLiveColor;\n  if (liveSend || finalSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveColor = rgb24;\n    ui._server.IndexSetColor(b.id, r, g, bl);\n    ui._server.IndexSetEvent(b.id);\n    char hex[8];\n    snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, bl);\n    ui.publishMqttAction(b, String(hex));\n  }''',
    '''  const uint32_t now = millis();\n  const bool finalSend = code == LV_EVENT_RELEASED;\n  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_VALUE_CHANGED) {\n    b.localInputUntilMs = now + 250;\n  }\n  if (finalSend) b.localInputUntilMs = now + 350;\n\n  const bool liveSend = (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSING) &&\n                        rgb24 != b.lastLiveColor &&\n                        (b.lastLiveSendMs == 0 || (uint32_t)(now - b.lastLiveSendMs) >= 60);\n  char hex[8];\n  snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, bl);\n  if (liveSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveColor = rgb24;\n    ui.publishMqttAction(b, String(hex));\n  }\n  if (finalSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveColor = rgb24;\n    ui._server.IndexSetColor(b.id, r, g, bl);\n    ui._server.IndexSetEvent(b.id);\n    ui.publishMqttAction(b, String(hex));\n  }''',
    'stable live colour wheel',
)

# Ignore delayed server/MQTT state echoes while a local continuous control owns the
# screen. Do this at the common server-to-widget refresh point so both range types
# and the colorwheel get the same protection.
text = ui.read_text(encoding='utf-8')
needle = 'void MiraPanelUI::updateBindingFromServer('
pos = text.find(needle)
if pos < 0:
    raise SystemExit('updateBindingFromServer definition not found')
brace = text.find('{', pos)
if brace < 0:
    raise SystemExit('updateBindingFromServer opening brace not found')
sig = text[pos:brace]
params = sig[sig.find('(') + 1:sig.rfind(')')].split(',')
if not params:
    raise SystemExit('updateBindingFromServer parameters not found')
first_name = params[0].strip().split()[-1].replace('&', '').replace('*', '')
insert = (\n    '\n  if (' + first_name + ' >= _bindingCount) return;'\n    '\n  Binding& localBinding = _bindings[' + first_name + '];'\n    '\n  if (localBinding.localInputUntilMs != 0 && '\n    '(int32_t)(millis() - localBinding.localInputUntilMs) < 0) return;\n'\n)
text = text[:brace + 1] + insert + text[brace + 1:]
ui.write_text(text, encoding='utf-8')

print('Mira Panel 0.5.10 stable local-first controls, hue lock and feedback holdoff applied')
