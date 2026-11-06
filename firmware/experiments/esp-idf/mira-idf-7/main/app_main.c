// Mira ESP-IDF Waveshare 7 STD: manufacturer RGB/LVGL9 baseline.
// Espressif adapter owns VSYNC, framebuffers and LVGL task; Mira UI is a timer callback.
#include <stdint.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "mira_display.h"
#include "mira_full.h"
#include "mira_mqtt.h"
#include "mira_jeedom.h"
#include "mira_camera.h"
#include "mira_recovery.h"

void mira_recovery_set_fps(uint32_t lvgl_fps_tenths, uint32_t rgb_fps_tenths);
void mira_recovery_set_profile(uint32_t draw_us, uint32_t wait_us, uint32_t handler_max_us);
static const char *TAG = "mira-idf";

static void mira_lvgl_timer(lv_timer_t *timer) {
    (void)timer;
    static uint64_t last_us;
    static uint32_t last_frames;
    static uint64_t ui_total_us;
    static uint32_t ui_calls;
    static uint32_t ui_max_us;
    const uint64_t start_us = esp_timer_get_time();
    // Called by the adapter LVGL task, not HTTP/MQTT worker tasks.
    mira_full_ui_tick();
    const uint32_t elapsed_us = (uint32_t)(esp_timer_get_time() - start_us);
    ui_total_us += elapsed_us;
    ui_calls++;
    if (elapsed_us > ui_max_us) ui_max_us = elapsed_us;
    const uint64_t now_us = esp_timer_get_time();
    if (!last_us) last_us = now_us;
    if (now_us - last_us < 1000000) return;

    mira_display_stats_t st;
    mira_display_stats(&st);
    const uint32_t interval_us = (uint32_t)(now_us - last_us);
    const uint32_t lvfps = (uint32_t)(((uint64_t)(st.frames - last_frames) * 10000000ULL) / interval_us);
    // Native VSYNC count is owned by Espressif adapter. Report 0 = unavailable.
    mira_recovery_set_fps(lvfps, 0);
    mira_recovery_set_profile(ui_calls ? (uint32_t)(ui_total_us / ui_calls) : 0, 0, ui_max_us);
    mira_recovery_set_cache_state(0, 0);
    mira_recovery_set_bench_state(0);
    mira_recovery_set_shadow_mode(0);
    mira_recovery_set_auto_state(0);
    mira_recovery_set_scroll_state(0, 0, 0);
    ESP_LOGI(TAG, "Mira 7 vendor adapter: widgets=%u LVGL=%u.%u FPS UIavg=%u us (RGB VSYNC managed by adapter)",
             (unsigned)mira_full_widget_count(), lvfps / 10, lvfps % 10,
             ui_calls ? (unsigned)(ui_total_us / ui_calls) : 0);
    last_us = now_us;
    last_frames = st.frames;
    ui_total_us = 0;
    ui_calls = 0;
    ui_max_us = 0;
}

void app_main(void) {
    ESP_LOGI(TAG, "Mira 7 STD ESP-IDF Waveshare RGB 16MHz/bounce + Espressif LVGL adapter 0.5.2");
    ESP_LOGW(TAG, "EXPERIMENTAL vendor baseline: hardware stability needs real 7-inch validation");
    // Preserve the existing Mira settings, widgets, firmware partitioning and Wi-Fi.
    mira_full_storage_init();
    // Early RGB driver allocation reserves scarce internal DMA memory before Wi-Fi.
    // Physical CH422G/GT911 bring-up and LVGL start remain after the services.
    ESP_ERROR_CHECK(mira_display_preallocate_rgb());
    ESP_ERROR_CHECK(mira_recovery_init());
    ESP_ERROR_CHECK(mira_mqtt_init());
    ESP_ERROR_CHECK(mira_jeedom_action_init());
    ESP_ERROR_CHECK(mira_camera_init());
    ESP_ERROR_CHECK(mira_display_init()); // CH422G, GT911, vendor RGB 800x480 16MHz
    ESP_ERROR_CHECK(esp_lv_adapter_start());
    ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
    mira_full_ui_create(mira_display_lvgl());
    lv_timer_create(mira_lvgl_timer, 10, NULL);
    esp_lv_adapter_unlock();
}
