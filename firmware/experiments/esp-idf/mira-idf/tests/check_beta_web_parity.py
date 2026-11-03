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
# The first physical Waveshare 4.3C setup entered HTTP 403 on
# /save?config=key despite access via the recovery AP. The initial wizard
# must still be reachable when the softAP is active and STA has no IP;
# this must not weaken protected production routes or recovery OTA.
assert "static bool first_run_ap_access(httpd_req_t *req)" in setup
assert 'esp_wifi_get_mode(&mode)==ESP_OK' in setup
assert '(mode==WIFI_MODE_AP||mode==WIFI_MODE_APSTA)' in setup
assert 'esp_netif_get_ip_info(sta_netif,&sta_ip)==ESP_OK' in setup
assert 'sta_ip.ip.addr==0' in setup
assert setup.count("if(!first_run_ap_access(req))return ESP_FAIL;")==2
assert 'if(!mira_setup_require_ap(req))return ESP_FAIL;' in recovery
assert 'mira_beta_web_authorized(req)' in (idf / "mira_beta_ota.c").read_text(encoding="utf-8")
assert 'ESP_LOGI(TAG,"Mira beta setup step 1/2: local screen key saved");\n        /* Match firmware-beta' in setup
assert 'return serve_wifi_page(req);' in setup.split('if(!strcmp(config,"key")){',1)[1].split('if(!strcmp(config,"wifi")){',1)[0]
assert 'httpd_resp_set_status(req,"303 See Other")' not in setup
print("PASS first-install beta direct key-to-Wi-Fi render, production and OTA access unchanged")
# The direct-served key/Wi-Fi/saved HTML templates are not fed through the
# percent-unescaping template renderer used by other beta pages.
setup_pages=(idf / "mira_beta_setup_pages.h").read_text(encoding="utf-8")
assert '%%' not in setup_pages, "Direct-served setup CSS must use single literal %"
assert 'nvs_set_u8(nv,"ap_enabled",1)' in setup
assert 'esp_restart();' in setup.split('if(!strcmp(nav,"reboot")',1)[1].split('static esp_err_t save_post',1)[0]
assert "Redémarrage de l'appareil en point d'accès Mira" in setup
print("PASS original setup CSS percentages and keep-AP reboot semantics")
# First real 4.3C USB boot crash, 2026-11-03:
# InstrFetchProhibited PC=0x00000008 immediately after captive DNS was logged.
# Recovery's 16-profile array alone occupied 3456 bytes on the small default
# ESP-IDF main task stack. Keep it on heap and reserve sufficient startup stack.
config = (idf.parent / "sdkconfig.defaults").read_text(encoding="utf-8")
assert "CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192" in config
assert "mira_beta_network_t profiles[MIRA_NET_MAX]" not in recovery
assert "mira_beta_network_t *profiles=calloc(MIRA_NET_MAX,sizeof(*profiles))" in recovery
assert "free(profiles);" in recovery
assert 'Startup stack available after DNS:' in recovery
assert 'Startup stack available before HTTP:' in recovery
print("PASS first-boot stack overflow regression guards")

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
assert 'decode_mjpeg_first_frame' in camera, "Multipart JPEG fallback missing"
assert 'camera_stream_mjpeg(url,current)' in camera, "Persistent MJPEG worker missing"
assert 'camera_stream_current(expected)' in camera, "Live camera cancellation missing"
assert 'decode_jpeg_buffer(stream->jpeg,stream->used,&frame)' in camera, "Stream JPEG decoder missing"
assert 'CAM_MAX_JPEG' in camera and '250000LL' in camera, "MJPEG memory/FPS limits missing"
assert 'esp_http_client_is_complete_data_received(http)' in camera, "MJPEG EOF not handled"
assert 'stream_detected=false;' in camera and 'stream_tested=false;' in camera, "Camera URL change not handled"
print("PASS beta camera HTTPS/MJPEG compile configuration")
# Normal routes must serve the actual beta, not the former experimental HTML.
full = (idf / "mira_full.c").read_text(encoding="utf-8")
assert '.uri="/recovery/mira"' in full
assert '.uri="/mira"' in full and 'legacy_mira_get' in full
assert 'if(!mira_beta_web_authorized(req))return ESP_FAIL;' in full
assert 'mira_full_enqueue_state(name->valuestring,val)' in full
print("PASS production UI routing and authenticated state injection")
# Dimitri JSON-B1 lesson on IDF: bounded cJSON cache, no repeat writes and
# observable state-pressure counters (not ArduinoJson garbageCollect()).
assert 'mira_full_state_diagnostics' in full
assert 'passive_queue_dropped' in full
assert 'passive_evictions' in full
assert 'passive_unchanged' in full
assert 'if(i<state_cache_count &&' in full
assert 'state_queue_dropped' in jeedom and 'state_cache_evictions' in jeedom
print("PASS Dimitri-inspired passive state cache diagnostics")
# Visual parity with MiraPanelUI 0.7.37 (Waveshare 4.3C).
beta_ui = (root / "firmware/source/consolidated/MIRA_PANEL/MiraPanelUI.cpp").read_text(encoding="utf-8")
for color in ("0x071421", "0x071A28", "0x090D12", "0x0E2032",
              "0x103047", "0x141B22", "0x173149", "0x17445D",
              "0x202A33", "0x17354B", "0x2B6882", "0x33414C",
              "0x2ABEFF", "0x5BDEFF", "0x31BDF2"):
    assert color in beta_ui and color in full, f"Missing original beta palette value: {color}"
