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
assert 'ota_manifest_entry(url,hash,&bytes,remote)' in update
assert 'ota_manifest_entry(url,digest,&expected,remote_version)' in update
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
assert 'if(!strcmp(nav,"system"))return render_system_43c(req);' in web
assert 'Configurer les E/S' in web
assert 'esp_err_t mira_beta_web_gpio_state(httpd_req_t *req)' in web
assert 'esp_err_t mira_beta_web_gpio_set(httpd_req_t *req)' in web
assert 'mira_beta_web_authorized(req)' in web
assert '"X-Mira-GPIO"' in web
assert 'mira_gpio43_write((unsigned)i,state[0]==\'1\')' in web
assert 'mira_gpio43_state(&state)' in web
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
assert 'if(!strcmp(type,"multiaction")&&!mira_gpio43_multi_visible(name))return;' in full
assert 'MIRA_TARGET_WAVESHARE_43C' in beta_ui
assert 'WT32MultiVisible(name)' in beta_ui
assert 'append_action(arr,&id,"button",compound);' in jeedom
# Composite Jeedom identity must preserve the beta's media/weather metadata.
assert 'if(a && (n==4||n==5))cJSON_ReplaceItemInObjectCaseSensitive(a,"unit",cJSON_CreateString("%"));' in jeedom
assert 'cJSON_AddNumberToObject(a,"max",11);' in jeedom
assert 'cJSON_AddNumberToObject(a,"value",40);' in jeedom
assert 'if(weather)cJSON_ReplaceItemInObjectCaseSensitive(weather,"unit",cJSON_CreateString("°C"));' in jeedom
print("PASS Waveshare-only hidden multiactions and Jeedom media/weather index parity")

# Beta IndexAddInfo does not reuse the JSON widget's default value.
assert 'cJSON_AddStringToObject(entry,"value","");' in jeedom
assert 'cJSON_AddStringToObject(entry,"return","true");' in jeedom
# The original MiraPanelUI.cpp normalizes range values and half-degree
# thermostat setpoints before registering the Jeedom command IDs.
assert 'if(hi<=lo)hi=lo>INT_MAX-100?INT_MAX:lo+100;' in jeedom
assert 'if(current<lo)current=lo;' in jeedom
assert 'if(current>hi)current=hi;' in jeedom
assert 'setpoint=roundf(setpoint*2.0f)*0.5f;' in jeedom
assert 'if(lo< -20)lo= -20;' in jeedom
assert 'if(lo>60)lo=60;' in jeedom
assert 'if(hi>80)hi=80;' in jeedom
assert 'if(id==before_modes)add_choices(arr,&id,compound,"Confort");' in jeedom
print("PASS beta Jeedom info/range/thermostat index registration parity")

# The original MiraMqtt publishes 11 retained Display/<key>/state
# channels immediately after connection and again on settings changes.
# Native IDF must not omit the outbound display feedback just because the
# inbound /set subscriptions are present.
assert 'void mira_mqtt_publish_display_states(const mira_mqtt_display_state_t *state)' in mqtt
assert 'last_display_signature[0]=0;' in mqtt
assert 'now-last_display_check_us<15000000LL' in mqtt
assert 'esp_mqtt_client_enqueue(client,topic,values[i],0,0,1,true)' in mqtt
for display_key in ("day","night","sleep_brightness","sleep_timeout",
                    "night_auto","night_start","night_end","clock_sleep",
                    "brightness","mode","state"):
    assert '"'+display_key+'"' in mqtt
assert 'mira_mqtt_publish_display_states(&status);' in full
assert 'status.brightness=brightness==0?0:' in full
assert 'mira_mqtt_display_state_t' in (idf / "mira_mqtt.h").read_text(encoding="utf-8")
print("PASS beta retained MQTT display state publication on connect and change")

# Arduino MiraPanelServer::GenericCmd is a real fallback after HTTP/Jeedom
# is disabled: Paramètres saves each widget URL with #value# substitution.
# It must be queued on the HTTP worker, not silently sent only via MQTT.
assert 'bool mira_generic_queue_local_action(const char *name,const char *type,' in jeedom
assert 'static void send_generic_action(const mira_action_msg_t *msg)' in jeedom
assert 'else if(!strcmp(action.type,"__generic"))send_generic_action(&action);' in jeedom
assert 'mira_generic_queue_local_action(name,type,value)' in jeedom
assert 'mira_other_transport(action_name,action_type,payload)' in jeedom
assert 'mira_other_transport(name,type,value)' in jeedom
assert 'if(mira_mqtt_publish_action(name,value))return true;' in jeedom
assert 'if(!strcmp(type,"list")||!strcmp(type,"listbutton"))' in jeedom
assert 'snprintf(command_value,sizeof(command_value),"%%23%02X%02X%02X"' in jeedom
assert 'chunk=command_value;' in jeedom
assert 'esp_http_client_perform(client)' in jeedom
assert 'static bool mira_beta_user_action(' in full
assert 'mira_jeedom_queue_local_action(name,type,value)' in full
assert 'mira_mqtt_publish_action(mqtt_name?mqtt_name:name,' in full
assert 'mira_generic_queue_local_action(name,type,value)' in full
assert 'mira_beta_user_action(w->name,"listbutton",value,' in full
assert 'mira_beta_user_action(command,(action>=3)?"range":"button",' in full
assert 'mira_format_hhmm(status.night_start,begin)' in full
assert 'mira_format_hhmm(status.night_end,finish)' in full
assert 'snprintf(status.night_start,sizeof(status.night_start)' not in full
print("PASS beta asynchronous GenericCmd configured URL fallback and fixed-size HH:MM")
# Exactly match firmware-beta GetParamBool("Jeedom") absent => false.
assert 'static volatile bool jeedom_mode_enabled=false;' in jeedom
assert 'bool enabled=false; // Match beta GetParamBool("Jeedom") when absent.' in jeedom
assert 'if(!strcmp(name,"Jeedom"))jeedom_mode_enabled=!strcmp(value,"1");' in jeedom
print("PASS beta disabled-by-default Jeedom transport and explicit setting override")


