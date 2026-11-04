#pragma once
#include "esp_http_server.h"
// Legacy compatibility GET endpoints, restricted to IDF-specific releases.
esp_err_t mira_beta_update_check(httpd_req_t *req);
esp_err_t mira_beta_update_legacy_page(httpd_req_t *req);
esp_err_t mira_beta_update_start(httpd_req_t *req);

// Explicit native-only remote installation (never from GET).
esp_err_t mira_beta_update_trigger(httpd_req_t *req);
esp_err_t mira_beta_update_status(httpd_req_t *req);