assert 'lv_dropdown_set_options(theme_choice,"Mira\\nSombre\\nGlass")' in full
assert 'save_sleep_setting_u8("theme",ui_theme)' in full
assert 'theme_reload_pending=true;' in full
assert 'theme_apply_on_lvgl_task();' in full
assert 'wheel_prepare_bitmap()' in full and 'wheel_touch' in full
assert 'wheel_mark_hue(w,hsv.h)' in full, "Passive RGB must update hue selector"
assert 'lv_color_hsv_to_rgb(w->color_hue' in full or 'const uint16_t hue=b->color_hue;' in full
assert 'mira_fit_info_value(w->value_label,w->card)' in full
assert 'lv_text_get_size(&measured' in full, "Fit must use actual glyph width"
assert 'scroll_cache_prepare(i)' in full and 'scroll_cache_activate(false)' in full
assert 'lv_snapshot_take_to_buf' in full and 'LV_COLOR_FORMAT_RGB565' in full
assert 'scroll_cache_reset(); // release every LVGL image reference' in full
print("PASS original palette, 3 themes, hue wheel, dynamic text and safe RGB565 scroll cache")
glyph = (idf / "mira_info_glyphs.c").read_text(encoding="utf-8")
for kind in ("temperature","humidity","pressure","light","door","energy","plug"):
    assert f'"{kind}"' in glyph, f"Original beta glyph missing: {kind}"
