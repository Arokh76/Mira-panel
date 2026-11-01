#include "mira_full.h"
#include "mira_mqtt.h"
#include "mira_jeedom.h"
#include "mira_setup_access.h"
// Isolated IDF test recovery AP. Uses raw .bin POST, not multipart/form-data.
// Partition layout must match Mira's current Waveshare OTA partition layout.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mira_recovery.h"
#include "mira_legacy_cipher.h"
#include "mira_display.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "esp_netif_sntp.h"
#include <time.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mira-idf-ota";
static volatile int pending_shadow_request = -1;
static bool clock_sntp_started;
static volatile int pending_scroll_request = -1;
static volatile int pending_auto_request = -1;
static volatile int pending_bench_request = -1;
static volatile int pending_cache_request = -1;
static volatile uint32_t current_cache_state, current_cache_count;
int mira_recovery_take_cache_request(void) {
    int request = pending_cache_request;
    pending_cache_request = -1;
    return request;
}
void mira_recovery_set_cache_state(int state, int cards) {
    current_cache_state = (uint32_t)state;
    current_cache_count = (uint32_t)cards;
}
static volatile uint32_t current_bench_static = 0;
int mira_recovery_take_bench_request(void) {
    int request = pending_bench_request;
    pending_bench_request = -1;
    return request;
}
void mira_recovery_set_bench_state(int enabled) {
    current_bench_static = enabled ? 1 : 0;
}
static volatile uint32_t current_auto_scroll = 1;
int mira_recovery_take_auto_request(void) {
    int request = pending_auto_request;
    pending_auto_request = -1;
    return request;
}
void mira_recovery_set_auto_state(int enabled) {
    current_auto_scroll = enabled ? 1 : 0;
}
static volatile uint32_t current_scroll_mode, current_scroll_switches, current_scroll_rebuild_us;
int mira_recovery_take_scroll_request(void) {
    int request = pending_scroll_request;
    pending_scroll_request = -1;
    return request;
}
void mira_recovery_set_scroll_state(uint32_t mode, uint32_t switches, uint32_t rebuild_us) {
    current_scroll_mode = mode;
    current_scroll_switches = switches;
    current_scroll_rebuild_us = rebuild_us;
}
static volatile uint32_t current_shadow_mode = 1;
void mira_recovery_set_shadow_mode(int mode) { current_shadow_mode = (uint32_t)mode; }
int mira_recovery_take_shadow_request(void) {
    int request = pending_shadow_request;
    pending_shadow_request = -1;
    return request;
}
static volatile uint32_t current_fps_tenths = 0;
static volatile uint32_t current_rgb_fps_tenths = 0;
void mira_recovery_set_fps(uint32_t lvgl_fps, uint32_t rgb_fps) {
    current_fps_tenths = lvgl_fps;
    current_rgb_fps_tenths = rgb_fps;
}
static volatile uint32_t current_draw_avg_us, current_wait_avg_us, current_handler_max_us;
void mira_recovery_set_profile(uint32_t draw_us, uint32_t wait_us, uint32_t handler_us) {
    current_draw_avg_us = draw_us;
    current_wait_avg_us = wait_us;
    current_handler_max_us = handler_us;
}

