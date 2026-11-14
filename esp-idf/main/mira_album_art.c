// Native IDF version of MiraPanelUI's approved-host album cover worker.
// Spotify / YouTube JPEGs decode in a background task, never inside LVGL.
// Keep the previous image visible until the NEXT fully decoded cover arrives.
#include "mira_album_art.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define MIRA_COVER_URL 768
static const char *TAG="mira-idf-art";
static SemaphoreHandle_t art_lock;
static char requested[MIRA_COVER_URL+1];
static uint32_t art_generation;
static uint8_t art_index;
static bool art_enabled;
static mira_camera_frame_t delivered;
static uint8_t delivered_index;

static bool cover_url(const char *url){
    if(!url||!*url||strlen(url)>MIRA_COVER_URL)return false;
    static const char *const allowed[]={
        "https://i.scdn.co/image/",
        "https://mosaic.scdn.co/",
        "https://yt3.googleusercontent.com/"
    };
    for(unsigned i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++){
        const size_t n=strlen(allowed[i]);
        if(!strncmp(url,allowed[i],n)&&url[n])return true;
    }
    return false;
}
static void cover_worker(void *unused){
    (void)unused;
    uint32_t seen=0;
    for(;;){
        char url[MIRA_COVER_URL+1]={0};
        uint32_t generation=0;
        uint8_t index=0;
        xSemaphoreTake(art_lock,portMAX_DELAY);
        if(art_enabled&&art_generation!=seen){
            generation=art_generation;
            index=art_index;
            snprintf(url,sizeof(url),"%s",requested);
        }
        xSemaphoreGive(art_lock);
        if(!generation){
            vTaskDelay(pdMS_TO_TICKS(180));
            continue;
        }
        seen=generation;
        mira_camera_frame_t frame={0};
        const bool success=mira_camera_fetch_url(url,&frame);
        xSemaphoreTake(art_lock,portMAX_DELAY);
        if(success&&art_enabled&&generation==art_generation){
            if(delivered.pixels)heap_caps_free(delivered.pixels);
            delivered=frame;
            delivered_index=index;
            frame.pixels=NULL;
        }
        xSemaphoreGive(art_lock);
        if(frame.pixels)heap_caps_free(frame.pixels);
        if(!success)ESP_LOGW(TAG,"Media cover unavailable (no stale frame shown as current)");
    }
}
static bool worker_started;
esp_err_t mira_album_art_init(void){
    if(art_lock)return ESP_OK;
    art_lock=xSemaphoreCreateMutex();
    return art_lock?ESP_OK:ESP_ERR_NO_MEM;
}
bool mira_album_art_request(uint8_t widget_index,const char *url){
    if(!cover_url(url))return false;
    if(!art_lock&&mira_album_art_init()!=ESP_OK)return false;
    if(!worker_started){
        // Album JPEG/TLS decoding is deferred and can run on a PSRAM-backed
        // stack; preserve INTERNAL RAM for Wi-Fi, HTTPD, DMA and flash tasks.
        if(xTaskCreateWithCaps(cover_worker,"mira_album_art",6656,NULL,3,NULL,
                               MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){
            ESP_LOGW(TAG,"Deferred JPEG cover worker allocation failed");
            return false;
        }
        worker_started=true;
    }
    xSemaphoreTake(art_lock,portMAX_DELAY);
    // Repeated metadata deliveries never restart a JPEG download.
    if(art_enabled&&art_index==widget_index&&!strcmp(requested,url)){
        xSemaphoreGive(art_lock);
        return true;
    }
    art_generation++;
    if(!art_generation)art_generation++;
    art_index=widget_index;
    art_enabled=true;
    snprintf(requested,sizeof(requested),"%s",url);
    if(delivered.pixels){heap_caps_free(delivered.pixels);memset(&delivered,0,sizeof(delivered));}
    xSemaphoreGive(art_lock);
    return true;
}
bool mira_album_art_take(uint8_t *index,mira_camera_frame_t *frame){
    if(!art_lock||!index||!frame)return false;
    xSemaphoreTake(art_lock,portMAX_DELAY);
    bool got=art_enabled&&delivered.pixels;
    if(got){
        *index=delivered_index;
        *frame=delivered;
        memset(&delivered,0,sizeof(delivered));
    }
    xSemaphoreGive(art_lock);
    return got;
}
void mira_album_art_stop(void){
    if(!art_lock)return;
    xSemaphoreTake(art_lock,portMAX_DELAY);
    art_enabled=false;
    art_generation++;
    requested[0]=0;
    if(delivered.pixels)heap_caps_free(delivered.pixels);
    memset(&delivered,0,sizeof(delivered));
    xSemaphoreGive(art_lock);
}
