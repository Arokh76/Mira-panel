#include "mira_full.h"
#include "mira_mqtt.h"
#include "mira_jeedom.h"
#include "mira_setup_access.h"
#include "mira_beta_setup.h"
#include "mira_beta_networks.h"
#include "mira_beta_web.h"
#include "mira_jeedom.h"
#include "mira_beta_captive_dns.h"
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
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "esp_netif_sntp.h"
#include <time.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mira-idf-ota";
static bool http_recovery_only;
static uint32_t http_stack_configured;
bool mira_recovery_web_fallback(void){return http_recovery_only;}
static const char *volatile boot_stage="not_started";
const char *mira_recovery_last_stage(void){return boot_stage;}
static volatile int pending_shadow_request = -1;
static bool clock_sntp_started;
static bool clock_sntp_initialized;
static volatile bool clock_start_requested;
static volatile esp_err_t clock_last_start_error=ESP_OK;
static volatile bool clock_sntp_synced;
static volatile time_t clock_last_sync_epoch;
// Callback fires only after an NTP response actually updates the system clock.
static void mira_ntp_synced(struct timeval *tv){
    if(!tv)return;
    clock_last_sync_epoch=tv->tv_sec;
    clock_sntp_synced=true;
    ESP_LOGI(TAG,"NTP time synchronized; UTC epoch=%lld",(long long)tv->tv_sec);
}
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
    // Mirror the 0.7.37 first-run workflow: beta key page then Wi-Fi page.
    // The performance/recovery UI remains available after provisioning.
    if(mira_beta_setup_root(req))return ESP_OK;
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
    return mira_beta_web_home(req);
}
static esp_err_t recovery_debug_get(httpd_req_t *req){
    if(!mira_beta_web_authorized(req))return ESP_FAIL;
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    return httpd_resp_send(req,recovery_page,HTTPD_RESP_USE_STRLEN);
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
// Consistent with Stage 3 /status: a tiny, read-only runtime memory endpoint.
static esp_err_t web_rescue_page_get(httpd_req_t *req){
    static const char *html=
        "<!doctype html><html lang='fr'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Mira Panel - mode secours</title><h2>Mira Panel : serveur HTTP allege</h2>"
        "<p>Memoire interne insuffisante pour le serveur complet. Les donnees sont conservees.</p>"
        "<p><a href='/status'>Diagnostic memoire /status</a></p>";
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    return httpd_resp_sendstr(req,html);
}
static esp_err_t full_status_get(httpd_req_t *req){
    mira_display_stats_t d={0};
    mira_display_stats(&d);
    char out[384];
    int n=snprintf(out,sizeof(out),
       "{\"stage\":4,\"ssid\":\"Mira Panel\","
       "\"rgb_refreshes\":%" PRIu32 ",\"gt911_ready\":%s,"
       "\"widgets\":%" PRIu32 ",\"setup_required\":%s,"
       "\"dma_internal_free\":%u,\"dma_internal_largest_block\":%u,"
       "\"psram_free\":%u,\"http_mode\":\"%s\",\"http_stack\":%u,"
       "\"jeedom_passive_ready\":%s,\"jeedom_actions_ready\":%s,\"jeedom_mode_enabled\":%s}",
       d.frames,d.touch_ready?"true":"false",
       mira_full_widget_count(),mira_beta_setup_unconfigured()?"true":"false",
       (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA),
       (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
       http_recovery_only?"RECOVERY":"FULL",(unsigned)http_stack_configured,
       (mira_full_passive_ready()&&mira_jeedom_passive_ready())?"true":"false",
       mira_jeedom_action_ready()?"true":"false",
       mira_jeedom_actions_enabled()?"true":"false");
    if(n<0||(size_t)n>=sizeof(out))return httpd_resp_send_500(req);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,out,n);
}
static esp_err_t update_post(httpd_req_t *req) {
    // Original beta browser POST /update?send=ID, /update?send_range, etc.
    // Only a local authenticated user can invoke this command route.
    char query[500]={0};
    if(httpd_req_get_url_query_str(req,query,sizeof(query))==ESP_OK&&
       (strstr(query,"send=")||strstr(query,"send_range=")||
        strstr(query,"send_list=")||strstr(query,"send_color=")||
        strstr(query,"send_textbox="))){
        if(!mira_beta_web_authorized(req))return ESP_FAIL;
        return mira_jeedom_beta_web_action(req);
    }
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
static bool legacy_fixed_auto_attempted,legacy_fixed_auto_applied;

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
// The original beta Wi-Fi form uses auto/manual/fixed_auto. Keep the
// profiles in the native NVS namespace; never alter reseau.json from Arduino.
/* Native IP settings are stored as one validated JSON NVS value. A
 * single atomic profile prevents mixed DHCP/manual fields if power is lost
 * while saving. Legacy six-key preferences remain readable as fallback. */
static bool read_atomic_web_profile(nvs_handle_t nv,
    char mode[16],char ip[32],char mask[32],char gw[32],
    char dns1[32],char dns2[32]){
    char encoded[512]={0};
    size_t cap=sizeof(encoded);
    if(nvs_get_str(nv,"web_profile",encoded,&cap)!=ESP_OK)return false;
    cJSON *root=cJSON_Parse(encoded);
    if(!root)return false;
    const char *const keys[]={"mode","ip","mask","gateway","dns1","dns2"};
    char *const targets[]={mode,ip,mask,gw,dns1,dns2};
    const size_t sizes[]={16,32,32,32,32,32};
    bool valid=true;
    for(size_t i=0;i<6;i++){
        const cJSON *item=cJSON_GetObjectItemCaseSensitive(root,keys[i]);
        if(!cJSON_IsString(item)||!item->valuestring||
           strlen(item->valuestring)>=sizes[i]){valid=false;break;}
    }
    if(valid){
        for(size_t i=0;i<6;i++){
            const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,keys[i]);
            snprintf(targets[i],sizes[i],"%s",v->valuestring);
        }
        valid=!strcmp(mode,"auto")||!strcmp(mode,"manual")||
              !strcmp(mode,"fixed_auto");
    }
    cJSON_Delete(root);
    return valid;
}
static bool beta_web_load_ip_mode(void){
    nvs_handle_t nv;
    if(nvs_open("mira_idf_net",NVS_READONLY,&nv)!=ESP_OK)return false;
    char mode[16]={0},ip[32]={0},mask[32]={0},gw[32]={0},dns1[32]={0},dns2[32]={0};
    bool stored=read_atomic_web_profile(nv,mode,ip,mask,gw,dns1,dns2);
    if(!stored){
        size_t n=sizeof(mode);
        stored=nvs_get_str(nv,"web_mode",mode,&n)==ESP_OK;
        if(stored){
            n=sizeof(ip);(void)nvs_get_str(nv,"web_ip",ip,&n);
            n=sizeof(mask);(void)nvs_get_str(nv,"web_mask",mask,&n);
            n=sizeof(gw);(void)nvs_get_str(nv,"web_gw",gw,&n);
            n=sizeof(dns1);(void)nvs_get_str(nv,"web_dns1",dns1,&n);
            n=sizeof(dns2);(void)nvs_get_str(nv,"web_dns2",dns2,&n);
        }
    }
    nvs_close(nv);
    if(!stored)return false;
    legacy_ip_mode=PROFILE_DHCP;
    legacy_fixed_auto_attempted=false;
    legacy_fixed_auto_applied=false;
    if(!strcmp(mode,"auto"))return true;
    esp_ip4_addr_t address={0};
    if(!legacy_parse_ipv4(ip,&address)){
        ESP_LOGW(TAG,"Saved beta web IPv4 invalid; DHCP fallback");
        return true;
    }
    if(!strcmp(mode,"fixed_auto")){
        legacy_requested_ip=address;
        legacy_ip_mode=PROFILE_FIXED_AUTO;
        return true;
    }
    if(strcmp(mode,"manual"))return true;
    esp_ip4_addr_t nm={0},gateway={0},primary={0},backup={0};
    bool valid=legacy_parse_ipv4(mask,&nm)&&legacy_mask_valid(nm)&&
               legacy_parse_ipv4(gw,&gateway)&&legacy_parse_ipv4(dns1,&primary)&&
               legacy_same_subnet(address,gateway,nm)&&address.addr!=gateway.addr;
    if(dns2[0]&& !legacy_parse_ipv4(dns2,&backup))valid=false;
    uint32_t host=address.addr&~nm.addr;
    if(!valid||host==0||host==~nm.addr){
        ESP_LOGW(TAG,"Saved beta web manual network invalid; DHCP fallback");
        return true;
    }
    legacy_manual_ip.ip=address;
    legacy_manual_ip.gw=gateway;
    legacy_manual_ip.netmask=nm;
    memset(legacy_manual_dns,0,sizeof(legacy_manual_dns));
    legacy_manual_dns[0].ip.type=ESP_IPADDR_TYPE_V4;
    legacy_manual_dns[0].ip.u_addr.ip4.addr=primary.addr;
    legacy_manual_dns[1].ip.type=ESP_IPADDR_TYPE_V4;
    legacy_manual_dns[1].ip.u_addr.ip4.addr=backup.addr;
    legacy_ip_mode=PROFILE_MANUAL;
    return true;
}
esp_err_t mira_recovery_save_beta_wifi(const char *ssid,const char *password,
    const char *mode,const char *ip,const char *mask,
    const char *gateway,const char *dns1,const char *dns2){
    if(!ssid||!password||!mode||!ip||!mask||!gateway||!dns1||!dns2)return ESP_ERR_INVALID_ARG;
    const size_t ssid_len=strlen(ssid),pwd_len=strlen(password);
    if(!ssid_len||ssid_len>32||pwd_len>63||
       (pwd_len>0&&pwd_len<8))return ESP_ERR_INVALID_ARG;
    if(strcmp(mode,"auto")&&strcmp(mode,"manual")&&strcmp(mode,"fixed_auto"))
        return ESP_ERR_INVALID_ARG;
    if(strcmp(mode,"auto")){
        esp_ip4_addr_t addr={0};
        if(!legacy_parse_ipv4(ip,&addr))return ESP_ERR_INVALID_ARG;
        if(!strcmp(mode,"manual")){
            esp_ip4_addr_t nm={0},gw={0},dn={0},backup={0};
            if(!legacy_parse_ipv4(mask,&nm)||!legacy_mask_valid(nm)||
               !legacy_parse_ipv4(gateway,&gw)||
               !legacy_parse_ipv4(dns1,&dn)||
               (dns2[0]&&!legacy_parse_ipv4(dns2,&backup))||
               !legacy_same_subnet(addr,gw,nm)||addr.addr==gw.addr)
                return ESP_ERR_INVALID_ARG;
            uint32_t host=addr.addr&~nm.addr;
            if(host==0||host==~nm.addr)return ESP_ERR_INVALID_ARG;
        }
    }
    if(strlen(ip)>31||strlen(mask)>31||strlen(gateway)>31||
       strlen(dns1)>31||strlen(dns2)>31)return ESP_ERR_INVALID_ARG;
    // An empty password on the same SSID means retain its existing secret.
    wifi_config_t sta={0},prior={0};
    if(esp_wifi_get_config(WIFI_IF_STA,&prior)!=ESP_OK)return ESP_FAIL;
    memcpy(sta.sta.ssid,ssid,ssid_len);
    // The ESP-IDF SSID field can contain exactly 32 bytes (no NUL terminator).
    const size_t old_len=strnlen((const char*)prior.sta.ssid,sizeof(prior.sta.ssid));
    if(!pwd_len&&old_len==ssid_len&&!memcmp(prior.sta.ssid,ssid,ssid_len))
        memcpy(sta.sta.password,prior.sta.password,sizeof(sta.sta.password));
    else if(pwd_len)memcpy(sta.sta.password,password,pwd_len);
    /* Save the six dependent IP settings as a single NVS record.
     * Formerly six nvs_set_str calls could leave half a manual-IP profile
     * after an interrupted write, even when the STA credentials remained
     * correct. Both IDF and beta Web pages read web_profile first. */
    cJSON *profile=cJSON_CreateObject();
    if(!profile){
        memset(&sta,0,sizeof(sta));memset(&prior,0,sizeof(prior));
        return ESP_ERR_NO_MEM;
    }
    bool made=cJSON_AddStringToObject(profile,"mode",mode)!=NULL&&
        cJSON_AddStringToObject(profile,"ip",ip)!=NULL&&
        cJSON_AddStringToObject(profile,"mask",mask)!=NULL&&
        cJSON_AddStringToObject(profile,"gateway",gateway)!=NULL&&
        cJSON_AddStringToObject(profile,"dns1",dns1)!=NULL&&
        cJSON_AddStringToObject(profile,"dns2",dns2)!=NULL;
    char *profile_json=made?cJSON_PrintUnformatted(profile):NULL;
    cJSON_Delete(profile);
    if(!profile_json||strlen(profile_json)>511){
        if(profile_json)cJSON_free(profile_json);
        memset(&sta,0,sizeof(sta));memset(&prior,0,sizeof(prior));
        return ESP_ERR_NO_MEM;
    }

    (void)esp_wifi_disconnect();
    esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&sta);
    if(err==ESP_OK){
        nvs_handle_t nv;
        err=nvs_open("mira_idf_net",NVS_READWRITE,&nv);
        if(err==ESP_OK){
            err=nvs_set_str(nv,"web_profile",profile_json);
            if(err==ESP_OK)err=nvs_commit(nv);
            nvs_close(nv);
        }
    }
    cJSON_free(profile_json);
    if(err!=ESP_OK){
        // Configuring Wi-Fi flash and saving our own NVS are independent
        // operations. Restore the original STA config if either fails;
        // the old web_mode/web_profile values remain authoritative.
        esp_err_t rollback=esp_wifi_set_config(WIFI_IF_STA,&prior);
        if(rollback!=ESP_OK)
            ESP_LOGE(TAG,"Wi-Fi config rollback failed: %s",esp_err_to_name(rollback));
        (void)beta_web_load_ip_mode();
        if(sta_netif){
            if(legacy_ip_mode==PROFILE_MANUAL)legacy_apply_manual_ip();
            else (void)esp_netif_dhcpc_start(sta_netif);
        }
        if(rollback==ESP_OK)(void)esp_wifi_connect();
        memset(&sta,0,sizeof(sta));memset(&prior,0,sizeof(prior));
        return err;
    }
    memset(&sta,0,sizeof(sta));memset(&prior,0,sizeof(prior));
    (void)beta_web_load_ip_mode();
    if(sta_netif){
        if(legacy_ip_mode==PROFILE_MANUAL)legacy_apply_manual_ip();
        else (void)esp_netif_dhcpc_start(sta_netif);
    }
    station_configured=true;
    ESP_LOGI(TAG,"Beta Wi-Fi profile applied and atomically saved (%s)",mode);
    return esp_wifi_connect();
}
static bool legacy_dns_valid(const esp_netif_dns_info_t *dns){
    return dns && dns->ip.type==ESP_IPADDR_TYPE_V4 &&
           dns->ip.u_addr.ip4.addr!=0 &&
           dns->ip.u_addr.ip4.addr!=0xffffffffu;
}
static void legacy_finish_fixed_auto(void){
    if(legacy_ip_mode!=PROFILE_FIXED_AUTO||legacy_fixed_auto_attempted||!sta_netif)return;
    legacy_fixed_auto_attempted=true;
    esp_netif_ip_info_t current={0};
    if(esp_netif_get_ip_info(sta_netif,&current)!=ESP_OK||
       !legacy_mask_valid(current.netmask)||
       !legacy_same_subnet(legacy_requested_ip,current.gw,current.netmask)||
       !legacy_same_subnet(legacy_requested_ip,current.ip,current.netmask)||
       legacy_requested_ip.addr==current.gw.addr||
       (legacy_requested_ip.addr&~current.netmask.addr)==0||
       (legacy_requested_ip.addr&~current.netmask.addr)==~current.netmask.addr){
        ESP_LOGW(TAG,"Mira beta fixed_auto IP outside DHCP network; keeping DHCP");
        return;
    }

    // Port of Arduino MiraPanelServer::miraFinishWifiProfile():
    // WiFi.config(chosen,gateway,mask,dns1,dns2) preserves the DHCP
    // DNS *explicitly*. ESP-IDF esp_netif_set_ip_info() ONLY sets IP,
    // gateway and netmask, and stopping DHCP may clear DNS.
    // Capture both DNS servers while DHCP is still running.
    esp_netif_dns_info_t primary={0},backup={0};
    const esp_err_t dns_primary_err=esp_netif_get_dns_info(
        sta_netif,ESP_NETIF_DNS_MAIN,&primary);
    const esp_err_t dns_backup_err=esp_netif_get_dns_info(
        sta_netif,ESP_NETIF_DNS_BACKUP,&backup);
    const bool had_dhcp_dns=dns_primary_err==ESP_OK&&legacy_dns_valid(&primary);
    if(!had_dhcp_dns){
        // A DHCP server may omit DNS, in which case the local router often
        // offers DNS forwarding. Use ONLY the discovered DHCP gateway as
        // a last resort, never a hard-coded/public DNS or a user IP change.
        ESP_LOGW(TAG,"fixed_auto DHCP did not expose DNS (err=%s); using DHCP gateway as DNS fallback",
                 esp_err_to_name(dns_primary_err));
        memset(&primary,0,sizeof(primary));
        primary.ip.type=ESP_IPADDR_TYPE_V4;
        primary.ip.u_addr.ip4.addr=current.gw.addr;
    }
    if(dns_backup_err!=ESP_OK||!legacy_dns_valid(&backup))
        memset(&backup,0,sizeof(backup));

    // DHCP may already have leased exactly the requested IP. In that case
    // keep the running DHCP client and its valid DNS, as Arduino does.
    if(legacy_requested_ip.addr==current.ip.addr){
        if(!had_dhcp_dns){
            // DHCP-provided IP is already the requested one, but the
            // router did not advertise a resolver; keep this lease and
            // apply its gateway as DNS only as an explicit last resort.
            esp_err_t dns_err=esp_netif_set_dns_info(
                sta_netif,ESP_NETIF_DNS_MAIN,&primary);
            ESP_LOGW(TAG,"fixed_auto requested IP equals DHCP lease; DNS gateway fallback: %s",
                     esp_err_to_name(dns_err));
        }
        return;
    }

    esp_err_t err=esp_netif_dhcpc_stop(sta_netif);
    if(err==ESP_OK||err==ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED){
        current.ip=legacy_requested_ip;
        err=esp_netif_set_ip_info(sta_netif,&current);
        if(err==ESP_OK){
            // Restore DNS *after* stop/set_ip_info to avoid losing it.
            err=esp_netif_set_dns_info(sta_netif,ESP_NETIF_DNS_MAIN,&primary);
            if(err==ESP_OK&&legacy_dns_valid(&backup))
                err=esp_netif_set_dns_info(sta_netif,ESP_NETIF_DNS_BACKUP,&backup);
            if(err==ESP_OK){
                esp_netif_dns_info_t verify={0};
                err=esp_netif_get_dns_info(sta_netif,ESP_NETIF_DNS_MAIN,&verify);
                if(err==ESP_OK&&!legacy_dns_valid(&verify))
                    err=ESP_FAIL;
            }
        }
        if(err==ESP_OK){
            legacy_fixed_auto_applied=true;
            ESP_LOGI(TAG,"Mira beta fixed_auto: IP + DHCP gateway/mask/DNS restored");
            return;
        }
        // Retain the last good network path, never stay on a fixed IP with
        // missing DNS and never change the stored user configuration.
        ESP_LOGW(TAG,"fixed_auto transition failed (%s); returning to DHCP",
                 esp_err_to_name(err));
        (void)esp_netif_dhcpc_start(sta_netif);
    }
    ESP_LOGW(TAG,"Mira beta fixed_auto IP setup failed; DHCP restored");
}

