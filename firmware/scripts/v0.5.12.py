from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('firmware/source/work/MIRA_PANEL')
ino = root / 'MIRA_PANEL.ino'
ui = root / 'MiraPanelUI.cpp'


def replace_once(path: Path, old: str, new: str, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly 1 match in {path}, got {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


def replace_exact(path: Path, old: str, new: str, expected: int, label: str):
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != expected:
        raise SystemExit(f'{label}: expected exactly {expected} matches in {path}, got {count}')
    path.write_text(text.replace(old, new), encoding='utf-8')


replace_once(ino, '"0.5.11"', '"0.5.12"', 'firmware version')
replace_once(ino, '[MIRA] Firmware 0.5.11 initialise', '[MIRA] Firmware 0.5.12 initialise', 'startup log')

# 0.5.10 accidentally made live movement MQTT-only and kept the legacy HTTP/event
# command path for RELEASED. Restore the server command path on every real value
# change while retaining the local anti-echo window added in 0.5.10.
replace_exact(
    ui,
    '''  if (liveSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveValue = value;\n    ui.publishMqttAction(b, String(value));\n  }''',
    '''  if (liveSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveValue = value;\n    ui._server.IndexSetRange(b.id, value);\n    ui._server.IndexSetEvent(b.id);\n    ui.publishMqttAction(b, String(value));\n  }''',
    2,
    'restore real-time HTTP/event for linear + round range',
)

replace_once(
    ui,
    '''  if (liveSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveColor = rgb24;\n    ui.publishMqttAction(b, String(hex));\n  }''',
    '''  if (liveSend) {\n    b.lastLiveSendMs = now;\n    b.lastLiveColor = rgb24;\n    ui._server.IndexSetColor(b.id, r, g, bl);\n    ui._server.IndexSetEvent(b.id);\n    ui.publishMqttAction(b, String(hex));\n  }''',
    'restore real-time HTTP/event for colorwheel',
)

print('Mira Panel 0.5.12 real-time HTTP/event commands restored; MQTT and anti-echo retained')
