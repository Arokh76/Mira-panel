#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "esp_http_server.h"

bool mira_full_storage_init(void);
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