# Source-derived contracts: keep comparing against the firmware-beta code,
# not just a manually maintained list of features. Future beta additions
# must fail this parity check until they are explicitly ported.
legacy_widget_types=set(re.findall(r'\btype\s*==\s*"([^"]+)"',beta_ui))
idf_types_block=full.split('static const char *const supported[]={',1)[1].split('};',1)[0]
native_widget_types=set(re.findall(r'"([^"]+)"',idf_types_block))
assert legacy_widget_types==native_widget_types, (
    "Widget types missing/extra compared to beta",
    sorted(legacy_widget_types-native_widget_types),
    sorted(native_widget_types-legacy_widget_types)
)
legacy_mqtt=(root / "firmware/source/consolidated/MIRA_PANEL/MiraMqtt.cpp").read_text(encoding="utf-8")
legacy_display_keys=set(re.findall(r'publishDisplayState\("([^"]+)"',legacy_mqtt))
native_names_block=mqtt.split('static const char *const names[]={',1)[1].split('};',1)[0]
native_display_keys=set(re.findall(r'"([^"]+)"',native_names_block))
assert legacy_display_keys==native_display_keys, (
    "MQTT Display topics differ from firmware-beta",
    sorted(legacy_display_keys-native_display_keys),
    sorted(native_display_keys-legacy_display_keys)
)
print("PASS source-derived parity: all beta LVGL widget types and MQTT Display topics")

# Derive the exact original metadata from IndexAddButton/IndexAddInfo/List.
legacy_server=(root/"firmware/source/consolidated/MIRA_PANEL/MiraPanelServer.cpp").read_text(encoding="utf-8")
legacy_header=(root/"firmware/source/consolidated/MIRA_PANEL/MiraPanelServer.h").read_text(encoding="utf-8")
assert 'int IndexAddButton(String Name, bool Return_Line = true)' in legacy_header
assert 'int IndexAddInfo(String Name, String Unit = "", String Size = "60")' in legacy_header
assert 'cJSON_AddStringToObject(entry,"size","60");' in jeedom
assert 'cJSON_AddStringToObject(entry,"unit","");' in jeedom
assert 'cJSON_AddNumberToObject(entry,"value",first_id);' in jeedom
assert 'cJSON_AddStringToObject(entry,"value",text);' not in jeedom
assert 'Action["value"] = Listes_Value[k];' in legacy_server
assert 'Action["value"] = IDAction - 1;' in legacy_server
assert 'Action["value"] = IDAction - 1' in legacy_server
print("PASS source-derived Jeedom button/info/list metadata for every compound widget")

# Jeedom-beta core/class/mira.class.php event() echoes the command ID for
# both actions and passive infos. A generic HTTP 200 is not an acknowledgement.
# Jeedom plugin is on the separate jeedom-beta branch (not this checkout).
# Its event() reply was inspected directly: it echoes init('id') for actions
# and infos. Keep the local transport/parser contracts tested in CI.
assert 'static esp_err_t capture_jeedom_ack(esp_http_client_event_t *event)' in jeedom
assert 'acknowledgement.overflow' in jeedom
assert '!strcmp(reply,remote_id)' in jeedom
assert 'for(unsigned attempt=1;attempt<=3;attempt++)' in jeedom
assert 'if(attempt<3)vTaskDelay(pdMS_TO_TICKS(3000));' in jeedom
assert 'if(confirmed){' in jeedom
assert 'jeedom_action_worker' in jeedom
print("PASS beta Jeedom acknowledgement by executed command ID, bounded retries, single worker")


# All four beta GPIO endpoints must be adapted to the Waveshare isolated
# IO-extension, including persist-on-boot, named links and DI multi-gestures.
gpio=(idf/"mira_gpio43.c").read_text(encoding="utf-8")
gpio_h=(idf/"mira_gpio43.h").read_text(encoding="utf-8")
for route in ("/gpio/config","/gpio/link","/gpio/raw","/gpio/multi/config"):
    assert '.uri="'+route+'"' in setup, route
for handler in ("mira_beta_web_gpio_config","mira_beta_web_gpio_link",
                "mira_beta_web_gpio_raw","mira_beta_web_gpio_multi"):
    assert "esp_err_t "+handler+"(httpd_req_t *req)" in web, handler
assert 'mira_gpio43_init()' in (idf/"app_main.c").read_text(encoding="utf-8")
assert '"mira_gpio43.c"' in (idf/"CMakeLists.txt").read_text(encoding="utf-8")
assert 'MIRA_DEBOUNCE_MS 35u' in gpio and 'MIRA_MULTICLICK_MS 370u' in gpio
assert 'MIRA_HOLD_MS 900u' in gpio
assert 'mira_display_io_read(&di0,&di1,&do0,&do1)' in gpio
assert 'mira_display_io_write(output,physical)' in gpio
assert 'nvs_open("mira43gpio",NVS_READWRITE' in gpio
assert 'nvs_open("mira43links",NVS_READWRITE' in gpio
assert 'nvs_open("mira43multi",NVS_READWRITE' in gpio
assert 'mira_gpio43_toggle_mapped(b->name)' in full
assert 'mira_gpio43_action_mapped(b->name)' in full
assert 'if(!strcmp(type,"multiaction")&&!mira_gpio43_multi_visible(name))return;' in full
assert 'mira_gpio43_multi_visible(const char *name)' in gpio_h
print("PASS Waveshare isolated GPIO full beta config/link/multi route and local-control parity")


# Strict HTTP endpoint comparison derived from the actual Arduino beta source,
# across all native modules, not merely a manually enumerated subset.
import glob
beta_routes=set((method,path) for path,method in
    re.findall(r'server\.on\(\s*"([^"]+)"\s*,\s*HTTP_(GET|POST)',legacy_server))
