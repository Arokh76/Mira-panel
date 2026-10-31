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
void mira_recovery_set_profile(uint32_t draw_us, uint32_t wait_us, uint32_t handler_max_us);
static const char *TAG = "mira-idf";
static void lvgl_loop(void *arg) {
    (void)arg;
    mira_display_set_render_task();
    mira_display_stats_t prev = {0};
    uint64_t last_us = esp_timer_get_time();
    uint64_t last_display_us = last_us;
    uint32_t last_wait_total = 0;
    uint32_t last_frame_count = 0;
    uint64_t period_draw_us = 0;
    uint64_t period_wait_us = 0;
    uint32_t period_render_frames = 0;
    uint32_t period_max_handler_us = 0;
    for (;;) {
        int64_t t0 = esp_timer_get_time();
        uint32_t next_ms = lv_timer_handler();
        uint64_t now_us = esp_timer_get_time();
        uint32_t elapsed_us = (uint32_t)(now_us - t0);
        mira_display_stats_t current;
        mira_display_stats(&current);
        uint32_t waited_us = current.wait_total_us - last_wait_total;
        uint32_t rendered = current.frames - last_frame_count;
        last_wait_total = current.wait_total_us;
        last_frame_count = current.frames;
        if (rendered) {
            period_draw_us += (elapsed_us > waited_us ? elapsed_us - waited_us : 0);
            period_wait_us += waited_us;
            period_render_frames += rendered;
            if (elapsed_us > period_max_handler_us) period_max_handler_us = elapsed_us;
        }
        if (now_us - last_us >= 1000000ULL) {
            mira_display_stats_t curr;
            mira_display_stats(&curr);
            uint32_t dt_us = (uint32_t)(now_us - last_us);
            uint32_t fps_x10 = (uint32_t)(((uint64_t)(curr.frames - prev.frames) * 10000000ULL) / dt_us);
            uint32_t avg_draw_us = period_render_frames
                ? (uint32_t)(period_draw_us / period_render_frames) : 0;
            uint32_t avg_wait_us = period_render_frames
                ? (uint32_t)(period_wait_us / period_render_frames) : 0;
            // Keep /perf at 1-second granularity, but invalidate the top FPS
            // label only every 3 seconds to diagnose/reduce top-strip shimmer.
            if (now_us - last_display_us >= 3000000ULL) {
                mira_ui_update_fps(fps_x10);
                last_display_us = now_us;
            }
            mira_recovery_set_fps(fps_x10);
            mira_recovery_set_profile(avg_draw_us, avg_wait_us, period_max_handler_us);
            ESP_LOGI(TAG, "FPS=%" PRIu32 ".%" PRIu32
                 " draw_avg=%" PRIu32 "us wait_avg=%" PRIu32
                 "us handler_max=%" PRIu32 "us VSYNC-missed=%" PRIu32
                 " heap=%u PSRAM=%u",
                 fps_x10/10, fps_x10%10, avg_draw_us, avg_wait_us,
                 period_max_handler_us, curr.vsync_missed,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            period_draw_us = 0;
            period_wait_us = 0;
            period_render_frames = 0;
            period_max_handler_us = 0;
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
