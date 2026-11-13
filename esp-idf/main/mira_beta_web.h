#pragma once
#include "esp_http_server.h"
#include <stdbool.h>
#include <stdint.h>
// HTTP pages and form posts from firmware-beta/HTML, implemented natively for ESP-IDF.
// No Arduino web server, no replacement UI.
esp_err_t mira_beta_web_load(httpd_req_t *req);
esp_err_t mira_beta_web_save(httpd_req_t *req);
// Non-blocking Wi-Fi selector and authenticated cached scan endpoint.
esp_err_t mira_beta_web_wifi_choices(httpd_req_t *req);
esp_err_t mira_beta_web_wifi_scan_get(httpd_req_t *req);
esp_err_t mira_beta_web_display_save(httpd_req_t *req);

// Shared by beta System OTA and pages; enforces the original admin password.
bool mira_beta_web_authorized(httpd_req_t *req);
// Diagnostic only: aggregate failed NVS key reads, never expose key material.
void mira_beta_web_auth_diagnostic(uint32_t *failures,esp_err_t *last_error);

// Serve exact beta HTML_Index page with real Jeedom command controls.
esp_err_t mira_beta_web_home(httpd_req_t *req);

// Waveshare 4.3C isolated digital I/O: authenticated read, guarded POST.
esp_err_t mira_beta_web_gpio_state(httpd_req_t *req);
esp_err_t mira_beta_web_gpio_set(httpd_req_t *req);
esp_err_t mira_beta_web_gpio_raw(httpd_req_t *req);
esp_err_t mira_beta_web_gpio_config(httpd_req_t *req);
esp_err_t mira_beta_web_gpio_link(httpd_req_t *req);
esp_err_t mira_beta_web_gpio_multi(httpd_req_t *req);

// Beta network saved-list forms (POST-only with expiring nonce).
esp_err_t mira_beta_web_network_post(httpd_req_t *req);
