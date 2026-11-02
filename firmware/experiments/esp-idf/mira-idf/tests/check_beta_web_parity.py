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
    "HTML_Donnees.h": "mira_beta_data_html",
    "HTML_ConfigAP.h": "mira_beta_config_ap_html",
    "HTML_ReseauxEnreg.h": "mira_beta_known_networks_html",
    "HTML_ModificationReseau.h": "mira_beta_edit_network_html",
    "HTML_Message.h": "mira_beta_message_html",
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
               'mira_beta_wifi_manager_html', 'mira_beta_system_html',
               'mira_beta_data_html', 'mira_beta_config_ap_html'):
    assert marker in handlers, f"Missing page handler: {marker}"
for marker in ('uri="/events"', 'mira_jeedom_beta_web_action',
               'mira_jeedom_render_beta_actions'):
    assert marker in jeedom, f"Missing Jeedom beta UI compatibility: {marker}"
# Ensure destructive operations never execute from a GET /load navigation.
data = (idf / "mira_beta_data.c").read_text(encoding="utf-8")
setup = (idf / "mira_beta_setup.c").read_text(encoding="utf-8")
recovery = (idf / "mira_recovery.c").read_text(encoding="utf-8")
assert 'mira_beta_data_confirm(req,nav)' in handlers, "Missing reset confirmation"
assert 'mira_beta_data_reset_post' in setup, "Missing POST-only reset endpoint"
assert 'reset_token=0;' in data and 'do_scope(operation)' in data, "Reset confirmation not single-use"
assert 'mira_recovery_save_beta_ap' in recovery and 'beta_ap_persistent||!station_connected' in recovery, "Beta AP settings not persisted/recovered"
networks = (idf / "mira_beta_networks.c").read_text(encoding="utf-8")
assert 'mira_beta_networks_list' in networks and 'mira_beta_networks_activate' in networks
assert 'mira_beta_networks_remove' in networks and 'mira_beta_networks_upsert' in networks
assert 'BETA_OVERLAY_KEY "wifi_profiles"' in networks
assert 'mira_beta_web_network_post' in setup
assert 'mira_beta_networks_remove(id)' in handlers and 'mira_beta_networks_activate(id)' in handlers
assert 'mira_beta_known_networks_html' in handlers and 'mira_beta_edit_network_html' in handlers
print("PASS beta network list, edit, activation and removal routes")
print("PASS beta Wi-Fi AP and reset safety contracts")
# Original beta LVGL programming is a staged sequence, not a write on
# every /addlistlvgl. All six routes must operate on the same pending list.
for name in ("addlistlvgl", "removelistlvgl", "savelistlvgl",
             "screenadd", "screenformat", "screensave"):
    assert f'uri="/{name}"' in jeedom, f"Missing beta /{name}"
assert "static cJSON *beta_lvgl_stage;" in jeedom
assert "beta_lvgl_dirty=true;" in jeedom
assert "rename(MIRA_JSON_TMP,MIRA_JSON_PATH)" in jeedom
assert "mira_full_request_reload();" in jeedom
print("PASS beta staged LVGL programming routes")
# Arduino firmware-beta and ESP-IDF use different binary formats/configurations;
# they must never share the same OTA manifest target, even on a 4.3C panel.
assert "HARDWARE_PROFILE='waveshare-esp32s3-touch-lcd-4.3c-idf'" in native
assert "HARDWARE_PROFILE='waveshare-esp32s3-touch-lcd-4.3c';" not in native
print("PASS IDF-only OTA release target guard")
# Camera parity: the beta supports HTTP and HTTPS MJPEG/JPEG cameras.
# TLS verification must be enabled in the ESP-IDF build (never insecure).
camera = (idf / "mira_camera.c").read_text(encoding="utf-8")
config = (idf.parent / "sdkconfig.defaults").read_text(encoding="utf-8")
assert 'strncmp(url,"https://",8)' in camera, "HTTPS URLs not supported"
assert '.crt_bundle_attach=esp_crt_bundle_attach' in camera, "Camera TLS CA trust disabled"
assert 'CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y' in config, "CA bundle missing"
assert 'decode_mjpeg_first_frame' in camera, "Multipart JPEG decoder missing"
print("PASS beta camera HTTPS/MJPEG compile configuration")
print("PASS beta page/control route wiring")
