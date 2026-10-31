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
static volatile uint32_t current_fps_tenths = 0;
void mira_recovery_set_fps(uint32_t fps) { current_fps_tenths = fps; }

static const char recovery_page[] =
"<!doctype html><html lang='fr'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Mira IDF - Retour</title><h2>Mira ESP-IDF - Banc d'essai LVGL 9.6</h2>"
"<p>Cette application n'est pas le Mira Jeedom complet.</p>"
"<p>Pour revenir a Mira 0.7.37, selectionner son fichier <b>OTA .bin</b> (pas le factory.bin).</p>"
"<input type='file' id='fw' accept='.bin'><button onclick='flash()'>Reflasher</button>"
"<pre id='msg'>Pret</pre><p><a href='/perf'>FPS et statut JSON</a></p>"
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
static esp_err_t perf_get(httpd_req_t *req) {
    mira_display_stats_t s;
    mira_display_stats(&s);
    char text[224];
    snprintf(text, sizeof(text),
      "{\"fps_tenths\":%" PRIu32 ",\"frames\":%" PRIu32
      ",\"vsync_missed\":%" PRIu32 ",\"vsync_wait_us\":%" PRIu32
      ",\"uptime_ms\":%" PRIu32 "}",
      current_fps_tenths, s.frames, s.vsync_missed, s.last_refresh_us,
      (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
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
    conf.stack_size = 6144;
    conf.recv_wait_timeout = 15;
    httpd_handle_t httpd = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&httpd, &conf), TAG, "HTTP start");
    httpd_uri_t root = {.uri="/", .method=HTTP_GET, .handler=index_get};
    httpd_uri_t perf = {.uri="/perf", .method=HTTP_GET, .handler=perf_get};
    httpd_uri_t ota = {.uri="/update", .method=HTTP_POST, .handler=update_post};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &root), TAG, "HTTP root");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &perf), TAG, "HTTP perf");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(httpd, &ota), TAG, "HTTP OTA");
    ESP_LOGW(TAG, "Recovery AP Mira-IDF-Test password MiraIDF2026 at http://192.168.4.1");
    return ESP_OK;
}