static const char recovery_page[] =
"<!doctype html><html lang='fr'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Mira IDF - Retour</title><h2>Mira ESP-IDF - Banc d'essai LVGL 9.6</h2>"
"<p>Cette application n'est pas le Mira Jeedom complet.</p>"
"<p>Pour revenir a Mira 0.7.37, selectionner son fichier <b>OTA .bin</b> (pas le factory.bin).</p>"
"<input type='file' id='fw' accept='.bin'><button onclick='flash()'>Reflasher</button>"
"<pre id='msg'>Pret</pre><p><a href='/perf'>FPS et statut JSON</a></p>"
"<p><b>Defilement automatique :</b> "
"<a href='/?auto=off'>AUTO OFF (STOP)</a> | "
"<a href='/?auto=on'>AUTO ON (REPRENDRE)</a></p>"
"<p><b>Mesure sans defilement :</b> "
"<a href='/?bench=static'>BENCH STATIC (RAFRAICHIR TOUT)</a> | "
"<a href='/?bench=off'>BENCH OFF (ARRET COMPLET)</a></p>"
"<p><b>Cache bitmap des cartes (TEST):</b> "
"<a href='/?cache=on'>CACHE ON</a> | <a href='/?cache=off'>CACHE OFF</a></p>"
"<p>cache_state: 0=normal, 1=creation (deplacement suspendu), 2=actif. "
"En cas de RAM insuffisante, retour automatique au mode normal.</p>"
"<p>/perf : FPS LVGL/RGB + cache_state / cache_cards.</p>"
"<p><b>Defilement - au demarrage seul le mode CLASSIQUE est initialise :</b> "
"<a href='/scroll/classic'>35 CARTES</a> | "
"<a href='/scroll/virtual'>8 CARTES (TEST)</a></p>"
"<p>Comparer les FPS a defilement egal : "
"<a href='/shadow/off'>SANS OMBRE</a> | "
"<a href='/shadow/on'>OMBRE 4PX</a> | "
"<a href='/shadow/light'>OMBRE 2PX</a> | "
"<a href='/shadow/relief'>RELIEF SANS FLOU</a></p>"
"<script>"
"async function flash(){let f=document.getElementById('fw').files[0];"
"if(!f){alert('Choisir un .bin OTA');return;}"
"document.getElementById('msg').innerText='Envoi en cours...';"
"try{let r=await fetch('/update',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:f});"
"document.getElementById('msg').innerText=await r.text();}"
"catch(e){document.getElementById('msg').innerText=String(e);}}"
"</script></html>";

