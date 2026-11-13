#pragma once
// Two-step initial setup copied from the Mira beta's existing HTML pages.
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
// Returns true when an unconfigured Mira request was handled.
bool mira_beta_setup_root(httpd_req_t *req);
bool mira_beta_setup_unconfigured(void);
// Session flag: configured at HTTP startup, never an authentication bypass.
void mira_beta_setup_mark_configured_runtime(void);
bool mira_beta_setup_runtime_configured(void);
// Read-only first-run diagnostics; never return a secret or stored SSID.
// Both presence flags and NVS/Wi-Fi read error codes are measured together.
void mira_beta_setup_diagnostic(bool *key_saved, bool *wifi_saved,
                                esp_err_t *key_read_err, esp_err_t *wifi_read_err);
esp_err_t mira_beta_setup_register(httpd_handle_t server);
esp_err_t mira_beta_setup_register_minimal(httpd_handle_t server);
// Catch captive OS probe URLs only on the AP while initial setup is pending.
esp_err_t mira_beta_setup_register_captive(httpd_handle_t server);
