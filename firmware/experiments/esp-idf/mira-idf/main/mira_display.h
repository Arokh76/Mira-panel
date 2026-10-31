#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "esp_err.h"

typedef struct {
    uint32_t frames;
    uint32_t vsync_missed;
    uint32_t last_refresh_us;
    uint32_t vsync_events;
    uint32_t frame_complete_events;
    uint32_t wait_total_us;
} mira_display_stats_t;

esp_err_t mira_display_init(void);
lv_display_t *mira_display_lvgl(void);
void mira_display_touch_read(lv_indev_t *indev, lv_indev_data_t *data);
void mira_display_set_render_task(void);
void mira_display_stats(mira_display_stats_t *out);