static esp_err_t index_get(httpd_req_t *req) {
    // Reuse the already-registered root URI: NO new HTTP handler slots.
    char query[48], requested[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "auto", requested, sizeof(requested)) == ESP_OK) {
            if (strcmp(requested, "off") == 0) {
                pending_bench_request = -1;
                pending_auto_request = 0;
            } else if (strcmp(requested, "on") == 0) {
                pending_bench_request = -1;
                pending_auto_request = 1;
            }
        } else if (httpd_query_key_value(query, "bench", requested, sizeof(requested)) == ESP_OK) {
            if (strcmp(requested, "static") == 0) {
                pending_auto_request = -1;
                pending_bench_request = 1;
            } else if (strcmp(requested, "off") == 0) {
                pending_auto_request = -1;
                pending_bench_request = 0;
            }
        } else if (httpd_query_key_value(query, "cache", requested, sizeof(requested)) == ESP_OK) {
            if (strcmp(requested, "on") == 0) pending_cache_request = 1;
            else if (strcmp(requested, "off") == 0) pending_cache_request = 0;
        }
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, recovery_page, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t shadow_on_get(httpd_req_t *req) {
    pending_shadow_request = 1;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, "<h2>OMBRES ON</h2><p>Commande envoyee au moteur LVGL.</p><a href='/'>Retour</a>");
}
static esp_err_t shadow_off_get(httpd_req_t *req) {
    pending_shadow_request = 0;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, "<h2>OMBRES OFF</h2><p>Commande envoyee au moteur LVGL.</p><a href='/'>Retour</a>");
}
static esp_err_t shadow_light_get(httpd_req_t *req) {
    pending_shadow_request = 2;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, "<h2>OMBRE 2PX</h2><p>Commande envoyee a LVGL.</p><a href='/'>Retour</a>");
}
static esp_err_t shadow_relief_get(httpd_req_t *req) {
    pending_shadow_request = 3;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, "<h2>RELIEF SANS FLOU</h2><p>Commande envoyee a LVGL.</p><a href='/'>Retour</a>");
}
static esp_err_t scroll_classic_get(httpd_req_t *req) {
    pending_scroll_request = 0;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, "<h2>MODE CLASSIQUE 35 CARTES</h2><a href='/'>Retour</a>");
}
static esp_err_t scroll_virtual_get(httpd_req_t *req) {
    pending_scroll_request = 1;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, "<h2>MODE VIRTUEL 8 CARTES - EXPERIMENTAL</h2>"
                                "<p>En cas de reboot le mode classique est restaure.</p><a href='/'>Retour</a>");
}
static esp_err_t perf_get(httpd_req_t *req) {
    mira_display_stats_t s;
    mira_display_stats(&s);
    char text[1280];
    snprintf(text, sizeof(text),
      "{\"fps_tenths\":%" PRIu32 ",\"rgb_fps_tenths\":%" PRIu32 ",\"frames\":%" PRIu32
      ",\"vsync_missed\":%" PRIu32 ",\"vsync_wait_us\":%" PRIu32
      ",\"uptime_ms\":%" PRIu32 ",\"vsync_events\":%" PRIu32
      ",\"frame_complete_events\":%" PRIu32
      ",\"draw_avg_us\":%" PRIu32 ",\"wait_avg_us\":%" PRIu32
      ",\"handler_max_us\":%" PRIu32
      ",\"touch_ready\":%" PRIu32 ",\"touch_addr\":%" PRIu32
      ",\"touch_presses\":%" PRIu32 ",\"touch_reads\":%" PRIu32
      ",\"touch_errors\":%" PRIu32 ",\"touch_last_x\":%" PRIu32
      ",\"touch_last_y\":%" PRIu32 ",\"shadow_mode\":%" PRIu32
      ",\"touch_last_status\":%" PRIu32 ",\"touch_irq_level\":%" PRIu32
      ",\"touch_ready_events\":%" PRIu32 ",\"touch_invalid_coords\":%" PRIu32
      ",\"touch_raw_x\":%" PRIu32 ",\"touch_raw_y\":%" PRIu32
      ",\"touch_config_x\":%" PRIu32 ",\"touch_config_y\":%" PRIu32
      ",\"auto_scroll\":%" PRIu32 ",\"bench_static\":%" PRIu32 ",\"scroll_mode\":%" PRIu32 ",\"scroll_switches\":%" PRIu32
      ",\"scroll_rebuild_us\":%" PRIu32 ",\"cache_state\":%" PRIu32
      ",\"cache_cards\":%" PRIu32 "}",
      current_fps_tenths, current_rgb_fps_tenths, s.frames, s.vsync_missed, s.last_refresh_us,
      (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
      s.vsync_events, s.frame_complete_events,
      current_draw_avg_us, current_wait_avg_us, current_handler_max_us,
      (uint32_t)(s.touch_ready ? 1 : 0), (uint32_t)s.touch_addr,
      s.touch_presses, s.touch_reads, s.touch_errors,
      s.touch_last_x, s.touch_last_y, current_shadow_mode,
      (uint32_t)s.touch_last_status, (uint32_t)s.touch_irq_level,
      s.touch_ready_events, s.touch_invalid_coords,
      s.touch_raw_x, s.touch_raw_y, s.touch_config_x, s.touch_config_y,
      current_auto_scroll, current_bench_static, current_scroll_mode, current_scroll_switches,
      current_scroll_rebuild_us, current_cache_state, current_cache_count);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, text);
}
static esp_err_t update_post(httpd_req_t *req) {
    if(!mira_setup_require_ap(req))return ESP_FAIL;
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target || req->content_len <= 0 || req->content_len > target->size) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid OTA size or partition");
    }
    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(target, OTA_SIZE_UNKNOWN, &ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
    }
    uint8_t buf[2048];
    int remaining = req->content_len;
    while (remaining > 0) {
        int count = httpd_req_recv(req, (char *)buf, remaining > (int)sizeof(buf) ? sizeof(buf) : remaining);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (count <= 0) { err = ESP_FAIL; break; }
        err = esp_ota_write(ota, buf, count);
        if (err != ESP_OK) break;
        remaining -= count;
    }
    if (err == ESP_OK && remaining == 0) err = esp_ota_end(ota);
    else esp_ota_abort(ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA write/end failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA failed - current firmware unchanged");
    }
    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA boot selection failed");
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Firmware OTA installe. Redemarrage...");
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