static volatile bool station_connected;
static TickType_t clock_last_attempt_tick;
// Keep the recovery AP while unprovisioned; once STA is connected it can be
// disabled without changing or deleting any saved configuration. On network
// loss it is restored, so the firmware remains reachable for recovery.
static volatile bool recovery_ap_active=true;
static volatile bool beta_ap_persistent;
static esp_netif_t *ap_netif;
static void recovery_ap_supervisor(void *arg){
    (void)arg;
    for(;;){
        vTaskDelay(pdMS_TO_TICKS(3000));
        // Match validated Arduino: configure/start SNTP AFTER Wi-Fi has IP,
        // from the supervisor task (not inside the IP event callback).
        if(station_connected){
            const TickType_t now=xTaskGetTickCount();
            const bool retry_due=!clock_sntp_synced&&time(NULL)<1700000000&&
                (TickType_t)(now-clock_last_attempt_tick)>=pdMS_TO_TICKS(60000);
            if(clock_start_requested||retry_due){
                clock_start_requested=false;
                clock_last_attempt_tick=now;
                esp_err_t err=ESP_OK;
                if(!clock_sntp_initialized){
                    esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
                        2,ESP_SNTP_SERVER_LIST("pool.ntp.org","time.nist.gov"));
                    cfg.sync_cb=mira_ntp_synced;
                    // With a valid STA address, start on init, exactly as
                    // Arduino configTzTime() does after Wi-Fi connects.
                    err=esp_netif_sntp_init(&cfg);
                    if(err==ESP_OK)clock_sntp_initialized=true;
                }else{
                    err=esp_netif_sntp_start();
                }
                clock_last_start_error=err;
                if(err==ESP_OK)clock_sntp_started=true;
                ESP_LOGI(TAG,"NTP init/start after STA IP: %s, clock=%lld",
                         esp_err_to_name(err),(long long)time(NULL));
            }
        }
        const bool want_ap=beta_ap_persistent||!station_connected;
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
        // Schedule first initialization (or restart) in the dedicated task.
        // Do not invoke SNTP from the IP event handler.
        clock_start_requested=true;
        ESP_LOGI(TAG,"Station Wi-Fi connected; recovery AP will be disabled");
    }else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){
        station_connected=false;
        legacy_fixed_auto_attempted=false;
        if(legacy_ip_mode==PROFILE_FIXED_AUTO&&legacy_fixed_auto_applied&&sta_netif){
            legacy_fixed_auto_applied=false;
            // Obtain fresh DHCP subnet information on every reconnection
            // before applying the chosen fixed_auto address again.
            (void)esp_netif_dhcpc_start(sta_netif);
        }
        if(station_configured)esp_wifi_connect();
    }
}
static esp_err_t clock_status_get(httpd_req_t *req){
    const time_t now=time(NULL);
    const bool valid=now>1700000000;
    char local[48]="",dns_main[20]="",dns_backup[20]="",gateway[20]="";
    if(valid){
        struct tm tmv={0};
        if(localtime_r(&now,&tmv))
            strftime(local,sizeof(local),"%Y-%m-%d %H:%M:%S %z",&tmv);
    }
    if(sta_netif){
        esp_netif_dns_info_t dns={0};
        if(esp_netif_get_dns_info(sta_netif,ESP_NETIF_DNS_MAIN,&dns)==ESP_OK&&
           dns.ip.type==ESP_IPADDR_TYPE_V4)
            snprintf(dns_main,sizeof(dns_main),IPSTR,IP2STR(&dns.ip.u_addr.ip4));
        memset(&dns,0,sizeof(dns));
        if(esp_netif_get_dns_info(sta_netif,ESP_NETIF_DNS_BACKUP,&dns)==ESP_OK&&
           dns.ip.type==ESP_IPADDR_TYPE_V4)
            snprintf(dns_backup,sizeof(dns_backup),IPSTR,IP2STR(&dns.ip.u_addr.ip4));
        esp_netif_ip_info_t ip={0};
        if(esp_netif_get_ip_info(sta_netif,&ip)==ESP_OK)
            snprintf(gateway,sizeof(gateway),IPSTR,IP2STR(&ip.gw));
    }
    unsigned int reach0=0,reach1=0;
    if(clock_sntp_initialized){
        (void)esp_netif_sntp_reachability(0,&reach0);
        (void)esp_netif_sntp_reachability(1,&reach1);
    }
    char body[448];
    snprintf(body,sizeof(body),
        "{\"wifi_connected\":%s,\"ntp_started\":%s,\"ntp_synced\":%s,"
        "\"time_valid\":%s,\"epoch\":%lld,\"last_sync\":%lld,"
        "\"local\":\"%s\",\"ntp_initialized\":%s,"
        "\"last_ntp_start_error\":%d,\"dns_main\":\"%s\","
        "\"dns_backup\":\"%s\",\"gateway\":\"%s\","
        "\"ntp_reachability\":[%u,%u]}",
        station_connected?"true":"false",
        clock_sntp_started?"true":"false",
        clock_sntp_synced?"true":"false",
        valid?"true":"false",(long long)now,(long long)clock_last_sync_epoch,
        local,clock_sntp_initialized?"true":"false",
        (int)clock_last_start_error,dns_main,dns_backup,gateway,
        reach0,reach1);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,body);
}
static esp_err_t wifi_info_get(httpd_req_t *req){
    char status[180];
    const char *mode=legacy_ip_mode==PROFILE_MANUAL?"manual":
                     legacy_ip_mode==PROFILE_FIXED_AUTO?"fixed_auto":"auto";
    snprintf(status,sizeof(status),
             "{\"configured\":%s,\"connected\":%s,\"recovery_ap\":%s,\"ip_mode\":\"%s\",\"fixed_ip_applied\":%s}",
             station_configured?"true":"false",station_connected?"true":"false",
             recovery_ap_active?"true":"false",mode,
             (legacy_ip_mode==PROFILE_MANUAL||legacy_fixed_auto_applied)?"true":"false");
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
    const size_t password_len=strlen(pass->valuestring);
    if(password_len>0&&password_len<8){
        cJSON_Delete(json);
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Wi-Fi password must be empty or 8..63 bytes");
        return ESP_FAIL;
    }

    /* Match the full beta Web provisioning path. Previously this endpoint
     * set WIFI_STORAGE_FLASH without updating wifi_profiles. On reboot,
     * the startup stale-network check could then DELETE that STA config
     * because the selected SSID was absent from the NVS overlay. */
    mira_beta_network_t *saved=calloc(MIRA_NET_MAX,sizeof(*saved));
    if(!saved){cJSON_Delete(json);return httpd_resp_send_err(req,
                       HTTPD_500_INTERNAL_SERVER_ERROR,"Wi-Fi profile memory unavailable");}
    size_t count=0;
    esp_err_t err=mira_beta_networks_list(saved,&count,MIRA_NET_MAX);
    if(err!=ESP_OK){
        free(saved);cJSON_Delete(json);
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Wi-Fi profiles unavailable");
    }
    int profile_id=-1;
    for(size_t i=0;i<count;i++)
        if(!strcmp(saved[i].ssid,ssid->valuestring)){profile_id=saved[i].id;break;}
    if(profile_id<0&&count>=MIRA_NET_MAX){
        free(saved);cJSON_Delete(json);
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Too many saved Wi-Fi networks");
    }
    if(profile_id<0){
        for(int candidate=0;candidate<=999;candidate++){
            bool occupied=false;
            for(size_t i=0;i<count;i++)
                if(saved[i].id==candidate){occupied=true;break;}
            if(!occupied){profile_id=candidate;break;}
        }
    }
    free(saved);
    if(profile_id<0){
        cJSON_Delete(json);
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"No Wi-Fi profile ID available");
    }
    mira_beta_network_t profile={0};
    profile.id=profile_id;
    snprintf(profile.ssid,sizeof(profile.ssid),"%s",ssid->valuestring);
    snprintf(profile.ip_mode,sizeof(profile.ip_mode),"auto");
    // Keep the secrets in native NVS; no password goes into SPIFFS JSON.
    char password[64]={0};
    memcpy(password,pass->valuestring,password_len);
    cJSON_Delete(json);
    err=mira_recovery_save_beta_wifi(profile.ssid,password,"auto",
                                      "","","","","");
    if(err==ESP_OK)
        err=mira_beta_networks_upsert(&profile,password);
    memset(password,0,sizeof(password));
    if(err!=ESP_OK){
        ESP_LOGE(TAG,"Explicit Wi-Fi setup not fully saved: %s",esp_err_to_name(err));
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Wi-Fi setup incomplete; verify saved network");
    }
    httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,"{\"saved\":true,\"joining\":true}");
}

