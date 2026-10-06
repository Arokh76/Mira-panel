from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
mqtt = root / 'MiraMqtt.cpp'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


replace_once(ino, '"0.6.22"', '"0.6.23"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.6.22 initialise', '[MIRA] Firmware 0.6.23 initialise', 'startup log')

# UI must remain responsive even when Wi-Fi is technically connected but the
# optional MQTT broker is unreachable/slow. Service LVGL first.
replace_once(
    ino,
    '''  if (MIRA_MQTT_RUNTIME_ENABLED) Mqtt.loop();
  Interface.loop();
  delay(5);
''',
    '''  Interface.loop();
  if (MIRA_MQTT_RUNTIME_ENABLED) Mqtt.loop();
  delay(2);
''',
    'UI before MQTT in main loop',
)

# WiFiClient::setTimeout() does not cap TCP connect() time. Use the explicit
# connect timeout overload so a bad Wi-Fi route/broker cannot freeze LVGL.
replace_once(
    mqtt,
    '  if (!_net.connect(_host.c_str(), _port)) {\n',
    '  if (!_net.connect(_host.c_str(), _port, 180)) {\n',
    'MQTT TCP connect timeout',
)

# A local broker should return CONNACK almost immediately. Never hold the UI
# for 1.2 s waiting on optional MQTT.
replace_once(
    mqtt,
    '  if (!readPacket(header, response, sizeof(response), responseLen, 1200) || (header & 0xF0) != 0x20 || responseLen < 2 || response[1] != 0) {\n',
    '  if (!readPacket(header, response, sizeof(response), responseLen, 250) || (header & 0xF0) != 0x20 || responseLen < 2 || response[1] != 0) {\n',
    'MQTT CONNACK timeout',
)

# Back off failed broker attempts. MQTT is optional and must never dominate the
# main loop when the network is unstable.
replace_once(
    mqtt,
    '    if (millis() - _lastConnectAttempt >= 5000UL || _lastConnectAttempt == 0) {\n',
    '    if (millis() - _lastConnectAttempt >= 10000UL || _lastConnectAttempt == 0) {\n',
    'MQTT reconnect backoff',
)

print('Mira Panel 0.6.23 network-resilience pass: UI priority + bounded MQTT reconnect')