// Optional UI routes must NEVER abort Mira if an HTTP registration fails.
static void register_optional_route(httpd_handle_t server, const httpd_uri_t *route) {
    esp_err_t err = httpd_register_uri_handler(server, route);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Optional HTTP route %s unavailable: %s",
                 route->uri, esp_err_to_name(err));
    }
}

static volatile bool station_configured;
static esp_netif_t *sta_netif;
// Mira beta modes: auto, manual (full static IP), fixed_auto (DHCP options
// followed by a requested static IP once its subnet has been verified).
static enum { PROFILE_DHCP, PROFILE_MANUAL, PROFILE_FIXED_AUTO } legacy_ip_mode;
static esp_netif_ip_info_t legacy_manual_ip;
static esp_netif_dns_info_t legacy_manual_dns[2];
static esp_ip4_addr_t legacy_requested_ip;
static bool legacy_fixed_auto_attempted;

static bool legacy_parse_ipv4(const char *s,esp_ip4_addr_t *target){
    ip4_addr_t parsed={0};
    if(!s||!*s||!ip4addr_aton(s,&parsed))return false;
    target->addr=parsed.addr;
    return parsed.addr!=0 && parsed.addr!=0xffffffffu;
}
static bool legacy_mask_valid(esp_ip4_addr_t mask){
    uint32_t m=ntohl(mask.addr);
    uint32_t inverted=~m;
    return m!=0 && (inverted&(inverted+1u))==0;
}
static bool legacy_same_subnet(esp_ip4_addr_t a,esp_ip4_addr_t b,esp_ip4_addr_t mask){
    return (a.addr&mask.addr)==(b.addr&mask.addr);
}
static void legacy_apply_manual_ip(void){
    if(legacy_ip_mode!=PROFILE_MANUAL||!sta_netif)return;
    esp_err_t err=esp_netif_dhcpc_stop(sta_netif);
    if(err!=ESP_OK && err!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED){
        ESP_LOGW(TAG,"Legacy manual IP: unable to stop DHCP: %s",esp_err_to_name(err));
        return;
    }
    err=esp_netif_set_ip_info(sta_netif,&legacy_manual_ip);
    if(err==ESP_OK){
        (void)esp_netif_set_dns_info(sta_netif,ESP_NETIF_DNS_MAIN,&legacy_manual_dns[0]);
        if(legacy_manual_dns[1].ip.u_addr.ip4.addr)
            (void)esp_netif_set_dns_info(sta_netif,ESP_NETIF_DNS_BACKUP,&legacy_manual_dns[1]);
        ESP_LOGI(TAG,"Mira beta manual IP settings applied");
    }else{
        ESP_LOGW(TAG,"Invalid static IP setup, falling back to DHCP: %s",esp_err_to_name(err));
        (void)esp_netif_dhcpc_start(sta_netif);
    }
}
static void legacy_finish_fixed_auto(void){
    if(legacy_ip_mode!=PROFILE_FIXED_AUTO||legacy_fixed_auto_attempted||!sta_netif)return;
    legacy_fixed_auto_attempted=true;
    esp_netif_ip_info_t current={0};
    if(esp_netif_get_ip_info(sta_netif,&current)!=ESP_OK||
       !legacy_mask_valid(current.netmask)||
       !legacy_same_subnet(legacy_requested_ip,current.gw,current.netmask)||
       !legacy_same_subnet(legacy_requested_ip,current.ip,current.netmask)||
       legacy_requested_ip.addr==current.gw.addr){
        ESP_LOGW(TAG,"Mira beta fixed_auto IP outside DHCP network; keeping DHCP");
        return;
    }
    if(legacy_requested_ip.addr==current.ip.addr)return;
    esp_err_t err=esp_netif_dhcpc_stop(sta_netif);
    if(err==ESP_OK||err==ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED){
        current.ip=legacy_requested_ip;
        err=esp_netif_set_ip_info(sta_netif,&current);
        if(err==ESP_OK){
            ESP_LOGI(TAG,"Mira beta fixed_auto IP applied with DHCP gateway/mask/DNS");
            return;
        }
        (void)esp_netif_dhcpc_start(sta_netif);
    }
    ESP_LOGW(TAG,"Mira beta fixed_auto IP setup failed; DHCP restored");
}