native_source="\n".join(p.read_text(encoding="utf-8") for p in sorted(idf.glob("*.c")))
native_routes=set((method,path) for path,method in re.findall(
    r'\.uri\s*=\s*"([^"]+)"\s*,\s*\.method\s*=\s*HTTP_(GET|POST)',native_source))
missing_routes=sorted(beta_routes-native_routes)
assert not missing_routes, "Missing beta HTTP routes: "+repr(missing_routes)
assert len(beta_routes)>=48, ("Unexpected beta route count",len(beta_routes))
assert 'mira_gpio43_write((unsigned)i,state[0]==\'1\')' in web
print("PASS all %d original firmware-beta HTTP routes registered on IDF" % len(beta_routes))

# Automated OTA is allowed only after authenticated local POST; native-only
# manifest, matching SHA-256, exact bytes and app descriptor verification.
assert '.uri="/updatemodule",.method=HTTP_POST' in setup
assert '.uri="/api/otastatus",.method=HTTP_GET' in setup
assert 'if(!mira_beta_web_authorized(req))return ESP_FAIL;' in update
assert '"X-Mira-OTA"' in update
assert 'bool ota_valid_hex(const char *hex)' in update
assert 'mbedtls_md_finish(&md,raw_hash)' in update
assert 'strcasecmp(hex,digest)!=0' in update
assert 'esp_ota_get_partition_description(partition,&candidate)' in update
assert 'strcmp(candidate.project_name,"mira_idf_scroll")!=0' in update
assert 'strcmp(candidate.version,remote_version)!=0' in update
assert 'esp_ota_set_boot_partition(partition)' in update
assert 'ota_active' in update
assert 'if(!mira_beta_web_authorized(req))return ESP_FAIL;' in (idf/"mira_beta_ota.c").read_text(encoding="utf-8")
assert 'esp_ota_get_partition_description(target,&candidate)' in (idf/"mira_beta_ota.c").read_text(encoding="utf-8")
assert 'set(PROJECT_VER "0.7.37")' in (idf.parent/"CMakeLists.txt").read_text(encoding="utf-8")
assert "id='install'" not in web or "Installer la mise à jour IDF" in web
print("PASS native OTA auto/manual target isolation, authenticated trigger and SHA-256 contract")

# Deep source-derived contract audit: compare every GET/POST historical
# AsyncWebServer route to the registered esp_http_server routes. The 4.3C
# has native isolated DI/DO bindings, so GPIO routes must remain present
# too, not dropped as if the hardware had no I/O.
beta_server=(root/"firmware/source/consolidated/MIRA_PANEL/MiraPanelServer.cpp").read_text(encoding="utf-8")
native_sources={
    p.name:p.read_text(encoding="utf-8")
    for p in idf.glob("*.c")
}
legacy_routes=set(
    (verb,path)
    for path,verb in re.findall(
        r'server\.on\(\s*"([^"]+)"\s*,\s*HTTP_(GET|POST)',beta_server)
)
native_routes=set(
    (verb,path)
    for body in native_sources.values()
    for path,verb in re.findall(
        r'\.uri\s*=\s*"([^"]+)"\s*,\s*\.method\s*=\s*HTTP_(GET|POST)',
        body,re.S
    )
)
missing=sorted(legacy_routes-native_routes)
assert not missing, f"Unported beta HTTP routes: {missing}"
assert all(
    ("POST",path) in native_routes
    for path in ("/gpio/config","/gpio/link","/gpio/raw",
                 "/gpio/multi/config","/gpio/set")
)
gpio=native_sources["mira_gpio43.c"]
assert 'const bool mapped=slot>=0&&links[slot].kind<2;' in gpio
assert 'mira_gpio43_toggle_mapped' in gpio
assert 'mira_gpio43_multi_visible' in gpio
assert 'mira_gpio43_write_physical' in gpio
assert 'mira_gpio43_state' in gpio
assert 'mira_gpio43_init' in gpio
assert 'mira_beta_web_gpio_multi' in setup
assert 'mira_beta_web_gpio_link' in setup
print(f"PASS all {len(legacy_routes)} beta GET/POST routes present, "
      "including Waveshare native isolated GPIO")

# Jeedom-beta plugin confirms actions AND infos with exactly the remote
# command ID. Preserve bounded acknowledgment capture and at-most-three
# sends with 3-second retry spacing (beta SendQueud).
jeedom_plugin=(root/"core/class/mira.class.php")
# Plugin lives in a separate jeedom-beta branch, so source retrieval is
# external to this CI checkout. Keep validation of the native contract here.
assert 'capture_jeedom_ack' in jeedom
assert 'acknowledgement.overflow' in jeedom
assert 'for(unsigned attempt=1;attempt<=3;attempt++)' in jeedom
assert 'vTaskDelay(pdMS_TO_TICKS(3000))' in jeedom
assert '!strcmp(reply,remote_id)' in jeedom
assert 'same_index_structure(local,remote)' in jeedom
assert 'ui_config_hash(config)' in jeedom
assert 'if(!mapped||strncmp(binding,expected_prefix,9)!=0)return;' in jeedom
print("PASS Jeedom response acknowledgment, retry and index mapping safeguards")

# The check and installer must share EXACTLY one eligibility validator.
update=native_sources["mira_beta_update.c"]
assert 'static bool ota_manifest_entry(' in update
check=update.split('esp_err_t mira_beta_update_check(',1)[1].split(
    'esp_err_t mira_beta_update_legacy_page(',1)[0]
