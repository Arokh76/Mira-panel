// Isolated IDF test recovery AP. Uses raw .bin POST, not multipart/form-data.
// Partition layout must match Mira's current Waveshare OTA partition layout.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "mira_recovery.h"
#include "mira_display.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mira-idf-ota";
static volatile int pending_shadow_request = -1;
static volatile int pending_scroll_request = -1;
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
void mira_recovery_set_fps(uint32_t fps) { current_fps_tenths = fps; }
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
    char text[900];
    snprintf(text, sizeof(text),
      "{\"fps_tenths\":%" PRIu32 ",\"frames\":%" PRIu32
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
      ",\"scroll_mode\":%" PRIu32 ",\"scroll_switches\":%" PRIu32
      ",\"scroll_rebuild_us\":%" PRIu32 "}",
      current_fps_tenths, s.frames, s.vsync_missed, s.last_refresh_us,
      (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
      s.vsync_events, s.frame_complete_events,
      current_draw_avg_us, current_wait_avg_us, current_handler_max_us,
      (uint32_t)(s.touch_ready ? 1 : 0), (uint32_t)s.touch_addr,
      s.touch_presses, s.touch_reads, s.touch_errors,
      s.touch_last_x, s.touch_last_y, current_shadow_mode,
      (uint32_t)s.touch_last_status, (uint32_t)s.touch_irq_level,
      s.touch_ready_events, s.touch_invalid_coords,
      s.touch_raw_x, s.touch_raw_y, s.touch_config_x, s.touch_config_y,
      current_scroll_mode, current_scroll_switches, current_scroll_rebuild_us);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, text);
}
static esp_err_t update_post(httpd_req_t *req) {
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

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    wifi_config_t ap = {
        .ap = {
            .ssid = "Mira-IDF-Test",
            .ssid_len = 0,
            .password = "MiraIDF2026",
            .channel = 6,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 2,
            .pmf_cfg = {.required = false},
        }
    };
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "wifi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
    // 9 routes are registered below; ESP-IDF defaults to 8 URI handlers.
    // Exceeding that limit used to abort app_main() before LCD init, causing
    // a black screen + Wi-Fi reboot loop (ESP_ERR_HTTPD_HANDLERS_FULL).
    conf.max_uri_handlers = 16;
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
    ESP_LOGW(TAG, "Recovery AP Mira-IDF-Test password MiraIDF2026 at http://192.168.4.1");
    return ESP_OK;
}