static volatile bool station_connected;
// Keep the recovery AP while unprovisioned; once STA is connected it can be
// disabled without changing or deleting any saved configuration. On network
// loss it is restored, so the firmware remains reachable for recovery.
static volatile bool recovery_ap_active=true;
static void recovery_ap_supervisor(void *arg){
    (void)arg;
    for(;;){
        vTaskDelay(pdMS_TO_TICKS(3000));
        const bool want_ap=!station_connected;
        if(want_ap==recovery_ap_active)continue;
        esp_err_t err=esp_wifi_set_mode(want_ap?WIFI_MODE_APSTA:WIFI_MODE_STA);
        if(err==ESP_OK){
            recovery_ap_active=want_ap;
            ESP_LOGI(TAG,"Recovery AP %s (%s)",want_ap?"enabled":"disabled",
                     want_ap?"station not connected":"station connected");
        }else ESP_LOGW(TAG,"Recovery AP mode update failed: %s",esp_err_to_name(err));
    }
}
static void wifi_status_event(void *arg,esp_event_base_t base,int32_t id,void *data){
    (void)arg;(void)data;
    if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){
        station_connected=true;
        legacy_finish_fixed_auto();
        if(!clock_sntp_started){
            esp_err_t time_err=esp_netif_sntp_start();
            if(time_err==ESP_OK)clock_sntp_started=true;
            else ESP_LOGW(TAG,"SNTP start failed: %s",esp_err_to_name(time_err));
        }
        ESP_LOGI(TAG,"Station Wi-Fi connected; recovery AP will be disabled");
    }else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){
        station_connected=false;
        legacy_fixed_auto_attempted=false;
        if(station_configured)esp_wifi_connect();
    }
}
static esp_err_t wifi_info_get(httpd_req_t *req){
    char status[100];
    snprintf(status,sizeof(status),
             "{\"configured\":%s,\"connected\":%s,\"recovery_ap\":%s}",
             station_configured?"true":"false",station_connected?"true":"false",
             recovery_ap_active?"true":"false");
    httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,status);
}
// Explicit provisioning: never overwrite or decode Mira Arduino /reseau.json.
// Wi-Fi STA profile is stored by ESP-IDF driver in NVS, AP remains available.
static esp_err_t wifi_profile_post(httpd_req_t *req){
    if(!mira_setup_require_ap(req))return ESP_FAIL;
    if(req->content_len<2||req->content_len>320){
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid network JSON size");return ESP_FAIL;
    }
    char body[321];size_t n=0,len=req->content_len;
    while(n<len){
        int r=httpd_req_recv(req,body+n,len-n);
        if(r<=0){httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Incomplete Wi-Fi JSON");return ESP_FAIL;}
        n+=(size_t)r;
    }
    body[len]=0;
    cJSON *json=cJSON_ParseWithLength(body,len);
    const cJSON *ssid=json?cJSON_GetObjectItemCaseSensitive(json,"ssid"):NULL;
    const cJSON *pass=json?cJSON_GetObjectItemCaseSensitive(json,"password"):NULL;
    if(!cJSON_IsString(ssid)||!ssid->valuestring||!*ssid->valuestring||
       strlen(ssid->valuestring)>32||!cJSON_IsString(pass)||!pass->valuestring||
       strlen(pass->valuestring)>63){
        if(json)cJSON_Delete(json);
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Expected SSID (1..32) and password (0..63)");
        return ESP_FAIL;
    }
    wifi_config_t sta={0};
    snprintf((char*)sta.sta.ssid,sizeof(sta.sta.ssid),"%s",ssid->valuestring);
    snprintf((char*)sta.sta.password,sizeof(sta.sta.password),"%s",pass->valuestring);
    cJSON_Delete(json);
    esp_wifi_disconnect();
    esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&sta);
    if(err==ESP_OK){
        station_configured=true;
        err=esp_wifi_connect();
    }
    if(err!=ESP_OK){httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"STA configuration failed");return ESP_FAIL;}
    httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,"{\"saved\":true,\"joining\":true}");
}