assert 'ota_manifest_entry(url,hash,&bytes,remote)' in check
assert 'ota_manifest_entry(url,digest,&expected,remote_version)' in update
assert 'strcasecmp(hex,digest)!=0' in update
assert 'esp_ota_get_partition_description(partition,&candidate)' in update
assert 'strcmp(candidate.project_name,"mira_idf_scroll")!=0' in update
assert 'esp_ota_set_boot_partition(partition)' in update
assert 'mira_beta_update_trigger' in setup
assert 'mira_beta_update_status' in setup
print("PASS IDF OTA install and version check share exact target/hash policy")


# In-depth behavioral parity contracts from the *actual Arduino source*.
# These are not merely route/widget-count checks.
beta_ui_header=(root/"firmware/source/consolidated/MIRA_PANEL/MiraPanelUI.h").read_text(encoding="utf-8")
beta_ui_impl=(root/"firmware/source/consolidated/MIRA_PANEL/MiraPanelUI.cpp").read_text(encoding="utf-8")
beta_server_impl=(root/"firmware/source/consolidated/MIRA_PANEL/MiraPanelServer.cpp").read_text(encoding="utf-8")
assert "MAX_BINDINGS = 40" in beta_ui_header
assert "#define MAX_WIDGETS 40" in full
assert "if(count>40)valid=false;" in jeedom
assert 'if(nvs_open("mira_idf_jeed",NVS_READONLY,&nv)!=ESP_OK)' in jeedom
assert 'return httpd_resp_sendstr(req,"Null");' in jeedom
assert 'const bool ready=saved_mode&&!strcmp(on,"1")&&' in jeedom
assert 'const bool mapped=slot>=0&&links[slot].kind<2;' in gpio
print("PASS exact 40-widget beta limit, no implicit Jeedom and no read-only GPIO interception")

# Arduino IndexSetToggle stores symbolic On/Off, not MQTT booleans; /get
# must return the symbolic value even when the widget cache has 1 or 0.
assert 'Value_0 = "Off";' in beta_server_impl
assert 'Value_1 = "On";' in beta_server_impl
assert 'snprintf(result,sizeof(result),"%s",on?"On":"Off");' in jeedom
assert 'jstr((cJSON*)entry,"state","Off")' in jeedom
assert 'return httpd_resp_sendstr(req,err==ESP_OK?value:"Null");' in jeedom
print("PASS Arduino symbolic toggle GET and absent parameter readback")

# IndexAddList stores an INTEGER initial selected ID on ALL option rows.
# The previously used string default for later options diverged from the
# Jeedom plugin's JSON expectations.
assert 'Action["value"] = Listes_Value[k];' in beta_server_impl
assert 'cJSON_AddNumberToObject(entry,"value",first_id);' in jeedom
assert 'cJSON_AddStringToObject(entry,"value",text);' not in jeedom
print("PASS numeric list default types across all option rows")

# Arduino trims the outer MQTT envelope, parses {"value":...} when present,
# then dispatches the decoded text; a 150-character value enclosed in JSON
# must not be rejected just because the raw envelope is >159 bytes.
assert 'String value = payload;' in beta_ui_impl
assert 'value.trim();' in beta_ui_impl
assert 'value.startsWith("{")' in beta_ui_impl
assert 'static bool mqtt_decode_payload(' in mqtt
assert 'while(leading<used&&isspace((unsigned char)envelope[leading]))leading++;' in mqtt
assert 'cJSON_GetObjectItemCaseSensitive(json,"value")' in mqtt
assert 'final_len<=max_value' in mqtt
assert 'mqtt_decode_payload(payload,plen,value,sizeof(value),limit)' in mqtt
assert 'if(mira_full_enqueue_display_command(key,value)){' in mqtt
assert 'last_display_signature[0]=0;' in mqtt
assert 'out.trim();' in beta_ui_impl
assert 'while(*src&&isspace((unsigned char)*src))src++;' in full
print("PASS beta MQTT envelope decoding, topic trimming and immediate display feedback")


# Arduino's outgoing control payloads, clock schedule and MQTT prefix rules
# are observable API contracts: an identical UI may still break automations.
assert 'snprintf(hex, sizeof(hex), "#%02X%02X%02X"' in beta_ui_impl
assert 'snprintf(b->value,sizeof(b->value),"#%02X%02X%02X"' in full
assert 'if(msg->value[0]==\'#\'&&strlen(msg->value)==7)' in jeedom
assert 'snprintf(value,value_size,"%u,%u,%u",r,g,b);' in jeedom
assert 'const String setpointText = (rawValue & 1)' in beta_ui_impl
assert 'if(half&1)snprintf(thermostat_payload' in full
assert 'else snprintf(thermostat_payload' in full
assert 'payload=thermostat_payload;' in full
assert '_nightStart = "23:00";' in beta_ui_header
assert '_nightEnd = "07:00";' in beta_ui_header
assert 'night_start_minute=23*60,night_end_minute=7*60;' in full
assert "value[4]=='a'" in full and "value[2]=='%'" in full
assert 'const int parsed=parse_beta_clock(trim);' in full
assert 'clock[0]=(char)(\'0\'+hh/10);' in full
assert 'static void clean_screen_name(' in mqtt
assert "if(*p=='#'||*p=='+')*p='_';" in mqtt
assert 'clean_screen_name(screen_name,sizeof(screen_name));' in mqtt
print("PASS beta color HEX, thermostat half-degree, 23:00/07:00 and MQTT screen-name formats")


legacy_mqtt_h=(root/"firmware/source/consolidated/MIRA_PANEL/MiraMqtt.h").read_text(encoding="utf-8")
assert 'KEEPALIVE_S = 20;' in legacy_mqtt_h
assert '.session.keepalive=20,' in mqtt
print("PASS beta MQTT 20-second broker keepalive")


