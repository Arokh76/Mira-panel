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
#include "mira_full.h"
#include "mira_mqtt.h"
#include "mira_recovery.h"

void mira_recovery_set_fps(uint32_t lvgl_fps_tenths, uint32_t rgb_fps_tenths);
void mira_recovery_set_profile(uint32_t draw_us, uint32_t wait_us, uint32_t handler_max_us);
static const char *TAG = "mira-idf";
static void lvgl_loop(void *arg) {
    (void)arg;
    mira_display_set_render_task();
    mira_display_stats_t prev = {0};
    uint64_t last_us = esp_timer_get_time();
    uint32_t last_wait_total = 0;
    uint32_t last_frames = 0;
    uint64_t draw_total = 0, wait_total = 0;
    uint32_t drawn = 0, max_handler = 0;
    for (;;) {
        mira_full_ui_tick(); // configuration reloads only on the LVGL task
        const uint64_t t0 = esp_timer_get_time();
        uint32_t next_ms = lv_timer_handler();
        uint32_t handler = (uint32_t)(esp_timer_get_time()-t0);
        mira_display_stats_t st;
        mira_display_stats(&st);
        uint32_t waits=st.wait_total_us-last_wait_total;
        uint32_t frames=st.frames-last_frames;
        last_wait_total=st.wait_total_us;
        last_frames=st.frames;
        if(frames) {
            drawn += frames;
            draw_total += handler>waits ? handler-waits : 0;
            wait_total += waits;
            if(handler>max_handler)max_handler=handler;
        }
        const uint64_t now=esp_timer_get_time();
        if(now-last_us >= 1000000) {
            mira_display_stats_t cur;
            mira_display_stats(&cur);
            uint32_t dur=(uint32_t)(now-last_us);
            uint32_t lvfps=(uint32_t)((uint64_t)(cur.frames-prev.frames)*10000000ULL/dur);
            uint32_t rgbfps=(uint32_t)((uint64_t)(cur.vsync_events-prev.vsync_events)*10000000ULL/dur);
            mira_recovery_set_fps(lvfps,rgbfps);
            mira_recovery_set_profile(drawn?(uint32_t)(draw_total/drawn):0,
                                      drawn?(uint32_t)(wait_total/drawn):0,max_handler);
            mira_recovery_set_cache_state(0,0);
            mira_recovery_set_bench_state(0);
            mira_recovery_set_shadow_mode(0);
            mira_recovery_set_auto_state(0);
            mira_recovery_set_scroll_state(0,0,0);
            ESP_LOGI(TAG,"Mira full UI: widgets=%u FPS=%u.%u RGB=%u.%u draw=%u us wait=%u us",
                     (unsigned)mira_full_widget_count(),lvfps/10,lvfps%10,
                     rgbfps/10,rgbfps%10,
                     drawn?(unsigned)(draw_total/drawn):0,
                     drawn?(unsigned)(wait_total/drawn):0);
            prev=cur;last_us=now;drawn=0;draw_total=0;wait_total=0;max_handler=0;
        }
        if(next_ms<2)next_ms=2;
        if(next_ms>12)next_ms=12;
        vTaskDelay(pdMS_TO_TICKS(next_ms));
    }
}
void app_main(void) {
    ESP_LOGI(TAG,"Mira native UI 0.7.37 -> ESP-IDF + LVGL %d.%d.%d on Waveshare 4.3C",
             LVGL_VERSION_MAJOR,LVGL_VERSION_MINOR,LVGL_VERSION_PATCH);
    ESP_LOGW(TAG,"EXPERIMENTAL UI PORT: Jeedom HTTP commands, MQTT, camera and OTA settings are not yet ported");
    mira_full_storage_init(); // no format, preserve original SPIFFS
    ESP_ERROR_CHECK(mira_recovery_init()); // Wi-Fi AP and separate OTA recovery
    ESP_ERROR_CHECK(mira_mqtt_init()); // native optional MQTT transport, read NVS settings
    ESP_ERROR_CHECK(mira_display_init());  // 4.3C known-good 12MHz RGB + GT911
    mira_full_ui_create(mira_display_lvgl());
    BaseType_t ok=xTaskCreatePinnedToCore(lvgl_loop,"mira_lvgl",8192,NULL,5,NULL,1);
    ESP_ERROR_CHECK(ok==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
}