// Import the newest Mira beta network entry without modifying reseau.json.
// Only migrate the station password when the IDF network has no credentials.
// On later boots the saved migration marker allows reapplying the static
// mode while an explicitly configured IDF station remains authoritative.
static bool restore_legacy_wifi_profile(bool import_station){
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
    const bool dhcp=!strcmp(mode_text,"auto") ||
              (!mode_text[0]&&(!ip_text[0]||!strcmp(ip_text,"Auto")||
                               !strcmp(ip_text,"null")));
    bool restored=!import_station;
    if(import_station&&entry&&cJSON_IsString(ssid)&&ssid->valuestring&&
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
                ESP_LOGI(TAG,"Legacy Mira station profile imported (no credentials logged)");
            }
        }
        memset(password,0,sizeof(password));
    }
    if(restored && !dhcp && entry) {
        if(!strcmp(mode_text,"manual")){
            esp_ip4_addr_t ip_addr={0},gw={0},mask={0},dns={0},backup={0};
            const cJSON *gateway=cJSON_GetObjectItemCaseSensitive(entry,"gateway");
            const cJSON *netmask=cJSON_GetObjectItemCaseSensitive(entry,"mask");
            const cJSON *dns1=cJSON_GetObjectItemCaseSensitive(entry,"dns1");
            const cJSON *dns2=cJSON_GetObjectItemCaseSensitive(entry,"dns2");
            bool valid=legacy_parse_ipv4(ip_text,&ip_addr)&&
                cJSON_IsString(gateway)&&legacy_parse_ipv4(gateway->valuestring,&gw)&&
                cJSON_IsString(netmask)&&legacy_parse_ipv4(netmask->valuestring,&mask)&&
                cJSON_IsString(dns1)&&legacy_parse_ipv4(dns1->valuestring,&dns)&&
                legacy_mask_valid(mask)&&legacy_same_subnet(ip_addr,gw,mask)&&
                ip_addr.addr!=gw.addr;
            if(valid&&cJSON_IsString(dns2)&&dns2->valuestring&&dns2->valuestring[0])
                valid=legacy_parse_ipv4(dns2->valuestring,&backup);
            const uint32_t host=ip_addr.addr&~mask.addr;
            if(valid&&host!=0&&host!=~mask.addr){
                legacy_manual_ip.ip=ip_addr;
                legacy_manual_ip.gw=gw;
                legacy_manual_ip.netmask=mask;
                memset(legacy_manual_dns,0,sizeof(legacy_manual_dns));
                legacy_manual_dns[0].ip.type=ESP_IPADDR_TYPE_V4;
                legacy_manual_dns[0].ip.u_addr.ip4.addr=dns.addr;
                legacy_manual_dns[1].ip.type=ESP_IPADDR_TYPE_V4;
                legacy_manual_dns[1].ip.u_addr.ip4.addr=backup.addr;
                legacy_ip_mode=PROFILE_MANUAL;
            }else ESP_LOGW(TAG,"Invalid legacy manual IP profile; retaining DHCP");
        }else if(!strcmp(mode_text,"fixed_auto")||
                 (!mode_text[0]&&ip_text[0]&&strcmp(ip_text,"Auto")&&strcmp(ip_text,"null"))){
            if(legacy_parse_ipv4(ip_text,&legacy_requested_ip))
                legacy_ip_mode=PROFILE_FIXED_AUTO;
            else ESP_LOGW(TAG,"Invalid legacy fixed_auto address; retaining DHCP");
        }
    }
    if(root)cJSON_Delete(root);
    return restored;
}