# The beta settings page accepts the historical MQTT_Nom text and the
# MQTT layer cleans it; do not reject beta-compatible nested topic names
# at HTML submission while accepting them through the JSON API.
assert 'static void clean_screen_name(char *name,size_t cap)' in mqtt
assert 'clean_screen_name(screen_name,sizeof(screen_name));' in mqtt
assert 'Nom MQTT invalide' not in handlers
assert 'if(!strcmp(host,"null"))host[0]=0;' in mqtt
assert 'if(!strcmp(username,"null"))username[0]=0;' in mqtt
assert 'if(!strcmp(secret,"null"))secret[0]=0;' in mqtt
print("PASS Arduino MQTT screen name via web form and literal null config")


# Arduino cleanScreenName(blank) returns "panel", not a config error.
assert 'if(!screen||strlen(screen)>=48||' in handlers
assert 'if(!strcmp(name,"null"))' not in mqtt  # handled by clean_screen_name
assert 'if(!name[0]||!strcmp(name,"null"))snprintf(name,cap,"panel");' in mqtt
print("PASS empty beta MQTT screen name defaults to panel")


# Beta roundrange (not just thermostat) uses half-unit LVGL arcs:
# physical range is min*2 .. max*2; outgoing 20.5 becomes "20.5"
# and a passive 20.5 must display without triggering an action.
assert 'lv_arc_set_range(b.control, b.minValue * 2, b.maxValue * 2);' in beta_ui_impl
assert 'const float setpoint = rawValue * 0.5f;' in beta_ui_impl
assert 'lv_arc_set_range(w->control,w->minv*2,w->maxv*2);' in full
assert 'if(!strcmp(b->type,"thermostat")||!strcmp(b->type,"roundrange"))' in full
assert 'if(w->control && !strcmp(w->type,"roundrange")){' in full
assert 'lv_arc_set_value(w->control,half);' in full
assert 'if((!strcmp(b->type,"thermostat")||!strcmp(b->type,"roundrange"))&&b->control)' in full
print("PASS beta roundrange half-unit arc, passive state and action value")


# Drag behavior must follow beta implementation, not a guessed common
# throttle. Range and roundrange send each changed value and ALWAYS send
# on release; thermostat throttles only intermediate changes at 130 ms.
assert 'const bool liveSend = code == LV_EVENT_VALUE_CHANGED && value != b.lastLiveValue;' in beta_ui_impl
assert 'const bool liveSend = code == LV_EVENT_VALUE_CHANGED && rawValue != b.lastLiveValue;' in beta_ui_impl
assert '(uint32_t)(now - b.lastLiveSendMs) >= 130U' in beta_ui_impl
assert 'if (liveSend || finalSend)' in beta_ui_impl
assert 'static void event_live_range(lv_event_t *event)' in full
assert 'w->live_last_value==value)return;' in full
assert '(uint32_t)(now-w->live_last_tick)<130u' in full
assert 'else if(code!=LV_EVENT_RELEASED)return;' in full
assert 'lv_obj_add_event_cb(w->control,event_live_range,LV_EVENT_VALUE_CHANGED,w);' in full
assert 'lv_obj_add_event_cb(w->control,event_live_range,LV_EVENT_RELEASED,w);' in full
assert '<180u' not in full
print("PASS source-derived drag events: immediate range/roundrange, 130-ms thermostat and final release")

# Arduino LVGL8 uses the 150-pixel HUE colorwheel, with fixed mode:
# saturation and value are preserved when a remote RGB state arrives,
# but there are no extra HSV sliders. Release ALWAYS sends the final RGB.
assert 'lv_colorwheel_set_mode(b.control, LV_COLORWHEEL_MODE_HUE);' in beta_ui_impl
assert 'lv_colorwheel_set_mode_fixed(b.control, true);' in beta_ui_impl
assert 'const bool liveSend = (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_PRESSING)' in beta_ui_impl
assert 'event_local(event);' in full[full.index('static void wheel_touch'):full.index('/* Legacy beta GenericCmd')]
assert 'lv_obj_set_size(w->control,MIRA_WHEEL_SIZE,MIRA_WHEEL_SIZE);' in full
assert 'lv_obj_add_event_cb(w->control,event_local,LV_EVENT_RELEASED,w);' in full
assert 'static void wheel_slider_touch(' not in full
assert 'lv_slider_create(w->card)' not in full[full.index('if(!strcmp(type,"color"))'):full.index('if(!strcmp(type,"energy"))')]
assert 'w->color_sat=hsv.s;' in full
assert 'w->color_val=hsv.v;' in full
print("PASS fixed 150px Hue wheel, live/final RGB, passive saturation and brightness")

# Arduino multi-action gesture semantics: 900-ms held press, then
# 370-ms post-release aggregation for single/double/triple.
assert '(uint32_t)(now-b.multiTouchPressAt)>=900' in beta_ui_impl
assert '(uint32_t)(now-b.multiTouchReleasedAt)>=370' in beta_ui_impl
assert 'LV_EVENT_RELEASED' in beta_ui_impl[beta_ui_impl.index('void MiraPanelUI::onMultiTouch'):beta_ui_impl.index('void MiraPanelUI::onToggle')]
assert '(uint32_t)(gesture_now-w->multi_pressed_at)>=900u' in full
assert 'w->multi_deadline=now+370u;' in full
assert 'lv_obj_add_event_cb(bt,multiaction_event,LV_EVENT_PRESS_LOST,w);' in full
assert 'lv_obj_add_event_cb(bt,multiaction_event,LV_EVENT_LONG_PRESSED,w);' not in full
print("PASS Arduino multi-action 900-ms hold / 370-ms release grouping")

# Arduino thermostat MQTT is a composite topic, unlike the Jeedom range
# index. Never publish the thermostat command at the bare widget name.
assert 'publishAction(b.name + "__consigne", String(value, 1));' in beta_ui_impl
assert 'snprintf(mqtt_topic,sizeof(mqtt_topic),"%.95s__consigne",b->name);' in full
assert 'mqtt_name=mqtt_topic;' in full
assert 'mqtt_payload=b->value;' in full
assert 'mira_beta_user_action(b->name,b->type,payload,' in full
assert 'mira_generic_queue_local_action(name,type,value)' in full
print("PASS thermostat MQTT uses composite __consigne topic and decimal payload")

