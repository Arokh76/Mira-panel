// Waveshare 7 STD diagnostic stage 2: REAL Mira UI, no network/services.
// Based on the independently user-validated RGB/GT911 + LVGL9 baseline.
// No Wi-Fi, HTTP, MQTT, Jeedom actions, camera or album-art worker.
// Preserve SPIFFS, NVS, RGB timing, DMA bounce size and LVGL configuration.
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "mira_display.h"
#include "mira_full.h"
#include "mira_net_diag.h"

static const char *TAG = "mira7-ui-wifi-diag";
static volatile uint32_t ui_ticks;
static lv_obj_t *health_dot;

static void halt_on_error(const char *stage, esp_err_t err)
{
    ESP_LOGE(TAG, "OFFLINE_UI_FAILED at %s: %s (0x%x)",
             stage, esp_err_to_name(err), (unsigned)err);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

static void ui_tick(lv_timer_t *t)
{
    (void)t;
    mira_full_ui_tick();
}

static void heartbeat(lv_timer_t *t)
{
    (void)t;
    ui_ticks++;
    // Tiny marker on top layer indicates LVGL is still rendering and updating.
    if (health_dot) {
        lv_obj_set_x(health_dot,(ui_ticks & 1U) ? 764 : 780);
    }
    if ((ui_ticks % 4U) == 0) {
        mira_display_stats_t d;
        mira_display_stats(&d);
        ESP_LOGI(TAG, "OFFLINE_UI_ALIVE: widgets=%" PRIu32
                  ", refreshes=%" PRIu32 ", GT911=%s, uptime=%" PRIu32 "s",
                 mira_full_widget_count(), d.frames,
                 d.touch_ready ? "ready" : "not ready", ui_ticks / 2U);
    }
}

static void install_offline_indicator(void)
{
    health_dot=lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(health_dot);
    lv_obj_set_size(health_dot, 8, 8);
    lv_obj_set_pos(health_dot, 780, 8);
    lv_obj_set_style_bg_color(health_dot, lv_color_hex(0x00FF00), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(health_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(health_dot, LV_OBJ_FLAG_CLICKABLE);
}

void app_main(void)
{
    ESP_LOGI(TAG, "STAGE 3 Mira real UI + minimal AP/HTTP, proven Waveshare RGB profile");
    // Avoid discarding existing NVS if it is incompatible.
    esp_err_t nvs_err=nvs_flash_init();
    if (nvs_err!=ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable (%s). Reading UI defaults only; no erase",
                 esp_err_to_name(nvs_err));
    }

    // Exactly as in the successful hardware-only diagnostic: reserve RGB DMA
    // before any filesystem or UI allocations.
    esp_err_t err=mira_display_preallocate_rgb();
    if (err!=ESP_OK) halt_on_error("early RGB allocation",err);

    const bool have_spiffs=mira_full_storage_mount_existing_only();
    ESP_LOGI(TAG,"OFFLINE_UI_STORAGE: %s",have_spiffs?"existing SPIFFS":"no mounted SPIFFS");

    err=mira_display_init();
    if (err!=ESP_OK) halt_on_error("RGB, GT911 and LVGL init",err);
    ESP_LOGI(TAG,"OFFLINE_UI_STAGE: display initialized");

    err=esp_lv_adapter_start();
    if (err!=ESP_OK) halt_on_error("LVGL adapter start",err);
    ESP_LOGI(TAG,"OFFLINE_UI_STAGE: LVGL started");

    err=esp_lv_adapter_lock(-1);
    if (err!=ESP_OK) halt_on_error("LVGL lock",err);
    mira_full_ui_create(mira_display_lvgl()); // Real config from saved miralvgl.json.
    install_offline_indicator();
    lv_timer_create(ui_tick, 10, NULL);
    lv_timer_create(heartbeat, 500, NULL);
    esp_lv_adapter_unlock();
    ESP_LOGI(TAG, "OFFLINE_UI_READY: Mira rendered, starting DIAGNOSTIC AP only");
    // The panel, LVGL and touchscreen have now been initialized successfully.
    // Keep all Wi-Fi and HTTP callbacks outside the LVGL task and never call
    // the original Mira network/recovery, Jeedom, MQTT or camera startup.
    // Failure here must NOT restart the display or erase any configuration.
    const esp_err_t net_err=mira_net_diag_start();
    if(net_err!=ESP_OK)
        ESP_LOGE(TAG,"MIRA7_NET_DIAG_FAILED: %s; display remains alive",esp_err_to_name(net_err));
}