// Same fields as beta HTML_ConfigAP.h: pass, ip, actif. Do not replace
// the existing identity/setup workflow or any SPIFFS file in this path.
esp_err_t mira_recovery_save_beta_ap(const char *password,const char *ip,bool active){
    if(!password||!ip)return ESP_ERR_INVALID_ARG;
    size_t len=strlen(password);
    if((len>0&&len<8)||len>63)return ESP_ERR_INVALID_ARG;
    esp_ip4_addr_t address={0};
    if(!legacy_parse_ipv4(ip,&address))return ESP_ERR_INVALID_ARG;
    // /24 is the beta AP's default; reject network and broadcast addresses.
    const uint32_t ip_host=ntohl(address.addr);
    if((ip_host&255u)==0u||(ip_host&255u)==255u)
        return ESP_ERR_INVALID_ARG;
    // A configured AP cannot share the STA subnet.
    esp_netif_ip_info_t sta_info={0};
    if(sta_netif&&esp_netif_get_ip_info(sta_netif,&sta_info)==ESP_OK&&
       sta_info.ip.addr&&
       (address.addr&sta_info.netmask.addr)==(sta_info.ip.addr&sta_info.netmask.addr))
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t nv;
    esp_err_t err=nvs_open("mira_idf_net",NVS_READWRITE,&nv);
    if(err!=ESP_OK)return err;
    err=nvs_set_str(nv,"ap_ip",ip);
    if(err==ESP_OK)err=nvs_set_u8(nv,"ap_enabled",active?1:0);
    if(err==ESP_OK)err=nvs_commit(nv);
    nvs_close(nv);
    if(err!=ESP_OK)return err;
    if(len){
        // In the original beta, AP password doubles as the admin password.
        // Explicitly changing this field updates the same native auth key.
        err=nvs_open("mira_idf_auth",NVS_READWRITE,&nv);
        if(err!=ESP_OK)return err;
        err=nvs_set_str(nv,"jeedom_key",password);
        if(err==ESP_OK)err=nvs_commit(nv);
        nvs_close(nv);
    }
    return err;
}
static void configure_beta_ap_profile(void){
    nvs_handle_t nv;
    char ip[32]="192.168.4.1",password[65]={0};
    size_t n=sizeof(ip);
    uint8_t active=0;
    if(nvs_open("mira_idf_net",NVS_READONLY,&nv)==ESP_OK){
        (void)nvs_get_str(nv,"ap_ip",ip,&n);
        (void)nvs_get_u8(nv,"ap_enabled",&active);
        nvs_close(nv);
    }
    beta_ap_persistent=active!=0;
    n=sizeof(password);
    if(nvs_open("mira_idf_auth",NVS_READONLY,&nv)==ESP_OK){
        (void)nvs_get_str(nv,"jeedom_key",password,&n);
        nvs_close(nv);
    }
    if(strlen(password)>=8&&strlen(password)<=63){
        wifi_config_t ap={0};
        snprintf((char*)ap.ap.ssid,sizeof(ap.ap.ssid),"%s","Mira Panel");
        memcpy(ap.ap.password,password,strlen(password));
        ap.ap.channel=6;
        ap.ap.max_connection=2;
        ap.ap.authmode=WIFI_AUTH_WPA2_PSK;
        esp_err_t err=esp_wifi_set_config(WIFI_IF_AP,&ap);
        if(err!=ESP_OK)ESP_LOGW(TAG,"Unable to configure beta AP password: %s",esp_err_to_name(err));
    }
    memset(password,0,sizeof(password));
    if(ap_netif&&strcmp(ip,"192.168.4.1")){
        esp_ip4_addr_t addr={0};
        if(legacy_parse_ipv4(ip,&addr)){
            esp_netif_ip_info_t info={0};
            info.ip=addr;info.gw=addr;
            info.netmask.addr=htonl(0xffffff00u);
            esp_err_t err=esp_netif_dhcps_stop(ap_netif);
            if(err==ESP_OK||err==ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED){
                err=esp_netif_set_ip_info(ap_netif,&info);
                if(err==ESP_OK)(void)esp_netif_dhcps_start(ap_netif);
                else ESP_LOGW(TAG,"Beta AP IP update rejected: %s",esp_err_to_name(err));
            }
        }
    }
}