assert 'mira_info_glyph_create' in glyph and 'mira_info_glyphs.c' in (idf / "CMakeLists.txt").read_text(encoding="utf-8")
assert 'mira_decorate_info(w,str(it,"icon","auto")' in full
assert 'str(it,"labelMode","auto")' in full
assert 'mira_fit_icon_info_value(w)' in full
assert 'w->info_name=nm;' in full
assert 'if(hero||value_style)' in full and 'hero?84:72' in full
assert 'wide?56:(compact?30:36)' in full
assert '"MAJ %02d:%02d"' in full
assert 't->dsc.header.stride=dst_stride;' in full, "RGB565 cached tiles require own row stride"
assert 'lv_obj_get_parent(widgets[i].card)==pages[current_page]' in full
print("PASS beta INFO icons, display modes, dimensions, last-seen and snapshot stride")
# Multimedia is the native Spotify-like beta widget, NOT a key/value grid.
album = (idf / "mira_album_art.c").read_text(encoding="utf-8")
assert 'w->card=card(pg,786,558)' in full
assert 'media_add_button(w,0,261,LV_SYMBOL_PREV)' in full
assert 'media_add_button(w,1,357,LV_SYMBOL_PLAY)' in full
assert 'media_add_button(w,2,453,LV_SYMBOL_NEXT)' in full
assert 'media_artwork_image' in full and 'artwork_poll();' in full
assert 'mira_album_art_request(index,msg->value)' in full
assert 'https://i.scdn.co/image/' in album
assert 'https://mosaic.scdn.co/' in album
assert 'https://yt3.googleusercontent.com/' in album
assert 'mira_camera_fetch_url(url,&frame)' in album
assert 'mira_album_art.c' in (idf / "CMakeLists.txt").read_text(encoding="utf-8")
# Jeedom and MQTT cover URLs must reach the artwork worker without being
# truncated to a 159-byte ordinary info state.
mqtt = (idf / "mira_mqtt.c").read_text(encoding="utf-8")
jeedom = (idf / "mira_jeedom.c").read_text(encoding="utf-8")
assert 'mira_cover_update_t' in full and 'cover_update_queue' in full
assert 'mira_album_art_request(i,cover.url)' in full
assert 'const size_t limit=cover?768u:159u;' in mqtt
assert 'char value[769]={0};' in mqtt and 'char value[160]={0}' not in mqtt
assert 'char selected[24]={0},action_id[16]={0},value[769]={0};' in jeedom
assert 'char encoded[1024];' in jeedom
assert 'strlen(value)>(is_cover?768u:159u)' in jeedom
print("PASS beta media art/cover layout with HTTPS background decoding")
# The beta energy widget is a colored 2x2 dashboard with optional battery
# SOC percentage bars; the old flat text table was not visual parity.
assert 'w->card=card(pg,786,344)' in full
for caption in ('"SOLAIRE"','"MAISON"','"RÉSEAU"','"BATTERIE"','"STOCKAGE"','"BATTERIE 2"','"BATTERIE 3"'):
    assert caption in full, f"Energy dashboard missing: {caption}"
