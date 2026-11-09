// Waveshare ESP32-S3 Touch LCD 7 Standard: full native setup experimental build.
// Keep validated RGB/CH422G/GT911 parameters and stage3 lowmem Wi-Fi settings.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/stat.h>
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
#include "mira_recovery.h"
#include "mira_beta_setup.h"
#include "mira_jeedom.h"
#include "mira_mqtt.h"
#include "mira_splash.h"
#include "mira_lvgl_psram_malloc.h"

static const char *TAG="mira7-full";
static void checkpoint(const char *stage){
    const uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA;
    ESP_LOGI(TAG,"FULL_BOOT_MEM %s: dma_free=%u largest=%u psram=%u",
        stage,(unsigned)heap_caps_get_free_size(caps),
        (unsigned)heap_caps_get_largest_free_block(caps),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
static void halt_boot(const char *part,esp_err_t err){
    ESP_LOGE(TAG,"FULL_BOOT_HALTED %s: %s",part,esp_err_to_name(err));
    for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
}
static void progress(uint8_t n){
    if(esp_lv_adapter_lock(-1)==ESP_OK){
        mira_splash_set_progress(n);
        esp_lv_adapter_unlock();
    }
}
static void ui_tick(lv_timer_t *unused){
    (void)unused;
    mira_full_ui_tick();
}
void app_main(void){
    ESP_LOGI(TAG,"MIRA7 FULL PROVISION - experimental release");
    esp_err_t err=nvs_flash_init();
    if(err!=ESP_OK)halt_boot("NVS (no erase)",err);
    checkpoint("START");
    // Force the linker to pull the custom LVGL memory backend from libmain.a
    // before resolving references from liblvgl.a.
    ESP_LOGI(TAG,"LVGL PSRAM custom backend linked: largest=%u",
             (unsigned)mira_lvgl_psram_largest());
    err=mira_display_preallocate_rgb();
    if(err!=ESP_OK)halt_boot("RGB DMA",err);
    checkpoint("RGB_RESERVED");

    // Formatting a fully erased SPIFFS partition performs many flash writes.
    // Complete this operation BEFORE starting the LVGL task whose stack is in PSRAM.
    // Never format a nonblank partition; user data takes priority over a mount error.
    checkpoint("BEFORE_STORAGE");
    const bool storage=mira_full_storage_prepare();
    checkpoint("AFTER_STORAGE");
    if(!storage)ESP_LOGW(TAG,"SPIFFS unavailable: widgets cannot be imported yet");

    err=mira_display_init();
    if(err!=ESP_OK)halt_boot("RGB GT911 LVGL",err);
    err=esp_lv_adapter_start();
    if(err!=ESP_OK)halt_boot("LVGL task",err);
    checkpoint("DISPLAY_READY");

    err=esp_lv_adapter_lock(-1);
    if(err!=ESP_OK)halt_boot("LVGL lock",err);
    mira_splash_show(mira_display_lvgl());
    mira_splash_set_progress(20);
    esp_lv_adapter_unlock();

    // Reserve network and HTTP before creating the full LVGL widget tree.
    progress(35);
    checkpoint("BEFORE_NETWORK");
    const esp_err_t net=mira_recovery_init();
    checkpoint("AFTER_NETWORK");
    if(net!=ESP_OK){
        ESP_LOGE(TAG,"FULL_NET_FAILED at %s: %s (0x%x)",
                 mira_recovery_last_stage(),esp_err_to_name(net),(unsigned)net);
        progress(50);
        // Failure used to continue creating the complete Mira UI and
        // could panic/reboot while internal DMA RAM was exhausted.
        // Do NOT start a second Wi-Fi stack or erase user configuration.
        if(esp_lv_adapter_lock(-1)==ESP_OK){
            char msg[96];
            snprintf(msg,sizeof(msg),"Reseau %s : %s",mira_recovery_last_stage(),esp_err_to_name(net));
            mira_splash_set_message(msg);
            esp_lv_adapter_unlock();
        }
        checkpoint("NETWORK_FAILED_HALT");
        ESP_LOGE(TAG,"NETWORK_FAILED_HALT: stopped before widgets, no reset requested");
        for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG,"FULL_NET_READY: native Mira AP/STA + HTTP setup");
    progress(60);
    if(mira_recovery_web_fallback()){
        if(esp_lv_adapter_lock(-1)==ESP_OK){
            mira_splash_set_message("Mode secours HTTP : /status\n192.168.4.1");
            esp_lv_adapter_unlock();
        }
        ESP_LOGW(TAG,"MIRA_HTTP_RESCUE: no widgets/services until full server memory is available");
        checkpoint("HTTP_RESCUE_IDLE");
        return;
    }

    const bool setup=mira_beta_setup_unconfigured();
    ESP_LOGI(TAG,"FULL_SETUP_MODE: %s",setup?"FIRST_INSTALL":"CONFIGURED");
    if(setup){
        // First installation needs ONLY the Wi-Fi/key web wizard; the 800x480
        // dashboard can be built after the user saves credentials and reboots.
        // LVGL label is tiny; no widgets, queues, camera/Jeedom workers allocated.
        if(esp_lv_adapter_lock(-1)==ESP_OK){
            mira_splash_set_message("Configuration : Mira Panel\nhttp://192.168.4.1");
            esp_lv_adapter_unlock();
        }
        ESP_LOGI(TAG,"FIRST_SETUP_READY: waiting for key + Wi-Fi, no dashboard allocation");
        checkpoint("FIRST_SETUP_IDLE");
        return; // app_main task may terminate; LVGL/HTTP tasks remain running
    }
    // A first-time unit has no /spiffs/miralvgl.json yet. Previous firmware
    // unconditionally started Jeedom/MQTT workers and built six LVGL pages;
    // that wastes scarce internal RAM and may trigger a reset right after 60%.
    // Keep native Wi-Fi + HTTP import routes running until a real config exists.
    struct stat ui_config_info={0};
    const bool has_ui_config=storage &&
        stat("/spiffs/miralvgl.json",&ui_config_info)==0 &&
        ui_config_info.st_size>2;
    if(!has_ui_config){
        if(esp_lv_adapter_lock(-1)==ESP_OK){
            mira_splash_set_message("Reseau pret - aucun widget\nImporte les tuiles, puis redemarre");
            esp_lv_adapter_unlock();
        }
        ESP_LOGW(TAG,"MIRA_NO_WIDGETS_IDLE: saved Wi-Fi OK, no tile JSON; Jeedom/MQTT/UI deferred");
        checkpoint("NO_WIDGETS_IDLE");
        return; // HTTP server and LVGL remain active to import configuration
    }
    ESP_LOGI(TAG,"MIRA_WIDGET_CONFIG_FOUND: %ld bytes",
             (long)ui_config_info.st_size);
    // CLOCK-ONLY ISOLATION A/B: keep the saved widget JSON and real full
    // 800x480 dashboard, but do not create Jeedom event workers or MQTT
    // client, and do not pre-allocate the passive state queues. This is a
    // diagnostic firmware only: never send Jeedom commands from this version.
    ESP_LOGW(TAG,"MIRA_UI_ISOLATED: Jeedom+MQTT+state queues intentionally disabled");
    progress(75);
    vTaskDelay(pdMS_TO_TICKS(350));
    checkpoint("CLOCK_UI_ONLY_NO_SERVICES");
    progress(92);
    vTaskDelay(pdMS_TO_TICKS(350));
    checkpoint("BEFORE_UI");
    err=esp_lv_adapter_lock(-1);
    if(err!=ESP_OK)halt_boot("UI lock",err);
    ESP_LOGW(TAG,"MIRA_UI_CREATION_BEGIN: only widgets and LVGL, no MQTT/Jeedom");
    mira_full_ui_create(mira_display_lvgl());
    ESP_LOGI(TAG,"MIRA_UI_CREATION_RETURN: widgets=%" PRIu32,mira_full_widget_count());
    lv_timer_create(ui_tick,10,NULL);
    mira_splash_set_progress(100);
    esp_lv_adapter_unlock();
    checkpoint("AFTER_UI");

    vTaskDelay(pdMS_TO_TICKS(180));
    if(esp_lv_adapter_lock(-1)==ESP_OK){
        mira_splash_hide();
        esp_lv_adapter_unlock();
    }
    ESP_LOGI(TAG,"CLOCK_UI_ISOLATION_READY: widgets=%" PRIu32 " setup=%s network=%s storage=%s",
        mira_full_widget_count(),setup?"YES":"NO",net==ESP_OK?"OK":"FAIL",
        storage?"OK":"FAIL");
    checkpoint("FINAL");
}
