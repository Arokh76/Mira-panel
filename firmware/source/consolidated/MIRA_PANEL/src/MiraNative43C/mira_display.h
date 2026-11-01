#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t frames;
    uint32_t vsync_missed;
    uint32_t last_refresh_us;
    uint32_t vsync_events;
    uint32_t frame_complete_events;
    uint32_t wait_total_us;
    uint32_t touch_presses;
    uint32_t touch_reads;
    uint32_t touch_errors;
    uint32_t touch_last_x;
    uint32_t touch_last_y;
    uint32_t touch_ready_events;
    uint32_t touch_invalid_coords;
    uint32_t touch_raw_x;
    uint32_t touch_raw_y;
    uint32_t touch_config_x;
    uint32_t touch_config_y;
    uint8_t touch_last_status;
    uint8_t touch_irq_level;
    uint8_t touch_addr;
    bool touch_ready;
} mira_display_stats_t;

esp_err_t mira_display_init(void);
lv_display_t *mira_display_lvgl(void);
void mira_display_touch_read(lv_indev_t *indev, lv_indev_data_t *data);
// Executed on the LVGL UI loop, never in the independent I2C touch task.
// Return true to swallow this event (e.g. the first tap only wakes Mira).
typedef bool (*mira_touch_hook_t)(bool pressed, uint16_t x, uint16_t y);
void mira_display_set_touch_hook(mira_touch_hook_t hook);
bool mira_display_touch_ready(void);
void mira_display_set_brightness(uint8_t raw);
void mira_display_set_render_task(void);
void mira_display_stats(mira_display_stats_t *out);

#ifdef __cplusplus
}
#endif