# Beta preserves local gesture for 250 ms, and for 350 ms after release
# before accepting an older Jeedom echo that could snap a control backwards.
assert 'b.localInputUntilMs = now + 250;' in beta_ui_impl
assert 'if (finalSend) b.localInputUntilMs = now + 350;' in beta_ui_impl
assert 'localBinding.localInputUntilMs != 0' in beta_ui_impl
assert 'uint32_t local_input_until_ms;' in full
assert 'lv_event_get_code(e)==LV_EVENT_RELEASED?350u:250u' in full
assert '(int32_t)(lv_tick_get()-w->local_input_until_ms)<0' in full
assert 'w->local_input_until_ms=lv_tick_get()+350u;' in full
print("PASS beta 250/350-ms local touch precedence for ranged/HSV/media controls")

# Beta's backlight slider really changes hardware as the finger moves;
# the ESP-IDF callback must not wait until release, while avoiding an
# expensive NVS commit on every intermediate event.
assert 'lv_obj_add_event_cb(_brightnessSlider, onBrightness, LV_EVENT_VALUE_CHANGED, this);' in beta_ui_impl
assert 'static void brightness_live(lv_event_t *event)' in full
assert 'lv_obj_add_event_cb(sl,brightness_live,LV_EVENT_VALUE_CHANGED,NULL);' in full
assert 'lv_obj_add_event_cb(sl,brightness_release,LV_EVENT_RELEASED,NULL);' in full
assert 'if(persisted_brightness_raw==(uint8_t)value)return;' in full
print("PASS live beta brightness feedback with deferred NVS persistence")

# On Waveshare 4.3C the beta WT32MultiVisible() is false: native DI
# extension may display the tile ONLY if its own configuration opts in.
assert 'MIRA_TARGET_WAVESHARE_43C' in beta_server_impl
assert 'if(!strcmp(type,"multiaction")&&!mira_gpio43_multi_visible(w->name))' in full
assert 'bool mira_gpio43_multi_visible(const char *name)' in (root/"firmware/experiments/esp-idf/mira-idf/main/mira_gpio43.c").read_text(encoding="utf-8")
print("PASS Waveshare multi-action card hidden by default; EXIO visibility is opt-in")

# Beta sleep settings have an explicit On/Off switch, independently of
# display delay. Zero timeout remains the off value for web DISPLAY_Delai.
assert 'ui->_sleepEnabled = lv_obj_has_state' in beta_ui_impl
assert 'static bool sleep_enabled=true;' in full
assert 'static void sleep_mode_changed(lv_event_t *event)' in full
assert 'lv_obj_add_event_cb(sleep_switch,sleep_mode_changed,LV_EVENT_VALUE_CHANGED,NULL);' in full
assert 'if(!display_sleeping && sleep_enabled && sleep_timeout_seconds>0 &&' in full
assert 'nvs_get_u8(ui_nvs,"sleep_enabled",&enabled)' in full
print("PASS beta explicit sleep enable switch and wake/sleep gate")

# Firmware-beta's Preferences namespace "miraui" is reused only as a
# READ-ONLY fallback when corresponding IDF keys do not exist. This
# includes the user's actual screen brightness and sleep/clock toggles.
assert '_prefs.begin("miraui", false);' in beta_ui_impl
assert '_brightness = _prefs.getUChar("bright", 220);' in beta_ui_impl
assert '_sleepEnabled = _prefs.getBool("sleep", true);' in beta_ui_impl
assert '_clockInSleep = _prefs.getBool("clock", true);' in beta_ui_impl
for key in ("bright","sleep","clock","theme","home"):
    assert f'nvs_get_u8(legacy_nvs,"{key}"' in full
print("PASS legacy miraui preferences for brightness, theme, sleep, clock and navigation")

assert '_sleepEnabled = _sleepTimeoutSec > 0;' in beta_ui_impl
assert 'err=nvs_set_u8(nv,"sleep_enabled",number>0?1:0);' in jeedom
print('PASS remote sleep delay updates activation')

# Source-derived differences fixed: beta HTTP SSE keeps EventSource open
# (unlike the early native reconnecting snapshot); the native handler must
# hand off an async HTTP request and stream changed cached states.
assert 'AsyncEventSource events("/events");' in beta_server_impl
assert 'server.addHandler(&events);' in beta_server_impl
assert 'httpd_req_async_handler_begin(req,&client->req)' in jeedom
assert 'httpd_req_async_handler_complete(req)' in jeedom
assert 'mira_sse_changed(client)' in jeedom
assert 'xEventGroupWaitBits(mira_sse_signal,MIRA_SSE_CHANGED,pdTRUE,pdFALSE,' in jeedom
assert 'mira_jeedom_sse_notify();' in full
assert 'if(sse_payload)mira_sse_record_direct((int)id,sse_payload);' in jeedom
assert 'if(!strcmp(kind,"send_range"))sse_payload=payload;' in jeedom
assert 'else if(!strcmp(kind,"send_list"))sse_payload=action_name;' in jeedom
assert 'else if(!strcmp(kind,"send_textbox"))sse_payload=payload;' in jeedom
assert 'snprintf(sse_data,sizeof(sse_data),"#%02x%02x%02x",' in jeedom
assert 'events.send(CValue, CNom, millis());' in beta_server_impl
assert 'events.send(CNom, CValue, millis());' in beta_server_impl
assert 'httpd_resp_send_chunk(client->req,event,(size_t)size)' in jeedom
assert "retry: 3000\\n\\n" in jeedom
assert 'HTTPD_503_SERVICE_UNAVAILABLE' not in jeedom
assert ': Mira keepalive' in jeedom
assert 'return httpd_resp_send_chunk(req,NULL,0);' not in jeedom[jeedom.index('static esp_err_t beta_events_get('):jeedom.index('// Browser home commands on POST')]
print("PASS persistent authenticated and bounded SSE transport")

