#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
// Read-only and authenticated Jeedom plugin protocol + safe programming and state commands.
void mira_jeedom_register_http(httpd_handle_t server);

// Called once after NVS, Wi-Fi and SPIFFS are initialized.
esp_err_t mira_jeedom_action_init(void);
// Nonblocking, ONLY called by LVGL direct-user-touch callbacks. Never by States.
bool mira_jeedom_queue_local_action(const char *name,const char *type,const char *value);

// Select exactly one outbound transport: when Jeedom is enabled, avoid duplicate MQTT actions.
bool mira_jeedom_actions_enabled(void);

// Original beta home page: authenticated HTTP-only rendering / action bridge.
esp_err_t mira_jeedom_render_beta_actions(httpd_req_t *req);
esp_err_t mira_jeedom_beta_web_action(httpd_req_t *req);
