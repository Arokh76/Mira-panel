#!/usr/bin/env python3
"""Ensure native IDF templates are byte-for-byte imports of the Arduino beta.

This is a source parity smoke test, not a hardware or network integration test.
Run from any working directory with python3 stdlib.
"""
from pathlib import Path
import json
import re

root = Path(__file__).resolve().parents[5]
src = root / "firmware/source/consolidated/MIRA_PANEL/HTML"
idf = root / "firmware/experiments/esp-idf/mira-idf/main"
native = (idf / "mira_beta_web_pages.h").read_text(encoding="utf-8")
handlers = (idf / "mira_beta_web.c").read_text(encoding="utf-8")
jeedom = (idf / "mira_jeedom.c").read_text(encoding="utf-8")
recovery = (idf / "mira_recovery.c").read_text(encoding="utf-8")
setup = (idf / "mira_beta_setup.c").read_text(encoding="utf-8")

pairs = {
    "HTML_Index.h": "mira_beta_index_html",
    "HTML_Parametres.h": "mira_beta_params_html",
    "HTML_MQTT.h": "mira_beta_mqtt_html",
    "HTML_ManageurWiFi.h": "mira_beta_wifi_manager_html",
}


def extract_native(name: str) -> str:
    marker = f"static const char {name}[] ="
    assert native.count(marker) == 1, f"Missing / duplicate {name}"
    remainder = native.split(marker, 1)[1]
    literals = []
    for line in remainder.splitlines():
        line = line.strip()
        if not line:
            continue
        done = line.endswith(";")
        if done:
            line = line[:-1].rstrip()
        assert line.startswith('"') and line.endswith('"'), (name, line[:100])
        literals.append(json.loads(line))
        if done:
            break
    assert literals, f"Empty template {name}"
    return "".join(literals)


for filename, symbol in pairs.items():
    original = (src / filename).read_text(encoding="utf-8")
    chunks = re.findall(r'R"rawliteral\(([\s\S]*?)\)rawliteral"', original)
    assert len(chunks) == 1, f"{filename} changed embedded structure"
    imported = extract_native(symbol)
    assert imported.startswith(chunks[0]), f"{filename} differs from beta"
    assert imported[len(chunks[0]):] in ("", "\n"), f"{filename} has appended content"
    print(f"PASS {filename}: original HTML preserved")

for route in ('mira_beta_web_home(req)', 'mira_jeedom_beta_web_action(req)',
              'mira_beta_web_authorized(req)'):
    assert route in recovery, f"Missing HTTP route dispatch: {route}"
assert 'mira_beta_web_load(req)' in setup, "Missing beta /load bridge"
for marker in ('mira_jeedom_render_beta_actions(r)', 'mira_beta_index_html',
               'mira_beta_params_html', 'mira_beta_mqtt_html',
               'mira_beta_wifi_manager_html', 'mira_beta_system_html'):
    assert marker in handlers, f"Missing page handler: {marker}"
for marker in ('uri="/events"', 'mira_jeedom_beta_web_action',
               'mira_jeedom_render_beta_actions'):
    assert marker in jeedom, f"Missing Jeedom beta UI compatibility: {marker}"
print("PASS beta page/control route wiring")
