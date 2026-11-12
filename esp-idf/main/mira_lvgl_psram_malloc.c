// LVGL 9.6 external allocator for Waveshare ESP32-S3 7-inch, clock-only A/B.
//
// LVGL's default libc backend calls malloc(), and ESP-IDF default config
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384 prefers scarce INTERNAL RAM for
// each small LVGL object. With RGB+APSTA+HTTP running there were just 1.6KB
// contiguous internal bytes when the first clock tile was parsed.
//
// This file is enabled with CONFIG_LV_USE_CUSTOM_MALLOC=y. Allocate *only*
// LVGL objects and render scratch memory explicitly from octal PSRAM. No
// changes are made to Wi-Fi stacks, HTTP stacks or RGB/DMA reserved memory.
#include <stddef.h>
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG="mira-lvgl-psram";

void lv_mem_init(void)
{
    ESP_LOGI(TAG,"LVGL malloc backend = PSRAM 8-bit, free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
}

void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *mem,size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t bytes)
{
    if(bytes==0)return NULL;
    void *ptr=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!ptr){
        ESP_LOGE(TAG,"LVGL PSRAM allocation failed: %u bytes; PSRAM free=%u",
                 (unsigned)bytes,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    }
    return ptr;
}

void *lv_realloc_core(void *ptr,size_t bytes)
{
    if(!bytes){heap_caps_free(ptr);return NULL;}
    void *next=heap_caps_realloc(ptr,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!next){
        ESP_LOGE(TAG,"LVGL PSRAM realloc failed: %u bytes",(unsigned)bytes);
    }
    return next;
}

void lv_free_core(void *ptr)
{
    heap_caps_free(ptr);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    if(!mon)return;
    const size_t total=heap_caps_get_total_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    const size_t free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    mon->total_size=total;
    mon->free_size=free;
    mon->free_biggest_size=heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(total)mon->used_pct=(uint8_t)((total-free)*100u/total);
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}

size_t mira_lvgl_psram_largest(void)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