esp_err_t mira_recovery_init(void) {
    boot_stage="nvs";
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Never erase NVS: contains user Mira/Jeedom settings.
        ESP_LOGE(TAG, "NVS incompatible, refusing to erase user data");
        return err;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "NVS");
    boot_stage="netif";
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    boot_stage="event_loop";
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    boot_stage="ap_netif";
    ap_netif=esp_netif_create_default_wifi_ap();
    if(!ap_netif)return ESP_ERR_NO_MEM;
    boot_stage="sta_netif";
    sta_netif=esp_netif_create_default_wifi_sta();
    if(!sta_netif)return ESP_ERR_NO_MEM;
    // France / Europe-Paris local time, including automatic summer-time changes.
    // SNTP is initialized now but starts only when STA has an IP address.
    setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);
    tzset();
    // On WT32, configTzTime() is called ONLY once the Wi-Fi is connected.
    // Initialize ESP-NETIF SNTP in recovery_ap_supervisor after STA_GOT_IP.
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    boot_stage="wifi_init";
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    boot_stage="wifi_storage";
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_FLASH), TAG, "wifi storage");
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_status_event,NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,wifi_status_event,NULL));
    wifi_config_t ap = {
        .ap = {
            .ssid = "Mira Panel",
            .ssid_len = 0,
            .password = "",
            .channel = 6,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = 2,
            .pmf_cfg = {.required = false},
        }
    };
    boot_stage="wifi_mode";
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "wifi mode");
    boot_stage="ap_config";
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "wifi config");
    configure_beta_ap_profile();
    boot_stage="wifi_start";
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    // Only the unconfigured setup AP needs a DNS wildcard. On a configured
    // display, skipping this 4KB task preserves internal DRAM for MQTT/Jeedom.
    // DNS wildcard requires a permanent 4KB FreeRTOS task. For the first
    // memory recovery A/B, connect manually to http://192.168.4.1 instead.
    // No captive DNS startup until we verify the heap reserve.
    ESP_LOGI(TAG,"Captive DNS deferred to preserve RAM; use http://192.168.4.1");
    // Important: the 3KB AP supervisor was created before the 10KB HTTP
    // server and made the required contiguous internal stack impossible.
    // Start it ONLY AFTER the complete HTTP server is running.
    wifi_config_t saved_sta={0};
    bool have_sta=esp_wifi_get_config(WIFI_IF_STA,&saved_sta)==ESP_OK&&saved_sta.sta.ssid[0];
    nvs_handle_t profile_nv;
    uint8_t legacy_saved=0;
    bool has_native_profiles=false,has_web_profile=false;
    if(nvs_open("mira_idf_net",NVS_READONLY,&profile_nv)==ESP_OK){
        (void)nvs_get_u8(profile_nv,"legacy_wifi",&legacy_saved);
        size_t n=0;
        has_native_profiles=nvs_get_str(profile_nv,"wifi_profiles",NULL,&n)==ESP_OK&&n>2;
        char mode[16]={0},ip[32]={0},mask[32]={0},gw[32]={0},
             dns1[32]={0},dns2[32]={0};
        has_web_profile=read_atomic_web_profile(profile_nv,mode,ip,mask,gw,dns1,dns2);
        nvs_close(profile_nv);
    }
    // A network explicitly deleted from the native beta profile list must
    // NOT be silently imported again from its read-only Arduino backup.
    if(has_native_profiles&&have_sta){
        // If the user deleted the active SSID in Mira's native list, do not
        // resurrect it from Wi-Fi flash storage after a reboot.
        // This array was previously an automatic variable (>3.4 KiB) in
        // app_main's startup call chain. It can smash the 3.5 KiB default
        // ESP-IDF main-task stack before the HTTP server or LVGL is started.
        // Allocate on the heap only when the native NVS overlay is present.
        mira_beta_network_t *profiles=calloc(MIRA_NET_MAX,sizeof(*profiles));
        if(!profiles){
            ESP_LOGW(TAG,"Network profile list skipped: not enough heap");
        }else{
            size_t count=0;
            if(mira_beta_networks_list(profiles,&count,MIRA_NET_MAX)==ESP_OK){
                bool listed=false;
                size_t active_len=strnlen((const char*)saved_sta.sta.ssid,
                                           sizeof(saved_sta.sta.ssid));
                for(size_t i=0;i<count;i++){
                    if(strlen(profiles[i].ssid)==active_len&&
                       !memcmp(profiles[i].ssid,saved_sta.sta.ssid,active_len)){
                        listed=true;
                        break;
                    }
                }
                if(!listed){
                    wifi_config_t blank={0};
                    if(esp_wifi_set_config(WIFI_IF_STA,&blank)==ESP_OK){
                        have_sta=false;
                        memset(&saved_sta,0,sizeof(saved_sta));
                        ESP_LOGI(TAG,"Previously deleted Wi-Fi profile not restored");
                    }
                }
            }
            free(profiles);
        }
    }
    if(!has_native_profiles&&!has_web_profile&&(!have_sta || legacy_saved)){
        bool imported=restore_legacy_wifi_profile(!have_sta);
        if(imported&&!have_sta){
            if(nvs_open("mira_idf_net",NVS_READWRITE,&profile_nv)==ESP_OK){
                if(nvs_set_u8(profile_nv,"legacy_wifi",1)==ESP_OK)
                    (void)nvs_commit(profile_nv);
                nvs_close(profile_nv);
            }
        }
        memset(&saved_sta,0,sizeof(saved_sta));
        have_sta=esp_wifi_get_config(WIFI_IF_STA,&saved_sta)==ESP_OK&&saved_sta.sta.ssid[0];
    }
    if(have_sta){
        // Explicit IDF web configuration takes priority over imported beta files.
        const bool web_profile=beta_web_load_ip_mode();
        // A fresh Mira beta wizard may have saved a requested IP with
        // DHCP-assisted gateway/mask/DNS, just like fixed_auto on the beta.
        if(!web_profile&&legacy_ip_mode==PROFILE_DHCP &&
           nvs_open("mira_idf_net",NVS_READONLY,&profile_nv)==ESP_OK){
            char requested[32]={0};size_t n=sizeof(requested);
            if(nvs_get_str(profile_nv,"setup_ip",requested,&n)==ESP_OK &&
               legacy_parse_ipv4(requested,&legacy_requested_ip)){
                legacy_ip_mode=PROFILE_FIXED_AUTO;
                ESP_LOGI(TAG,"Mira first-run requested IP will be checked against DHCP subnet");
            }
            nvs_close(profile_nv);
        }
        station_configured=true;
        legacy_apply_manual_ip();
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    }

    ESP_LOGI(TAG,"Startup stack available before HTTP: %u bytes",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
    // 9 routes are registered below; ESP-IDF defaults to 8 URI handlers.
    // Exceeding that limit used to abort app_main() before LCD init, causing
    // a black screen + Wi-Fi reboot loop (ESP_ERR_HTTPD_HANDLERS_FULL).
    const bool setup_only=mira_beta_setup_unconfigured();
    conf.max_uri_handlers = setup_only?12:96;
    conf.uri_match_fn = httpd_uri_match_wildcard;
    // Beta HTML templates, JSON status pages and multipart OTA need deeper
    // handler call chains than the initial diagnostic web server.
    // The configured 10KB HTTP task stack did not fit alongside RGB DMA
    // and AP+STA. Keep stack INTERNAL: NVS and OTA handlers write flash.
    // 7168 is a compromise; report its observed high-water mark via /status.
    conf.stack_size = setup_only?6144:7168;
    conf.task_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    conf.recv_wait_timeout = 15;
    const uint32_t internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    ESP_LOGI(TAG,"HTTPD_PREFLIGHT setup=%u stack=%u routes=%u internal_free=%u largest=%u dma_free=%u",
        setup_only?1u:0u,(unsigned)conf.stack_size,(unsigned)conf.max_uri_handlers,
        (unsigned)heap_caps_get_free_size(internal),
        (unsigned)heap_caps_get_largest_free_block(internal),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));
    httpd_handle_t httpd = NULL;
    boot_stage="httpd_start";
    http_stack_configured=conf.stack_size;
    esp_err_t http_error=httpd_start(&httpd, &conf);
    if(http_error!=ESP_OK && !setup_only &&
       (http_error==ESP_ERR_HTTPD_TASK||http_error==ESP_ERR_HTTPD_ALLOC_MEM)){
        ESP_LOGW(TAG,"Full HTTP server rejected (%s); using internal-RAM rescue server",
                 esp_err_to_name(http_error));
        // Last-resort small server keeps /status and Wi-Fi reachable and
        // avoids a boot loop. No full API/UI task startup in this mode.
        conf.max_uri_handlers=8;
        conf.stack_size=4096;
        conf.max_open_sockets=2;
        http_stack_configured=conf.stack_size;
        boot_stage="httpd_rescue";
        ESP_LOGW(TAG,"HTTPD_RESCUE preflight: free=%u largest=%u",
                 (unsigned)heap_caps_get_free_size(internal),
                 (unsigned)heap_caps_get_largest_free_block(internal));
        http_error=httpd_start(&httpd,&conf);
        if(http_error==ESP_OK)http_recovery_only=true;
    }
    ESP_RETURN_ON_ERROR(http_error,TAG,"HTTP task start");
    httpd_uri_t root = {.uri="/", .method=HTTP_GET, .handler=index_get};
    httpd_uri_t status = {.uri="/status", .method=HTTP_GET, .handler=full_status_get};
    httpd_uri_t debug = {.uri="/recovery", .method=HTTP_GET, .handler=recovery_debug_get};
    httpd_uri_t perf = {.uri="/perf", .method=HTTP_GET, .handler=perf_get};
    httpd_uri_t scroll_classic = {.uri="/scroll/classic", .method=HTTP_GET, .handler=scroll_classic_get};
    httpd_uri_t scroll_virtual = {.uri="/scroll/virtual", .method=HTTP_GET, .handler=scroll_virtual_get};
    httpd_uri_t shadow_on = {.uri="/shadow/on", .method=HTTP_GET, .handler=shadow_on_get};
    httpd_uri_t shadow_off = {.uri="/shadow/off", .method=HTTP_GET, .handler=shadow_off_get};
    httpd_uri_t shadow_light = {.uri="/shadow/light", .method=HTTP_GET, .handler=shadow_light_get};
    httpd_uri_t shadow_relief = {.uri="/shadow/relief", .method=HTTP_GET, .handler=shadow_relief_get};
    httpd_uri_t ota = {.uri="/update", .method=HTTP_POST, .handler=update_post};
    httpd_uri_t clock_get={.uri="/mira/clock/status",.method=HTTP_GET,.handler=clock_status_get};
    httpd_uri_t wifi_get={.uri="/mira/wifi",.method=HTTP_GET,.handler=wifi_info_get};
    httpd_uri_t wifi_post={.uri="/mira/wifi",.method=HTTP_POST,.handler=wifi_profile_post};
    boot_stage="routes";
    if(http_recovery_only){
        const httpd_uri_t rescue_index={
            .uri="/",.method=HTTP_GET,.handler=web_rescue_page_get
        };
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd,&rescue_index),TAG,"HTTP rescue root");
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd,&status),TAG,"HTTP rescue status");
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd,&perf),TAG,"HTTP rescue perf");
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd,&wifi_get),TAG,"HTTP rescue Wi-Fi GET");
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd,&wifi_post),TAG,"HTTP rescue Wi-Fi POST");
        boot_stage="httpd_rescue_ready";
        ESP_LOGW(TAG,"HTTPD_RESCUE_READY: /status and basic Wi-Fi, full Mira services deferred");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &root), TAG, "HTTP root");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &perf), TAG, "HTTP perf");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &status), TAG, "HTTP status");
    if(setup_only){
        // Original key/Wi-Fi wizard, captive fallback, status and perf.
        // No OTA on an open first-run AP before the local password is chosen.
        ESP_RETURN_ON_ERROR(mira_beta_setup_register_minimal(httpd),TAG,"Mira setup wizard");
        ESP_RETURN_ON_ERROR(mira_beta_setup_register_captive(httpd),TAG,"Mira captive portal");
        boot_stage="setup_ready";
        ESP_LOGI(TAG,"MIRA_SETUP_READY: Mira Panel AP, http://192.168.4.1");
        return ESP_OK;
    }
    register_optional_route(httpd,&debug);
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
    ESP_RETURN_ON_ERROR(mira_beta_setup_register(httpd),TAG,"Mira beta first-run setup");
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(httpd,&clock_get));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(httpd,&wifi_get));
    ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(httpd,&wifi_post));
    // Catch Android/iOS/Windows captive-portal probe paths only after all
    // explicit Jeedom/API/OTA handlers have been registered.
    ESP_ERROR_CHECK_WITHOUT_ABORT(mira_beta_setup_register_captive(httpd));
    boot_stage="full_ready";
    // This task is optional. Start only after the 7KB HTTP task has reserved
    // its internal stack, otherwise it starves HTTP with a 3KB stack.
    if(heap_caps_get_largest_free_block(internal)>=4096){
        if(xTaskCreate(recovery_ap_supervisor,"mira_ap_guard",3072,NULL,3,NULL)!=pdPASS)
            ESP_LOGW(TAG,"AP guard task unavailable (deferred)");
    }else ESP_LOGW(TAG,"AP guard deferred: low internal heap");
    ESP_LOGI(TAG, "Mira 0.7.37 configured AP SSID Mira Panel; http://192.168.4.1");
    return ESP_OK;
}
