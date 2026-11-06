#pragma once
// Two-step initial setup copied from the Mira beta's existing HTML pages.
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
// Returns true when an unconfigured Mira request was handled.
bool mira_beta_setup_root(httpd_req_t *req);
bool mira_beta_setup_unconfigured(void);
esp_err_t mira_beta_setup_register(httpd_handle_t server);
// Catch captive OS probe URLs only on the AP while initial setup is pending.
esp_err_t mira_beta_setup_register_captive(httpd_handle_t server);
