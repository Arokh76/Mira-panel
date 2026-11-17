#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"
// Read-only and authenticated Jeedom plugin protocol + safe programming and state commands.
void mira_jeedom_register_http(httpd_handle_t server, unsigned max_open_sockets);

// Called once after NVS, Wi-Fi and SPIFFS are initialized.
esp_err_t mira_jeedom_action_init(void);
// Notify the Jeedom worker when STA obtains an IP (initial boot or reconnect).
// Queues read-only state replay; never performs HTTP inside the Wi-Fi callback.
void mira_jeedom_network_reconnected(void);
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

// ESPAsyncWebServer transport bridge: UTF-8 query values are already decoded.
// Buffer returned by index_snapshot is PSRAM allocated; caller must free().
bool mira_jeedom_async_key_valid(const char *key);
esp_err_t mira_jeedom_async_index_snapshot(char **body,size_t *length);
int mira_jeedom_async_updatenr(const char *kind,const char *selector,
    const char *incoming,const char *red,const char *green,const char *blue,
    char *answer,size_t answer_capacity);
// Browser replay on the async EventSource uses the same resident index/cache.
size_t mira_jeedom_async_event_count(void);
bool mira_jeedom_async_event_current(size_t ordinal,int *id,char *value,size_t capacity);

// Native ESPAsyncWebServer programming routes. Invoke from a worker task,
// never from AsyncTCP callbacks. Authentication is enforced by the caller.
int mira_jeedom_async_set_binding(const char *id,const char *value);
int mira_jeedom_async_set_toggle_binding(const char *id,const char *off,const char *on);
int mira_jeedom_async_set_init_id(const char *id);
int mira_jeedom_async_bindings_json(const char *json,char *answer,size_t capacity);

bool mira_jeedom_async_param_get(const char *name,char *out,size_t capacity);
int mira_jeedom_async_param_set(const char *name,const char *value);
