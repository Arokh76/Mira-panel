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
// Waveshare 4.3C confirmed CH32 IO expander (EXIO2 gate + PWM 0x05).
// Mira 0.7.37 brightness scale: 0=off, 30..255 interactive visible.
esp_err_t mira_display_set_brightness(uint8_t raw);
// Native isolated DI0/DI1 and DO0/DO1 (Waveshare 4.3C CH32 I2C @0x24).
// Output levels always default OFF on boot; no WT32 GPIO pin reuse.
esp_err_t mira_display_io_read(bool *di0,bool *di1,bool *do0,bool *do1);
esp_err_t mira_display_io_write(unsigned channel,bool level);
lv_display_t *mira_display_lvgl(void);
void mira_display_touch_read(lv_indev_t *indev, lv_indev_data_t *data);
bool mira_display_touch_ready(void);
void mira_display_set_render_task(void);
void mira_display_stats(mira_display_stats_t *out);