# The beta sends Jeedom actions AND MQTT to independent destinations when both
# are configured; receiving a passive state must not dispatch either.
assert 'ui._server.IndexSetEvent(b.id);' in beta_ui_impl
assert 'ui.publishMqttAction(b, String(value));' in beta_ui_impl
assert 'const bool queued=enabled&&mira_jeedom_queue_local_action(name,type,value);' in full
assert 'const bool published=publish_mqtt&&' in full
assert 'const bool generic=!enabled&&!published&&' in full
assert 'mqtt_payload=b->value;' in full
assert 'action!=4' in full # Beta device selection is Jeedom-only.
print("PASS independent Jeedom and MQTT callbacks, no action from passive states")

# Jeedom slider commands cannot be allowed to evict a final release from
# the same 12-entry FIFO used by discrete buttons. A keyed, PSRAM-backed
# latest-value store is drained independently with round-robin fairness.
assert 'action_queue=xQueueCreate(12,sizeof(mira_action_msg_t));' in jeedom
for contract in (
    '#define MIRA_LAST_VALUE_SLOTS 96u',
    'static bool enqueue_latest(const mira_action_msg_t *msg)',
    'static bool dequeue_latest(mira_action_msg_t *result)',
    'if(action_is_continuous(type))return enqueue_latest(&msg);',
    'next_latest_slot=(i+1u)%MIRA_LAST_VALUE_SLOTS;',
    'ulTaskNotifyTake(pdTRUE,portMAX_DELAY)',
    'heap_caps_calloc(MIRA_LAST_VALUE_SLOTS,',
):
    assert contract in jeedom, f"Jeedom latest-value contract missing: {contract}"
assert 'xQueueReceive(action_queue,&action,portMAX_DELAY)' not in jeedom
print('PASS Jeedom last-value slots protect final slider/thermostat/color commands')

# Browser legacy input boxes and HTTP action IDs must resolve ONLY when
# a matching historical entry actually exists; an unknown ID fails closed.
assert 'strcmp(type,"inputbox")&&strcmp(type,"httpaction"))return false;' in jeedom
assert 'strcmp(msg->type,"inputbox")&&!strcmp(kind,"inputbox")' in jeedom
assert 'strcmp(msg->type,"httpaction")&&!strcmp(kind,"httpaction")' in jeedom
assert 'snprintf(value,value_size,"%s",msg->value);' in jeedom
assert 'else if(!strcmp(kind,"httpaction"))value[0]=0;' in jeedom
assert 'if(local_id<1||local_id>4096)return;' in jeedom
print('PASS legacy Jeedom inputbox/httpaction exact-ID dispatch and fail-closed unmapped IDs')

# Web editor and Jeedom /programui may not disagree about MAX_BINDINGS=40.
assert 'if(count>40)valid=false;' in jeedom
assert 'if(cJSON_GetArraySize(array)>=40)' in jeedom
assert 'cJSON_GetArraySize(pending)>40' in jeedom
assert 'Maximum 64 widgets LVGL' not in jeedom
print('PASS Web LVGL editor and programming agree on 40 widgets')

# Saving DISPLAY_Delai in the beta HTML must also set sleep_enabled;
# the on-screen sleep toggle can still override locally until that save.
beta_web=(idf/"mira_beta_web.c").read_text(encoding="utf-8")
assert 'nvs_set_u8(nv,"sleep_enabled",values[2]>0?1:0)' in beta_web
print('PASS Web display settings synchronize sleep enable')

# The original HTTP APIs do not enable Jeedom when an NVS flag is absent.
# /getallparams (synthesized JSON), /getparam and the action worker must agree.
assert 'strcpy(value,"1"); // Original Mira IDF default transport: HTTP' not in jeedom
assert 'if(!have_nvs||nvs_get_str(nv,names[i],value,&cap)!=ESP_OK){' in jeedom
assert 'continue;' in jeedom[jeedom.index('if(!have_nvs||nvs_get_str(nv,names[i],value,&cap)!=ESP_OK){'):][:380]
print('PASS /getallparams no longer forges Jeedom=1 on an unconfigured panel')

# HTTP binding endpoints reject IDs with trailing text and malformed target
# values instead of accepting partial strtol() conversions with 200 OK.
assert 'if(id<1||id>4096||!value||!decimal_id(value)||' in jeedom
assert '!decimal_id(id)||!decimal_id(value)||' in jeedom
assert 'if(!decimal_id(id)||!decimal_id(off)||!decimal_id(on)||' in jeedom
assert 'if(!query_param(req,"value",id,sizeof(id))||!decimal_id(id))' in jeedom
print('PASS Web/Jeedom mapping endpoints validate complete decimal IDs')

# GET /get?infofa= must not return a different type's cached value.
assert '(!strcmp(kind,"infofa")&&!strcmp(type,"infofa"))' in jeedom
print('PASS exact Info vs InfoFA selector semantics on legacy /get')

# The untouched original beta HTML submits all five /update Web forms
# as POST URL query parameters. The native HTTP dispatcher must interpret
# those selectors before the binary firmware-upload path and require auth.
for selector in ("send=","send_range=","send_list=","send_color=","send_textbox="):
    assert selector in recovery, selector
for key in ('"send"','"send_range"','"send_list"','"send_color"','"send_textbox"'):
    assert key in jeedom[jeedom.index('esp_err_t mira_jeedom_beta_web_action('):
                          jeedom.index('esp_err_t mira_jeedom_beta_legacy_get_update(')]
assert 'if(!mira_beta_web_authorized(req))return ESP_FAIL;' in recovery
assert 'if(!mira_beta_web_authorized(req))return ESP_FAIL;' in jeedom
print('PASS beta Web POST action dispatch, auth gate and selector names')

