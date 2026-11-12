#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
// Read-only and authenticated Jeedom plugin protocol + safe programming and state commands.
void mira_jeedom_register_http(httpd_handle_t server);

// Called once after NVS, Wi-Fi and SPIFFS are initialized.
esp_err_t mira_jeedom_action_init(void);
// Queue and worker status, independent of Jeedom enabled/disabled UI setting.
bool mira_jeedom_action_ready(void);
// First memory-budget phase: passive HTTP/SSE without any outbound task.
esp_err_t mira_jeedom_passive_init(void);
bool mira_jeedom_passive_ready(void);
// Nonblocking, ONLY called by LVGL direct-user-touch callbacks. Never by States.
bool mira_jeedom_queue_local_action(const char *name,const char *type,const char *value);

// Legacy GenericCmd URL fallback from Paramètres when Jeedom/MQTT are disabled.
bool mira_generic_queue_local_action(const char *name,const char *type,const char *value);

// Is Jeedom enabled? LVGL also publishes via MQTT independently when configured,
// reproducing the firmware-beta dual-output behavior.
bool mira_jeedom_actions_enabled(void);

// Wake authenticated long-lived SSE browsers after a passive state changes.
// Never queues a physical command; safe from the LVGL/network tasks.
void mira_jeedom_sse_notify(void);

// Original beta home page: authenticated HTTP-only rendering / action bridge.
esp_err_t mira_jeedom_render_beta_actions(httpd_req_t *req);
esp_err_t mira_jeedom_beta_web_action(httpd_req_t *req);

// Read-only page of historic HTTP IDs and authenticated beta GET /update.
esp_err_t mira_jeedom_render_http_reference(httpd_req_t *req);
esp_err_t mira_jeedom_beta_legacy_get_update(httpd_req_t *req);
