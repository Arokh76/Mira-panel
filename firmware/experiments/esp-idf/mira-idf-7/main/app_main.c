// Waveshare 7" staged boot A/B. Preserve stable stage-3 firmware on its own branch.
// RGB/GT911 validated settings, Wi-Fi lowmem sdkconfig and SPIFFS untouched.
// Order: early RGB DMA -> display/LVGL -> tiny splash -> AP/HTTP -> storage -> Mira UI.
// No Jeedom/MQTT/camera/OTA workers enabled by this diagnostic build.
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "mira_display.h"
#include "mira_full.h"
#include "mira_net_diag.h"
#include "mira_splash.h"

static const char *TAG="mira7-staged-boot";

static void memory_checkpoint(const char *stage)
{
    const uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA;
    ESP_LOGI(TAG,"BOOT_MEM %s: internal_dma_free=%u largest=%u min_free=%u psram_free=%u",
         stage,
         (unsigned)heap_caps_get_free_size(caps),
         (unsigned)heap_caps_get_largest_free_block(caps),
         (unsigned)heap_caps_get_minimum_free_size(caps),
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void halt_on_error(const char *stage,esp_err_t err)
{
    ESP_LOGE(TAG,"STAGED_BOOT_FAILED at %s: %s (0x%x)",
             stage,esp_err_to_name(err),(unsigned)err);
    for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
}

// Called from app_main; LVGL adapter task renders asynchronously.
static void progress(uint8_t value)
{
    if(esp_lv_adapter_lock(-1)==ESP_OK){
        mira_splash_set_progress(value);
        esp_lv_adapter_unlock();
    }
}
static void ui_tick(lv_timer_t *unused)
{
    (void)unused;
    mira_full_ui_tick();
}

void app_main(void)
{
    ESP_LOGI(TAG,"MIRA7 STAGED BOOT: RGB -> splash -> Wi-Fi -> Mira UI");
    // Do not reset, erase, or format any user data.
    esp_err_t nvs_err=nvs_flash_init();
    if(nvs_err!=ESP_OK)
        ESP_LOGW(TAG,"NVS unavailable (%s), no erase attempted",esp_err_to_name(nvs_err));

    memory_checkpoint("BEFORE_RGB");
    esp_err_t err=mira_display_preallocate_rgb();
    if(err!=ESP_OK)halt_on_error("RGB DMA preallocation",err);
    memory_checkpoint("AFTER_RGB");

    err=mira_display_init();
    if(err!=ESP_OK)halt_on_error("display / GT911 / LVGL init",err);
    err=esp_lv_adapter_start();
    if(err!=ESP_OK)halt_on_error("LVGL adapter",err);
    memory_checkpoint("AFTER_LVGL");

    err=esp_lv_adapter_lock(-1);
    if(err!=ESP_OK)halt_on_error("LVGL lock",err);
    mira_splash_show(mira_display_lvgl());
    mira_splash_set_progress(20);
    esp_lv_adapter_unlock();

    // AP and HTTP must reserve their internal memory before the heavy UI tree.
    // Keep the proven min-RX/TX Wi-Fi setup, and don't enable production services.
    progress(30);
    memory_checkpoint("BEFORE_WIFI");
    const esp_err_t net_err=mira_net_diag_start();
    memory_checkpoint("AFTER_WIFI");
    if(net_err==ESP_OK){
        ESP_LOGI(TAG,"STAGED_NET_READY: AP + HTTP listening");
        progress(55);
    }else{
        ESP_LOGE(TAG,"STAGED_NET_FAILED: %s; continuing with offline Mira UI",
                 esp_err_to_name(net_err));
        progress(45);
    }

    // Existing SPIFFS only. A missing filesystem is not formatted.
    const bool have_spiffs=mira_full_storage_mount_existing_only();
    ESP_LOGI(TAG,"STAGED_STORAGE: %s",have_spiffs?"mounted":"unavailable (preserved)");
    progress(65);

    // Build all Mira widgets after network allocations, under the LVGL lock.
    memory_checkpoint("BEFORE_UI");
    err=esp_lv_adapter_lock(-1);
    if(err!=ESP_OK)halt_on_error("Mira UI lock",err);
    mira_full_ui_create(mira_display_lvgl());
    lv_timer_create(ui_tick,10,NULL);
    mira_splash_set_progress(100);
    esp_lv_adapter_unlock();
    memory_checkpoint("AFTER_UI");

    // Give the last 100% frame a brief opportunity to render before removal.
    vTaskDelay(pdMS_TO_TICKS(180));
    if(esp_lv_adapter_lock(-1)==ESP_OK){
        mira_splash_hide();
        esp_lv_adapter_unlock();
    }
    ESP_LOGI(TAG,"STAGED_UI_READY: widgets=%" PRIu32 ", wifi=%s, splash freed",
             mira_full_widget_count(),net_err==ESP_OK?"OK":"FAILED");
    memory_checkpoint("FINAL");
}
