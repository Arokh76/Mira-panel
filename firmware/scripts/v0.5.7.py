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
replace_once(ino, '"0.5.6"', '"0.5.7"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.6 initialise', '[MIRA] Firmware 0.5.7 initialise', 'startup log')

# Keep a tiny amount of per-widget state so continuous controls can be sent live
# without flooding Jeedom/MQTT. 100 ms = at most 10 updates/s, with a final forced
# value on finger release.
replace_once(
    hdr,
    '    String style = "standard";\n',
    '    String style = "standard";\n'
    '    uint32_t lastLiveSendMs = 0;\n'
    '    int lastLiveValue = -32768;\n'
    '    uint32_t lastLiveColor = 0xFFFFFFFFUL;\n',
    'live control binding state',
)

# Colour wheel: make the ring visually stronger and listen to all touch/value
# events so its colour can be transmitted while the finger is moving.
replace_once(
    ui,
    '''  b.control = lv_colorwheel_create(b.card, true);
  lv_obj_set_size(b.control, 160, 160);
  lv_obj_align(b.control, LV_ALIGN_CENTER, 0, 12);
  lv_colorwheel_set_rgb(b.control, lv_color_make(50, 120, 180));
  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onColor, LV_EVENT_RELEASED, ctx);''',
    '''  b.control = lv_colorwheel_create(b.card, true);
  lv_obj_set_size(b.control, 160, 160);
  lv_obj_set_style_arc_width(b.control, 18, LV_PART_MAIN);
  lv_obj_set_style_border_width(b.control, 2, LV_PART_KNOB);
  lv_obj_set_style_border_color(b.control, colorAccent(), LV_PART_KNOB);
  lv_obj_align(b.control, LV_ALIGN_CENTER, 0, 12);
  lv_colorwheel_set_rgb(b.control, lv_color_make(50, 120, 180));
  EventContext* ctx = makeContext(idx);
  lv_obj_add_event_cb(b.control, onColor, LV_EVENT_ALL, ctx);''',
    'thicker live colour wheel',
)

replace_once(
    ui,
    '''void MiraPanelUI::onRange(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  lv_event_code_t code = lv_event_get_code(e);
  int value = lv_slider_get_value(lv_event_get_target(e));
  b.currentValue = value;
  if (b.valueLabel) lv_label_set_text_fmt(b.valueLabel, "%d%s%s", value, b.unit.length() ? " " : "", b.unit.c_str());
  ui.noteActivity();
  if (code == LV_EVENT_RELEASED && !ui._suppressEvents) {
    ui._server.IndexSetRange(b.id, value);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, String(value));
  }
}''',
    '''void MiraPanelUI::onRange(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  lv_event_code_t code = lv_event_get_code(e);
  int value = lv_slider_get_value(lv_event_get_target(e));
  b.currentValue = value;
  if (b.valueLabel) lv_label_set_text_fmt(b.valueLabel, "%d%s%s", value, b.unit.length() ? " " : "", b.unit.c_str());
  ui.noteActivity();
  if (ui._suppressEvents) return;

  const uint32_t now = millis();
  const bool finalSend = code == LV_EVENT_RELEASED;
  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue &&
                        (b.lastLiveSendMs == 0 || (uint32_t)(now - b.lastLiveSendMs) >= 100);
  if (liveSend || finalSend) {
    b.lastLiveSendMs = now;
    b.lastLiveValue = value;
    ui._server.IndexSetRange(b.id, value);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, String(value));
  }
}''',
    'live linear range',
)

replace_once(
    ui,
    '''void MiraPanelUI::onRoundRange(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  lv_event_code_t code = lv_event_get_code(e);
  int value = lv_arc_get_value(lv_event_get_target(e));
  b.currentValue = value;
  if (b.valueLabel) {
    String txt = roundRangeDisplayValue(value, b.unit);
    lv_label_set_text(b.valueLabel, txt.c_str());
  }
  ui.noteActivity();
  if (code == LV_EVENT_RELEASED && !ui._suppressEvents) {
    ui._server.IndexSetRange(b.id, value);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, String(value));
  }
}''',
    '''void MiraPanelUI::onRoundRange(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  lv_event_code_t code = lv_event_get_code(e);
  int value = lv_arc_get_value(lv_event_get_target(e));
  b.currentValue = value;
  if (b.valueLabel) {
    String txt = roundRangeDisplayValue(value, b.unit);
    lv_label_set_text(b.valueLabel, txt.c_str());
  }
  ui.noteActivity();
  if (ui._suppressEvents) return;

  const uint32_t now = millis();
  const bool finalSend = code == LV_EVENT_RELEASED;
  const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue &&
                        (b.lastLiveSendMs == 0 || (uint32_t)(now - b.lastLiveSendMs) >= 100);
  if (liveSend || finalSend) {
    b.lastLiveSendMs = now;
    b.lastLiveValue = value;
    ui._server.IndexSetRange(b.id, value);
    ui._server.IndexSetEvent(b.id);
    ui.publishMqttAction(b, String(value));
  }
}''',
    'live round range',
)

replace_once(
    ui,
    '''void MiraPanelUI::onColor(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ui._suppressEvents || ctx->bindingIndex >= ui._bindingCount) return;
  Binding& b = ui._bindings[ctx->bindingIndex];
  lv_color_t c = lv_colorwheel_get_rgb(lv_event_get_target(e));
  uint32_t rgb = lv_color_to32(c);
  int r  = (rgb >> 16) & 0xFF;
  int g  = (rgb >> 8)  & 0xFF;
  int bl = rgb & 0xFF;
  ui.noteActivity();
  ui._server.IndexSetColor(b.id, r, g, bl);
  ui._server.IndexSetEvent(b.id);
  char hex[8];
  snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, bl);
  ui.publishMqttAction(b, String(hex));
}''',
    '''void MiraPanelUI::onColor(lv_event_t* e) {
  EventContext* ctx = static_cast<EventContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->ui) return;
  MiraPanelUI& ui = *ctx->ui;
  if (ui._suppressEvents || ctx->bindingIndex >= ui._bindingCount) return;

  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;

  Binding& b = ui._bindings[ctx->bindingIndex];
  lv_color_t c = lv_colorwheel_get_rgb(lv_event_get_target(e));
  uint32_t rgb = lv_color_to32(c);
  int r  = (rgb >> 16) & 0xFF;
  int g  = (rgb >> 8)  & 0xFF;
  int bl = rgb & 0xFF;
  const uint32_t rgb24 = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
  ui.noteActivity();

  const uint32_t now = millis();
  const bool finalSend = code == LV_EVENT_RELEASED;
  const bool liveSend = (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSING) &&
                        rgb24 != b.lastLiveColor &&
                        (b.lastLiveSendMs == 0 || (uint32_t)(now - b.lastLiveSendMs) >= 100);
  if (liveSend || finalSend) {
    b.lastLiveSendMs = now;
    b.lastLiveColor = rgb24;
    ui._server.IndexSetColor(b.id, r, g, bl);
    ui._server.IndexSetEvent(b.id);
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, bl);
    ui.publishMqttAction(b, String(hex));
  }
}''',
    'live colour wheel events',
)

print('Mira Panel 0.5.7 live range/thermostat/color controls and thicker color wheel applied')
