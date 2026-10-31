// Mira Panel ESP-IDF + LVGL 9.6 scrolling performance baseline.
// All display hardware details isolated in mira_display.c.
#include <inttypes.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mira_display.h"
#include "mira_ui.h"
#include "mira_recovery.h"

void mira_recovery_set_fps(uint32_t fps);
static const char *TAG = "mira-idf";
static void lvgl_loop(void *arg) {
    (void)arg;
    mira_display_set_render_task();
    mira_display_stats_t prev = {0};
    uint64_t last_us = esp_timer_get_time();
    for (;;) {
        uint32_t next_ms = lv_timer_handler();
        uint64_t now_us = esp_timer_get_time();
        if (now_us - last_us >= 1000000ULL) {
            mira_display_stats_t curr;
            mira_display_stats(&curr);
            uint32_t dt_us = (uint32_t)(now_us - last_us);
            uint32_t fps_x10 = (uint32_t)(((uint64_t)(curr.frames - prev.frames) * 10000000ULL) / dt_us);
            mira_ui_update_fps(fps_x10);
            mira_recovery_set_fps(fps_x10);
            ESP_LOGI(TAG, "FPS=%" PRIu32 ".%" PRIu32
                 " VSYNC-missed=%" PRIu32 " wait=%" PRIu32 "us"
                 " heap=%u PSRAM=%u",
                 fps_x10/10, fps_x10%10, curr.vsync_missed, curr.last_refresh_us,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            prev = curr;
            last_us = now_us;
        }
        if (next_ms < 2) next_ms = 2;
        if (next_ms > 10) next_ms = 10;
        vTaskDelay(pdMS_TO_TICKS(next_ms));
    }
}
void app_main(void) {
    ESP_LOGI(TAG, "ESP-IDF Waveshare 4.3C 800x480 RGB565; native driver; LVGL %d.%d.%d",
            LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    ESP_LOGW(TAG, "BENCHMARK ONLY (NOT JEEDOM). Native IDF direct RGB PSRAM.");
    ESP_ERROR_CHECK(mira_recovery_init());
    ESP_ERROR_CHECK(mira_display_init());
    mira_ui_bench_create(mira_display_lvgl());
    BaseType_t ok = xTaskCreatePinnedToCore(lvgl_loop, "mira_lvgl", 8192, NULL, 5, NULL, 1);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
