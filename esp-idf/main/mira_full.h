#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "lvgl.h"
#include "esp_http_server.h"

// Waveshare 7-inch: one UI widget may expose many Jeedom commands.
// Keep this shared with both the renderer and the HTTP programming endpoints.
#define MIRA_WIDGET_LIMIT 20

bool mira_full_storage_prepare(void); // mount/format only if blank, no queues
bool mira_full_storage_init(void);
// Experimental Jeedom receive-only /updatenr and /get transport, no workers.
bool mira_full_passive_init(void);
bool mira_full_passive_ready(void);
// Hardware-only offline UI test: mount existing SPIFFS; never format user data.
bool mira_full_storage_mount_existing_only(void);
void mira_full_ui_create(lv_display_t *disp);
void mira_full_ui_tick(void);
void mira_full_register_http(httpd_handle_t server);
uint32_t mira_full_widget_count(void);
void mira_full_navigation_stats(uint32_t *count,uint32_t *last_heap,uint32_t *min_heap);
// Network tasks may only queue passive states, never touch LVGL directly.
void mira_full_request_reload(void);
// HTTP settings handlers only signal; LVGL tree and backlight change on UI task.
void mira_full_request_settings_reload(void);
bool mira_full_enqueue_state(const char *name,const char *value);
// Beta Display MQTT control: parsed by LVGL task, never by MQTT callback.
bool mira_full_enqueue_display_command(const char *key,const char *value);

// Latest passive HTTP/MQTT value, or false if no value has arrived.
bool mira_full_cached_state(const char *name,char *out,size_t cap);

// ESP-IDF counterpart of Dimitri JSON-B1 diagnostics. cJSON has no
// ArduinoJson garbage collector; track cache pressure and dropped inputs.
typedef struct {
    uint32_t state_cache_used;
    uint32_t state_cache_capacity;
    uint32_t state_unchanged;
    uint32_t state_cache_evictions;
    uint32_t state_queue_dropped;
    uint32_t state_queue_pending;
} mira_full_state_diagnostics_t;
void mira_full_state_diagnostics(mira_full_state_diagnostics_t *out);