// Read the newest saved Arduino beta network entry when IDF Wi-Fi NVS has
// no station profile yet. Fixed-IP profiles are not silently converted to
// DHCP: provisioning remains available via the recovery AP in that case.
static bool restore_legacy_dhcp_wifi(void){
    FILE *fp=fopen("/spiffs/reseau.json","rb");
    if(!fp)return false;
    if(fseek(fp,0,SEEK_END)!=0){fclose(fp);return false;}
    const long len=ftell(fp);
    if(len<2||len>4096){fclose(fp);return false;}
    rewind(fp);
    char *buf=malloc((size_t)len+1);
    if(!buf){fclose(fp);return false;}
    const size_t read=fread(buf,1,(size_t)len,fp);
    fclose(fp);
    if(read!=(size_t)len){free(buf);return false;}
    buf[read]=0;
    cJSON *root=cJSON_ParseWithLength(buf,read);
    free(buf);
    const cJSON *networks=root?cJSON_GetObjectItemCaseSensitive(root,"reseau"):NULL;
    const int n=cJSON_IsArray(networks)?cJSON_GetArraySize(networks):0;
    const cJSON *entry=n>0?cJSON_GetArrayItem(networks,n-1):NULL;
    const cJSON *ssid=entry?cJSON_GetObjectItemCaseSensitive(entry,"ssid"):NULL;
    const cJSON *pass=entry?cJSON_GetObjectItemCaseSensitive(entry,"pass"):NULL;
    const cJSON *mode=entry?cJSON_GetObjectItemCaseSensitive(entry,"ip_mode"):NULL;
    const cJSON *ip=entry?cJSON_GetObjectItemCaseSensitive(entry,"ip"):NULL;
    const char *mode_text=cJSON_IsString(mode)?mode->valuestring:"";
    const char *ip_text=cJSON_IsString(ip)?ip->valuestring:"";
    bool dhcp=!strcmp(mode_text,"auto") ||
              (!mode_text[0]&&(!ip_text[0]||!strcmp(ip_text,"Auto")||
                               !strcmp(ip_text,"null")));
    bool restored=false;
    if(entry && !dhcp){
        ESP_LOGW(TAG,"Legacy static-IP Wi-Fi profile preserved; use recovery AP to provision IDF network");
    }else if(dhcp&&cJSON_IsString(ssid)&&ssid->valuestring&&
             cJSON_IsString(pass)&&pass->valuestring){
        const size_t ssid_len=strlen(ssid->valuestring);
        char password[256]={0};
        if(ssid_len>0&&ssid_len<=32 &&
           mira_legacy_decrypt(pass->valuestring,password,sizeof(password)) &&
           strlen(password)>=8&&strlen(password)<=63){
            wifi_config_t config={0};
            memcpy(config.sta.ssid,ssid->valuestring,ssid_len);
            memcpy(config.sta.password,password,strlen(password));
            if(esp_wifi_set_config(WIFI_IF_STA,&config)==ESP_OK){
                restored=true;
                ESP_LOGI(TAG,"Legacy Mira DHCP Wi-Fi station imported (credentials omitted from logs)");
            }
        }
        memset(password,0,sizeof(password));
    }
    if(root)cJSON_Delete(root);
    return restored;
}

