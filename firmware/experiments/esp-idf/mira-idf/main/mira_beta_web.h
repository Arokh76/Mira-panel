#pragma once
#include "esp_http_server.h"
// HTTP pages and form posts from firmware-beta/HTML, implemented natively for ESP-IDF.
// No Arduino web server, no replacement UI.
esp_err_t mira_beta_web_load(httpd_req_t *req);
esp_err_t mira_beta_web_save(httpd_req_t *req);
esp_err_t mira_beta_web_display_save(httpd_req_t *req);