assert 'w->energy_gauge[i]=lv_bar_create(w->card)' in full
assert 'strchr(msg->value,\'%\')!=NULL' in full
assert 'lv_obj_set_height(w->card,344+47*extra_rows)' in full
print("PASS native beta energy dashboard, SOC display and optional battery rows")
assert 'static void mira_media_display_text(' in full
assert 'mira_media_display_text(msg->value,shown,sizeof(shown))' in full
assert '"i\\xCC\\x82"' in full and '"\\xE2\\x80\\x99"' in full
print("PASS Spotify NFC display labels and punctuation compatibility")
# Only a published IDF-specific target may result in 'available'; the
# Arduino firmware target must never be silently reused.
update = (idf / "mira_beta_update.c").read_text(encoding="utf-8")
assert 'MIRA_IDF_PROFILE "waveshare-esp32s3-touch-lcd-4.3c-idf"' in update
assert 'if(!valid||newer==-2)' in update
assert 'esp_crt_bundle_attach' in update
assert 'mira_beta_update_check' in setup
assert 'mira_beta_update_start' in setup
assert 'mira_beta_update_legacy_page' in setup
assert 'mira_beta_update.c' in (idf / "CMakeLists.txt").read_text(encoding="utf-8")
print("PASS IDF-only update routes and trusted manifest HTTPS")
# The WT32 beta web navigation includes /load?nav=id (HTTP IDs),
# /load?nav=checkupdate (update checker), and legacy keyed GET /update
# commands. The first IDF web port showed pages but omitted these routes.
web=(idf / "mira_beta_web.c").read_text(encoding="utf-8")
jeedom=(idf / "mira_jeedom.c").read_text(encoding="utf-8")
update=(idf / "mira_beta_update.c").read_text(encoding="utf-8")
assert 'if(!strcmp(nav,"id"))return mira_jeedom_render_http_reference(req);' in web
assert 'if(!strcmp(nav,"checkupdate"))return beta_checkupdate_page(req);' in web
assert 'if(!strcmp(nav,"gpio"))return beta_43c_gpio_page(req);' in web
assert 'esp_err_t mira_beta_web_gpio_state(httpd_req_t *req)' in web
assert 'esp_err_t mira_beta_web_gpio_set(httpd_req_t *req)' in web
assert 'mira_beta_web_authorized(req)' in web
assert '"X-Mira-GPIO"' in web
assert 'mira_display_io_write(out,high)' in web
assert 'mira_display_io_read(&di0,&di1,&do0,&do1)' in web
assert '.uri="/gpio/state"' in setup and '.uri="/gpio/set"' in setup
display=(idf / "mira_display.c").read_text(encoding="utf-8")
display_h=(idf / "mira_display.h").read_text(encoding="utf-8")
assert 'ioexp_write(ioexp_device,0x02,0xde)' in display
assert 'ioexp_write(ioexp_device,0x03,0x3f)' in display
assert 'ioexp_output_shadow=0x3f;' in display
assert 'ioexp_write(ioexp_device, 0x03, 0xff)' not in display
assert 'ioexp_write(ioexp_device, 0x02, 0xff)' not in display
assert 'mira_display_io_write(unsigned channel,bool level)' in display
assert 'mira_display_io_read(bool *di0,bool *di1,bool *do0,bool *do1)' in display
assert 'SemaphoreHandle_t ioexp_lock' in display
assert 'mira_display_io_read' in display_h and 'mira_display_io_write' in display_h
print("PASS 4.3C isolated I/O direction and default OFF, UI routes and access protection")
assert "static esp_err_t beta_checkupdate_page(httpd_req_t *req)" in web
assert 'fetch(\'/api/checkupdate\'' in web
assert 'Mise à jour Arduino/WT32 ne doit jamais être installée ici.' in web or "Une mise à jour Arduino/WT32 ne doit jamais être installée ici." in web
assert 'esp_err_t mira_jeedom_render_http_reference(httpd_req_t *req)' in jeedom
assert 'esp_err_t mira_jeedom_beta_legacy_get_update(httpd_req_t *req)' in jeedom
assert 'if(!require_key(req))return ESP_FAIL;' in jeedom.split('esp_err_t mira_jeedom_beta_legacy_get_update',1)[1].split('esp_err_t mira_jeedom_render_http_reference',1)[0]
assert 'mira_full_enqueue_state(name,value)' in jeedom
assert 'mira_jeedom_queue_local_action(name,type,value)' in jeedom
assert 'return mira_jeedom_beta_legacy_get_update(req);' in update
assert 'mira_beta_web_authorized(req)' in update
assert 'return httpd_resp_send_chunk(req,NULL,0);' in jeedom.split('esp_err_t mira_jeedom_render_http_reference',1)[1].split('static esp_err_t getlvgl',1)[0]
print("PASS beta HTTP references, native update checker and authenticated legacy GET command routing")
print("PASS beta page/control route wiring")

# Hardware parity: the Arduino 4.3C registers multi-action IDs but does not
# draw the GPIO-only WT32 multi-action control; don't create a fake touch tile.
assert 'if(!strcmp(type,"multiaction"))return;' in full
assert 'MIRA_TARGET_WAVESHARE_43C' in beta_ui
assert 'WT32MultiVisible(name)' in beta_ui
assert 'append_action(arr,&id,"button",compound);' in jeedom
# Composite Jeedom identity must preserve the beta's media/weather metadata.
assert 'if(a && (n==4||n==5))cJSON_AddStringToObject(a,"unit","%");' in jeedom
assert 'cJSON_AddNumberToObject(a,"max",11);' in jeedom
assert 'cJSON_AddNumberToObject(a,"value",40);' in jeedom
assert 'if(weather)cJSON_AddStringToObject(weather,"unit","°C");' in jeedom
print("PASS Waveshare-only hidden multiactions and Jeedom media/weather index parity")