esp_err_t mira_recovery_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Never erase NVS: contains user Mira/Jeedom settings.
        ESP_LOGE(TAG, "NVS incompatible, refusing to erase user data");
        return err;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "NVS");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_ap();
    sta_netif=esp_netif_create_default_wifi_sta();
    if(!sta_netif)return ESP_ERR_NO_MEM;
    // France / Europe-Paris local time, including automatic summer-time changes.
    // SNTP is initialized now but starts only when STA has an IP address.
    setenv("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3",1);
    tzset();
    esp_sntp_config_t sntp_cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp_cfg.start=false;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_sntp_init(&sntp_cfg));

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_FLASH), TAG, "wifi storage");
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_status_event,NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,wifi_status_event,NULL));
    wifi_config_t ap = {
        .ap = {
            .ssid = "Mira-LVGL96-43",
            .ssid_len = 0,
            .password = "MiraIDF2026",
            .channel = 6,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 2,
            .pmf_cfg = {.required = false},
        }
    };
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    if(xTaskCreate(recovery_ap_supervisor,"mira_ap_guard",3072,NULL,3,NULL)!=pdPASS)
        ESP_LOGW(TAG,"Recovery AP supervisor unavailable; AP remains enabled");
    wifi_config_t saved_sta={0};
    bool have_sta=esp_wifi_get_config(WIFI_IF_STA,&saved_sta)==ESP_OK&&saved_sta.sta.ssid[0];
    if(!have_sta){
        (void)restore_legacy_dhcp_wifi();
        memset(&saved_sta,0,sizeof(saved_sta));
        have_sta=esp_wifi_get_config(WIFI_IF_STA,&saved_sta)==ESP_OK&&saved_sta.sta.ssid[0];
    }
    if(have_sta){
        station_configured=true;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    }

    httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
    // 9 routes are registered below; ESP-IDF defaults to 8 URI handlers.
    // Exceeding that limit used to abort app_main() before LCD init, causing
    // a black screen + Wi-Fi reboot loop (ESP_ERR_HTTPD_HANDLERS_FULL).
    conf.max_uri_handlers = 40;
    conf.stack_size = 6144;
    conf.recv_wait_timeout = 15;
    ESP_LOGI(TAG, "HTTP URI capacity=%u, registered routes=9",
             (unsigned)conf.max_uri_handlers);
    httpd_handle_t httpd = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&httpd, &conf), TAG, "HTTP start");
    httpd_uri_t root = {.uri="/", .method=HTTP_GET, .handler=index_get};
    httpd_uri_t perf = {.uri="/perf", .method=HTTP_GET, .handler=perf_get};
    httpd_uri_t scroll_classic = {.uri="/scroll/classic", .method=HTTP_GET, .handler=scroll_classic_get};
    httpd_uri_t scroll_virtual = {.uri="/scroll/virtual", .method=HTTP_GET, .handler=scroll_virtual_get};
    httpd_uri_t shadow_on = {.uri="/shadow/on", .method=HTTP_GET, .handler=shadow_on_get};
    httpd_uri_t shadow_off = {.uri="/shadow/off", .method=HTTP_GET, .handler=shadow_off_get};
    httpd_uri_t shadow_light = {.uri="/shadow/light", .method=HTTP_GET, .handler=shadow_light_get};
    httpd_uri_t shadow_relief = {.uri="/shadow/relief", .method=HTTP_GET, .handler=shadow_relief_get};
    httpd_uri_t ota = {.uri="/update", .method=HTTP_POST, .handler=update_post};
    httpd_uri_t wifi_get={.uri="/mira/wifi",.method=HTTP_GET,.handler=wifi_info_get};
    httpd_uri_t wifi_post={.uri="/mira/wifi",.method=HTTP_POST,.handler=wifi_profile_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &root), TAG, "HTTP root");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &perf), TAG, "HTTP perf");
    // Prioritize OTA recovery: register it before experimental routes.
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &ota), TAG, "HTTP OTA");
    register_optional_route(httpd, &scroll_classic);
    register_optional_route(httpd, &scroll_virtual);
    register_optional_route(httpd, &shadow_on);
    register_optional_route(httpd, &shadow_off);
    register_optional_route(httpd, &shadow_light);
    register_optional_route(httpd, &shadow_relief);
    mira_full_register_http(httpd); // optional 0.7.37 SPIFFS config and upload
    mira_mqtt_register_http(httpd); // optional native MQTT settings
    mira_jeedom_register_http(httpd); // beta Jeedom protocol compatibility
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(httpd,&wifi_get));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(httpd,&wifi_post));
    ESP_LOGW(TAG, "Recovery AP Mira-IDF-Test password MiraIDF2026 at http://192.168.4.1");
    return ESP_OK;
}