# Multi-client SSE must share one initialized event group. Creating it in
# concurrent /events handlers could strand a browser on a stale handle.
assert 'esp_err_t mira_jeedom_action_init(void) {' in jeedom
assert 'if(!mira_sse_signal)mira_sse_signal=xEventGroupCreate();' in jeedom[
    jeedom.index('esp_err_t mira_jeedom_action_init(void) {'):]
assert 'if(!mira_sse_signal)mira_sse_signal=xEventGroupCreate();' not in jeedom[
    jeedom.index('static esp_err_t beta_events_get('):
    jeedom.index('// Browser home commands on POST')]
print('PASS SSE event group initialized before concurrent /events requests')

# A complete retained MQTT message can be split across several ESP-MQTT
# DATA callbacks. Beta's processIncoming reads the entire broker PUBLISH;
# an IDF first-fragment-only handler silently loses larger state payloads.
assert 'void MiraMqtt::processIncoming()' in legacy_mqtt
assert 'processPublish(header, packet, len);' in legacy_mqtt
for required in (
    'static void clear_fragment(void)',
    'e->current_data_offset==0',
    'e->total_data_len>1024',
    'incoming_fragment.expected!=e->total_data_len',
    'incoming_fragment.received!=e->current_data_offset',
    'memcpy(incoming_fragment.payload+incoming_fragment.received,',
    'if(incoming_fragment.received==incoming_fragment.expected)',
    'incoming_state(incoming_fragment.topic,incoming_fragment.topic_len,',
    'incoming_display(incoming_fragment.topic,incoming_fragment.topic_len,',
):
    assert required in mqtt, f'MQTT fragment-reassembly missing: {required}'
assert 'e->data_len==e->total_data_len' not in mqtt
print('PASS MQTT fragment reassembly and bounded passive-state dispatch')

# Reconnection publishes all retained composite info states at once.
# A 24-slot FIFO can discard the majority of a 40-widget media panel;
# last-value PSRAM slots retain 40*9 plus weather, never trigger actions.
for required in (
    '#define MIRA_PENDING_STATE_SLOTS 384',
    'static bool put_pending_state(const mira_state_update_t *msg)',
    'static bool pop_pending_state(mira_state_update_t *out)',
    'bool accepted=put_pending_state(&msg);',
    'if(!have)have=pop_pending_state(&update);',
    '#define MIRA_STATE_CACHE_LIMIT 384',
    '#define MIRA_COVER_SLOTS 40',
    'bool accepted=put_pending_cover(&cover);',
    'if(!have)have=pop_pending_cover(&cover);',
):
    assert required in full, f'MQTT reconnect saturation protection missing: {required}'
assert 'remember_passive_state(&msg);' in full
assert 'mira_jeedom_sse_notify();' in full
print('PASS bounded PSRAM latest states/covers, no passive action echo')

# Legacy AES migration rejects capacities >256 (its ciphertext buffer is
# 256 bytes). A 769-byte MQTT plaintext buffer therefore rejected EVERY
# legacy crypted-text broker password, irrespective of its actual length.
assert 'capacity>256' in (idf/'mira_legacy_cipher.c').read_text(encoding='utf-8')
assert 'char plaintext[256]={0};' in mqtt
assert 'mira_legacy_decrypt(stored,plaintext,sizeof(plaintext))' in mqtt
assert 'char plaintext[769]={0};' not in mqtt
print('PASS encrypted Arduino MQTT credentials migrate with correct AES buffer')

# Configuration reapply clears credentials/flags from the old live broker
# and keeps the old password if a JSON setup omits or blanks that field.
assert 'host[0]=0;username[0]=0;secret[0]=0;' in mqtt
assert 'enabled=false;port=1883;' in mqtt
assert 'if(err==ESP_OK&&s&&s->valuestring[0])' in mqtt
assert 'if(!mira_setup_require_ap(r))return ESP_FAIL;' in mqtt[
    mqtt.index('static esp_err_t mqtt_get('):mqtt.index('static esp_err_t mqtt_post(')]
assert 'cJSON_AddStringToObject(result,"screen",screen_name);' in mqtt
print('PASS MQTT reprovisioning, password retention, authenticated JSON status')

# Editing an existing Wi-Fi profile first snapshots the previous native
# secret, then rolls it back if saving the index/overlay fails. A failed
# Wi-Fi profile write must not return a successful Web redirect.
native_networks=(idf/'mira_beta_networks.c').read_text(encoding='utf-8')
for required in (
    'char previous_password[96]={0};',
    'bool had_previous_password=false;',
    'if(err==ESP_OK)err=save(root);',
    'if(err!=ESP_OK&&*password){',
    'had_previous_password?',
    'nvs_erase_key(nv,key)',
    'memset(previous_password,0,sizeof(previous_password));',
):
    assert required in native_networks, f'Wi-Fi rollback missing: {required}'
assert 'Wi-Fi applique mais profil non enregistre; verifier NVS' in handlers
print('PASS Wi-Fi native overlay failure surfaces error and restores credential')

# The Display/<key>/set channel can restore eleven retained settings plus
# sleep/wake. Do not drop the final brightness or sleep command if the old
# 12-element GUI queue is already full. Pending values are keyed by setting.
for key in (
    '#define MIRA_DISPLAY_COMMAND_SLOTS 16',
    'static bool put_pending_display(const mira_display_command_t *msg)',
    'static bool pop_pending_display(mira_display_command_t *msg)',
    'if(put_pending_display(&msg))return true;',
    'if(!have)have=pop_pending_display(&cmd);',
):
    assert key in full, f'MQTT Display latest-wins contract missing: {key}'
assert 'if((!cover_update_queue&&!pending_covers)||size>768)return false;' in full
print('PASS retained Display settings coalesced and media cover fallback preserved')
