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
#include <ctype.h>
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
// Atomic counters survive reconnects; diagnostic never records private URLs.
static uint32_t art_requests,art_rejected,art_started,art_worker_errors;
static uint32_t art_fetch_ok,art_fetch_failed,art_frames_taken;
static int32_t last_phase,last_http_status,last_connect_error;
static int32_t last_tls_error,last_tls_flags,last_socket_errno;
void mira_album_art_diagnostics(mira_album_art_diag_t *out){
    if(!out)return;
    out->requests=__atomic_load_n(&art_requests,__ATOMIC_RELAXED);
    out->rejected=__atomic_load_n(&art_rejected,__ATOMIC_RELAXED);
    out->started=__atomic_load_n(&art_started,__ATOMIC_RELAXED);
    out->worker_errors=__atomic_load_n(&art_worker_errors,__ATOMIC_RELAXED);
    out->fetch_ok=__atomic_load_n(&art_fetch_ok,__ATOMIC_RELAXED);
    out->fetch_failed=__atomic_load_n(&art_fetch_failed,__ATOMIC_RELAXED);
    out->frames_taken=__atomic_load_n(&art_frames_taken,__ATOMIC_RELAXED);
    out->last_phase=__atomic_load_n(&last_phase,__ATOMIC_RELAXED);
    out->last_http_status=__atomic_load_n(&last_http_status,__ATOMIC_RELAXED);
    out->last_connect_error=__atomic_load_n(&last_connect_error,__ATOMIC_RELAXED);
    out->last_tls_error=__atomic_load_n(&last_tls_error,__ATOMIC_RELAXED);
    out->last_tls_flags=__atomic_load_n(&last_tls_flags,__ATOMIC_RELAXED);
    out->last_socket_errno=__atomic_load_n(&last_socket_errno,__ATOMIC_RELAXED);
}

// Match the WT32 String::trim() behavior before validating approved hosts.
// Keep strict HTTPS allowlisting; do not rewrite or authorize another domain.
static bool cover_url(const char *url){
    if(!url)return false;
    while(*url&&isspace((unsigned char)*url))url++;
    size_t length=strlen(url);
    while(length&&isspace((unsigned char)url[length-1]))length--;
    if(!length||length>MIRA_COVER_URL)return false;
    static const char *const allowed[]={
        "https://i.scdn.co/image/",
        "https://mosaic.scdn.co/",
        // YouTube Music / Google Cast public artwork thumbnails.
        // Accept explicit Google CDN hosts only (no arbitrary URL fetch).
        "https://lh3.googleusercontent.com/",
        "https://lh4.googleusercontent.com/",
        "https://lh5.googleusercontent.com/",
        "https://lh6.googleusercontent.com/",
        "https://i.ytimg.com/",
        "https://yt3.ggpht.com/",
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
        mira_camera_fetch_diag_t detail={0};
        bool success=mira_camera_fetch_url_diag(url,&frame,&detail);
        if(!success){
            // Retry once only while this URL still belongs to the current song.
            vTaskDelay(pdMS_TO_TICKS(350));
            xSemaphoreTake(art_lock,portMAX_DELAY);
            const bool current=art_enabled&&generation==art_generation;
            xSemaphoreGive(art_lock);
            if(current){
                memset(&detail,0,sizeof(detail));
                success=mira_camera_fetch_url_diag(url,&frame,&detail);
            }
        }
        __atomic_store_n(&last_phase,detail.phase,__ATOMIC_RELAXED);
        __atomic_store_n(&last_http_status,detail.http_status,__ATOMIC_RELAXED);
        __atomic_store_n(&last_connect_error,detail.connect_error,__ATOMIC_RELAXED);
        __atomic_store_n(&last_tls_error,detail.tls_error,__ATOMIC_RELAXED);
        __atomic_store_n(&last_tls_flags,detail.tls_flags,__ATOMIC_RELAXED);
        __atomic_store_n(&last_socket_errno,detail.socket_errno,__ATOMIC_RELAXED);
        if(success)__atomic_fetch_add(&art_fetch_ok,1u,__ATOMIC_RELAXED);
        else {
            __atomic_fetch_add(&art_fetch_failed,1u,__ATOMIC_RELAXED);
            ESP_LOGW(TAG,"Cover fetch failed phase=%ld http=%ld connect=%ld tls=%ld flags=%ld errno=%ld",
                (long)detail.phase,(long)detail.http_status,
                (long)detail.connect_error,(long)detail.tls_error,
                (long)detail.tls_flags,(long)detail.socket_errno);
        }
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
    __atomic_fetch_add(&art_requests,1u,__ATOMIC_RELAXED);
    if(!cover_url(url)){
        __atomic_fetch_add(&art_rejected,1u,__ATOMIC_RELAXED);
        return false;
    }
    // WT32 trims URL whitespace before storing/comparing/fetching artwork.
    while(isspace((unsigned char)*url))url++;
    size_t url_length=strlen(url);
    while(url_length&&isspace((unsigned char)url[url_length-1]))url_length--;
    if(!art_lock&&mira_album_art_init()!=ESP_OK)return false;
    if(!worker_started){
        // Album JPEG/TLS decoding is deferred and can run on a PSRAM-backed
        // stack; preserve INTERNAL RAM for Wi-Fi, HTTPD, DMA and flash tasks.
        if(xTaskCreateWithCaps(cover_worker,"mira_album_art",6656,NULL,3,NULL,
                               MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){
            __atomic_fetch_add(&art_worker_errors,1u,__ATOMIC_RELAXED);
            ESP_LOGW(TAG,"Deferred JPEG cover worker allocation failed");
            return false;
        }
        worker_started=true;
    }
    xSemaphoreTake(art_lock,portMAX_DELAY);
    // Repeated metadata deliveries never restart a JPEG download.
    if(art_enabled&&art_index==widget_index&&
       strlen(requested)==url_length&&!strncmp(requested,url,url_length)){
        xSemaphoreGive(art_lock);
        return true;
    }
    art_generation++;
    if(!art_generation)art_generation++;
    art_index=widget_index;
    art_enabled=true;
    __atomic_fetch_add(&art_started,1u,__ATOMIC_RELAXED);
    memcpy(requested,url,url_length);
    requested[url_length]=0;
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
        __atomic_fetch_add(&art_frames_taken,1u,__ATOMIC_RELAXED);
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
